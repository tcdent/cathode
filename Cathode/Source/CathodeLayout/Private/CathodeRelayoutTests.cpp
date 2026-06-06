#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeLayout.h"
#include "CathodeRelayout.h"

#include "Misc/AutomationTest.h"

// Specs for the BESPOKE relayout intelligence — affected-subtree selection, dirty bookkeeping, and the
// both-path relayout invocation. All litehtml-free: the selection is a pure graph computation on a
// scripted retained tree; the invocation drives a RECORDING backend that records WHICH subtree was
// requested (root + width). We assert OUR selection/bookkeeping/request-scope, NEVER litehtml's computed
// boxes (the layout-output tautology). efficient==full equivalence is the deferred validation-pass proof.
// Authored, not run this sprint.

namespace
{
using namespace Cathode;

// A scripted retained tree:  0[B] -> 1 -> 2[B] -> 3 -> 4   (B = layout boundary; root 0 is a boundary).
class FStubDocumentSource final : public ILayoutDocumentSource
{
public:
	int32 Root = 0;
	TMap<int32, int32> Parent;          // node -> parent (absent => INDEX_NONE, i.e. the root)
	TSet<int32> Boundaries;

	virtual int32 GetRootNode() const override { return Root; }
	virtual int32 GetParentNode(int32 NodeId) const override
	{
		const int32* P = Parent.Find(NodeId);
		return P ? *P : INDEX_NONE;
	}
	virtual void GetChildNodes(int32, TArray<int32>&) const override {}
	virtual bool IsLayoutBoundary(int32 NodeId) const override { return Boundaries.Contains(NodeId); }
	virtual FString GetDocumentHtml() const override { return TEXT("<html></html>"); }
	virtual FString GetSubtreeHtml(int32) const override { return TEXT("<div></div>"); }
	virtual FString GetDocumentCss() const override { return FString(); }
};

FStubDocumentSource MakeChainTree()
{
	FStubDocumentSource Tree;
	Tree.Root = 0;
	Tree.Parent.Add(1, 0);
	Tree.Parent.Add(2, 1);
	Tree.Parent.Add(3, 2);
	Tree.Parent.Add(4, 3);
	Tree.Boundaries.Add(0); // root is a boundary
	Tree.Boundaries.Add(2); // an inner boundary
	return Tree;
}

struct FRelayoutRequest
{
	bool bDocument = false;
	int32 Node = INDEX_NONE;
	int32 Width = 0;
};

class FRecordingRelayoutBackend final : public IRelayoutBackend
{
public:
	TArray<FRelayoutRequest> Requests;
	virtual void RelayoutDocument(int32 Width) override { Requests.Add(FRelayoutRequest{ true, INDEX_NONE, Width }); }
	virtual void RelayoutSubtree(int32 Node, int32 Width) override { Requests.Add(FRelayoutRequest{ false, Node, Width }); }
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeLayoutSelectionTest,
	"Cathode.Layout.Relayout.AffectedSubtreeSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutSelectionTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;
	const FStubDocumentSource Tree = MakeChainTree();

	// Layout dirty on node 4 -> nearest boundary is 2 (not the root) -> relayout subtree {2}.
	{
		TArray<FDirtyMark> Marks; Marks.Add(FDirtyMark{ 4, EDirtyKind::Layout });
		const FRelayoutPlan Plan = ComputeRelayoutPlan(Tree, Marks);
		TestFalse(TEXT("not a full relayout"), Plan.bFull);
		TestEqual(TEXT("one affected subtree root"), Plan.SubtreeRoots.Num(), 1);
		if (Plan.SubtreeRoots.Num() == 1)
		{
			TestEqual(TEXT("the relayout root is the nearest boundary (2)"), Plan.SubtreeRoots[0], 2);
		}
	}

	// Layout dirty on node 1 -> walk reaches the root boundary -> a complete re-feed.
	{
		TArray<FDirtyMark> Marks; Marks.Add(FDirtyMark{ 1, EDirtyKind::Layout });
		const FRelayoutPlan Plan = ComputeRelayoutPlan(Tree, Marks);
		TestTrue(TEXT("a root-level boundary => full relayout"), Plan.bFull);
	}

	// PaintOnly dirty -> no relayout at all.
	{
		TArray<FDirtyMark> Marks; Marks.Add(FDirtyMark{ 4, EDirtyKind::PaintOnly });
		const FRelayoutPlan Plan = ComputeRelayoutPlan(Tree, Marks);
		TestFalse(TEXT("paint-only is not a full relayout"), Plan.bFull);
		TestEqual(TEXT("paint-only has no relayout roots"), Plan.SubtreeRoots.Num(), 0);
		TestFalse(TEXT("paint-only requires no relayout"), Plan.RequiresRelayout());
	}

