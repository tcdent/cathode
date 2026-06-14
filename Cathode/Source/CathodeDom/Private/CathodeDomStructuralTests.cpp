#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Specs for the bounded structural API + dirty-mark on mutation. Drive the C++ DOM methods DIRECTLY (the
// engine interface is the surface — no JS-method registration). Assert OUR bookkeeping: the tree structure
// via the tree reads, the reference-drop, and that EACH mutation fires 005's MarkNodeDirty (recorded by the
// stub layout engine). ZERO GC-internal / refcount / real-layout assertions. Authored, not run this sprint.

namespace
{
using namespace Cathode;

// A recording layout engine: captures every MarkNodeDirty so we can assert the dirty-mark bookkeeping.
class FRecordingLayoutEngine final : public ILayoutEngine
{
public:
	virtual void SetDocument(const ILayoutDocumentSource&) override {}
	virtual void MarkNodeDirty(int32 NodeId, EDirtyKind Kind) override { DirtyMarks.Add(TPair<int32, EDirtyKind>(NodeId, Kind)); }
	virtual FLayoutResult Relayout(int32) override { return FLayoutResult(); }
	TArray<TPair<int32, EDirtyKind>> DirtyMarks;
};

FDomNodeId IdOf(FDomDocument& Doc, const FJsValue& Wrapper)
{
	const FDomNode* Node = Doc.NodeFromWrapper(Wrapper);
	return Node ? Node->GetNodeId() : INDEX_NONE;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomStructureTest,
	"Cathode.Dom.Structure.AppendInsertRemove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomStructureTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FRecordingLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);

	FJsValue ParentW = Doc->CreateElement(TEXT("div"));
	FJsValue AW = Doc->CreateElement(TEXT("a"));
	FJsValue BW = Doc->CreateElement(TEXT("b"));
	FJsValue CW = Doc->CreateElement(TEXT("c"));
	FDomNode* Parent = Doc->NodeFromWrapper(ParentW);
	const FDomNodeId ParentId = IdOf(*Doc, ParentW);
	const FDomNodeId AId = IdOf(*Doc, AW);
	const FDomNodeId BId = IdOf(*Doc, BW);
	const FDomNodeId CId = IdOf(*Doc, CW);

	// append a, b -> [a, b]
	Parent->AppendChild(AW);
	Parent->AppendChild(BW);
	{
		TArray<FDomNodeId> Kids;
		Parent->GetChildNodes(Kids);
		TestEqual(TEXT("two children after append"), Kids.Num(), 2);
		TestTrue(TEXT("order [a, b]"), Kids.Num() == 2 && Kids[0] == AId && Kids[1] == BId);
		TestEqual(TEXT("firstChild is a"), Parent->GetFirstChild(), AId);
		TestEqual(TEXT("a.nextSibling is b"), Doc->FindNode(AId)->GetNextSibling(), BId);
		TestEqual(TEXT("b.nextSibling is none"), Doc->FindNode(BId)->GetNextSibling(), INDEX_NONE);
		TestEqual(TEXT("a.parentNode is the parent"), Doc->FindNode(AId)->GetParentNode(), ParentId);
	}

	// insertBefore c before b -> [a, c, b]
	Parent->InsertBefore(CW, BW);
	{
		TArray<FDomNodeId> Kids;
		Parent->GetChildNodes(Kids);
		TestTrue(TEXT("order [a, c, b] after insertBefore"),
			Kids.Num() == 3 && Kids[0] == AId && Kids[1] == CId && Kids[2] == BId);
	}

	// removeChild a -> [c, b]; a is detached (reference-drop)
	Parent->RemoveChild(AW);
	{
		TArray<FDomNodeId> Kids;
		Parent->GetChildNodes(Kids);
		TestTrue(TEXT("order [c, b] after remove"), Kids.Num() == 2 && Kids[0] == CId && Kids[1] == BId);
		TestEqual(TEXT("removed child is detached (parent INDEX_NONE)"),
			Doc->FindNode(AId)->GetParentNode(), static_cast<FDomNodeId>(INDEX_NONE));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomDirtyMarkTest,
	"Cathode.Dom.Structure.DirtyMarkOnMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomDirtyMarkTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FRecordingLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);

	FJsValue ParentW = Doc->CreateElement(TEXT("div"));
	FJsValue AW = Doc->CreateElement(TEXT("a"));
	FJsValue BW = Doc->CreateElement(TEXT("b"));
	FJsValue CW = Doc->CreateElement(TEXT("c"));
	FDomNode* Parent = Doc->NodeFromWrapper(ParentW);
	const FDomNodeId ParentId = IdOf(*Doc, ParentW);

	// createElement does NOT dirty-mark (a detached node is not a tree mutation).
	TestEqual(TEXT("createElement does not dirty-mark"), Layout.DirtyMarks.Num(), 0);

	// Each mutation of a FRESH child fires exactly one MarkNodeDirty on the parent (the changed subtree).
	Parent->AppendChild(AW);       // +1
	Parent->AppendChild(BW);       // +1
	Parent->InsertBefore(CW, BW);  // +1 (CW fresh -> no reparent)
	Parent->RemoveChild(AW);       // +1

	const int32 Count = Layout.DirtyMarks.Num();
	TestEqual(TEXT("each of the four mutations fired exactly one dirty-mark"), Count, 4);

	bool bAllParentStructure = true;
	for (const TPair<int32, EDirtyKind>& Mark : Layout.DirtyMarks)
	{
		bAllParentStructure &= (Mark.Key == ParentId && Mark.Value == EDirtyKind::Structure);
	}
	TestTrue(TEXT("every dirty-mark targets the parent with EDirtyKind::Structure"), bAllParentStructure);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomReparentTest,
	"Cathode.Dom.Structure.Reparent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomReparentTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FRecordingLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);

	FJsValue AW = Doc->CreateElement(TEXT("a"));
	FJsValue BW = Doc->CreateElement(TEXT("b"));
	FJsValue XW = Doc->CreateElement(TEXT("x"));
	FDomNode* A = Doc->NodeFromWrapper(AW);
	FDomNode* B = Doc->NodeFromWrapper(BW);
	const FDomNodeId AId = IdOf(*Doc, AW);
	const FDomNodeId BId = IdOf(*Doc, BW);
	const FDomNodeId XId = IdOf(*Doc, XW);

	A->AppendChild(XW); // X under A
	const int32 Before = Layout.DirtyMarks.Num();

	B->AppendChild(XW); // REPARENT X from A to B (the one checked=0 path: no-double-hold + both-subtrees dirty)

	// Structure: X left A, entered B, parentNode updated.
	{
		TArray<FDomNodeId> AKids;
		A->GetChildNodes(AKids);
		TestEqual(TEXT("X left A's children (no double-hold)"), AKids.Num(), 0);
		TArray<FDomNodeId> BKids;
		B->GetChildNodes(BKids);
		TestTrue(TEXT("X entered B's children"), BKids.Num() == 1 && BKids[0] == XId);
		TestEqual(TEXT("X.parentNode is now B"), Doc->FindNode(XId)->GetParentNode(), BId);
	}

	// BOTH subtrees received an (id, Structure) dirty-mark from THIS reparent (the both-subtrees claim).
	bool bMarkedA = false;
	bool bMarkedB = false;
	for (int32 Index = Before; Index < Layout.DirtyMarks.Num(); ++Index)
	{
		const TPair<int32, EDirtyKind>& Mark = Layout.DirtyMarks[Index];
		bMarkedA |= (Mark.Key == AId && Mark.Value == EDirtyKind::Structure);
		bMarkedB |= (Mark.Key == BId && Mark.Value == EDirtyKind::Structure);
	}
	TestEqual(TEXT("reparent fired exactly two dirty-marks"), Layout.DirtyMarks.Num() - Before, 2);
	TestTrue(TEXT("old parent A was dirty-marked"), bMarkedA);
	TestTrue(TEXT("new parent B was dirty-marked"), bMarkedB);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