	// Two layout dirties both under boundary 2 -> de-duplicated to a single subtree root.
	{
		TArray<FDirtyMark> Marks;
		Marks.Add(FDirtyMark{ 3, EDirtyKind::Layout });
		Marks.Add(FDirtyMark{ 4, EDirtyKind::Structure });
		const FRelayoutPlan Plan = ComputeRelayoutPlan(Tree, Marks);
		TestFalse(TEXT("not full"), Plan.bFull);
		TestEqual(TEXT("de-duplicated to one root"), Plan.SubtreeRoots.Num(), 1);
	}

	// One dirty under the root boundary subsumes the others into a full relayout.
	{
		TArray<FDirtyMark> Marks;
		Marks.Add(FDirtyMark{ 4, EDirtyKind::Layout }); // -> boundary 2
		Marks.Add(FDirtyMark{ 1, EDirtyKind::Layout }); // -> root => full
		const FRelayoutPlan Plan = ComputeRelayoutPlan(Tree, Marks);
		TestTrue(TEXT("a root-reaching mark subsumes to full"), Plan.bFull);
		TestEqual(TEXT("full clears subtree roots"), Plan.SubtreeRoots.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeLayoutDirtyBookkeepingTest,
	"Cathode.Layout.Relayout.DirtyBookkeeping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutDirtyBookkeepingTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FDirtyTracker Tracker;
	TestFalse(TEXT("starts clean"), Tracker.HasAnyDirty());

	Tracker.Mark(5, EDirtyKind::PaintOnly);
	TestTrue(TEXT("has a mark"), Tracker.HasAnyDirty());
	TestFalse(TEXT("paint-only is not a layout dirty"), Tracker.HasLayoutDirty());

	Tracker.Mark(6, EDirtyKind::Layout);
	TestTrue(TEXT("a layout mark is a layout dirty"), Tracker.HasLayoutDirty());
	TestEqual(TEXT("two marks accumulated"), Tracker.GetMarks().Num(), 2);

	Tracker.Flush();
	TestFalse(TEXT("empty after flush"), Tracker.HasAnyDirty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeLayoutRelayoutInvocationTest,
	"Cathode.Layout.Relayout.BothPathInvocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutRelayoutInvocationTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;
	const FStubDocumentSource Tree = MakeChainTree();

	FRelayoutController Controller;
	Controller.SetDocument(Tree);
	FRecordingRelayoutBackend Backend;

	// First relayout -> complete re-feed of the whole document.
	Controller.Relayout(800, Backend);
	TestEqual(TEXT("first relayout requested once"), Backend.Requests.Num(), 1);
	if (Backend.Requests.Num() == 1)
	{
		TestTrue(TEXT("first relayout is the complete document path"), Backend.Requests[0].bDocument);
		TestEqual(TEXT("complete path uses the available width"), Backend.Requests[0].Width, 800);
	}

	// A localized Layout dirty -> intelligent re-feed of ONLY the affected subtree (root 2).
	Controller.MarkNodeDirty(4, EDirtyKind::Layout);
	Controller.Relayout(800, Backend);
	TestEqual(TEXT("a second request was made"), Backend.Requests.Num(), 2);
	if (Backend.Requests.Num() == 2)
	{
		TestFalse(TEXT("the intelligent path is NOT the document path"), Backend.Requests[1].bDocument);
		TestEqual(TEXT("it relaid out ONLY the affected subtree (2)"), Backend.Requests[1].Node, 2);
	}

	// A root-reaching Layout dirty -> back to the complete path.
	Controller.MarkNodeDirty(1, EDirtyKind::Layout);
	Controller.Relayout(800, Backend);
	TestEqual(TEXT("a third request was made"), Backend.Requests.Num(), 3);
	if (Backend.Requests.Num() == 3)
	{
		TestTrue(TEXT("a root-reaching dirty drives the complete path"), Backend.Requests[2].bDocument);
	}

	// A PaintOnly dirty -> NO relayout request at all.
	Controller.MarkNodeDirty(4, EDirtyKind::PaintOnly);
	Controller.Relayout(800, Backend);
	TestEqual(TEXT("paint-only made no relayout request"), Backend.Requests.Num(), 3);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
