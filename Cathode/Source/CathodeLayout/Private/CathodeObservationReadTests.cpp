#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeLayout.h"
#include "CathodeObservationRead.h"

#include "Misc/AutomationTest.h"

// Specs for the BESPOKE observation read-out — the geometry walk + the bounded resolved-style mapping —
// all litehtml-FREE. The walk runs over a scripted neutral tree; the per-node box + per-property value are
// supplied by scripted/opaque callbacks (standing in for the vendor edge). We assert OUR traversal +
// assembly + bounded property set + value->slot translation, NEVER litehtml's computed boxes or cascade
// (the layout-output tautology). Authored, not run this sprint.

namespace
{
using namespace Cathode;

// A scripted laid-out tree with children:  0 -> [1, 2],  1 -> [3].  (Boxes/styles are opaque, supplied by
// the test callbacks — the walk neither computes nor asserts them as truth.)
class FStubGeometrySource final : public ILayoutDocumentSource
{
public:
	int32 Root = 0;
	TMap<int32, TArray<int32>> Children;

	virtual int32 GetRootNode() const override { return Root; }
	virtual int32 GetParentNode(int32) const override { return INDEX_NONE; }
	virtual void GetChildNodes(int32 NodeId, TArray<int32>& Out) const override
	{
		if (const TArray<int32>* C = Children.Find(NodeId))
		{
			Out = *C;
		}
	}
	virtual bool IsLayoutBoundary(int32) const override { return false; }
	virtual FString GetDocumentHtml() const override { return FString(); }
	virtual FString GetSubtreeHtml(int32) const override { return FString(); }
	virtual FString GetDocumentCss() const override { return FString(); }
};

FStubGeometrySource MakeTree()
{
	FStubGeometrySource Tree;
	Tree.Root = 0;
	Tree.Children.Add(0, { 1, 2 });
	Tree.Children.Add(1, { 3 });
	return Tree;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeLayoutGeometryWalkTest,
	"Cathode.Layout.Observation.GeometryWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutGeometryWalkTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;
	const FStubGeometrySource Tree = MakeTree();

	// A scripted opaque box per node (id*10 origin) — stands in for the vendor-edge box query. The walk
	// must carry exactly this through; it never computes a box itself.
	auto GetBox = [](int32 NodeId)
	{
		const float O = static_cast<float>(NodeId * 10);
		return FBox2f(FVector2f(O, O), FVector2f(O + 5, O + 5));
	};
	// A scripted bounded style per node — one contract property carrying the node id, to prove per-node
	// assembly (the real engine builds this via MapResolvedStyle; here it's opaque).
	auto GetStyle = [](int32 NodeId)
	{
		FResolvedStyle S;
		S.Properties.Add(TEXT("width"), FString::FromInt(NodeId));
		return S;
	};

	TArray<FLayoutNodeGeometry> Out;
	WalkGeometry(Tree, GetBox, GetStyle, Out);

	// The walk visits EVERY node (0,1,2,3) exactly once and assembles a per-node record.
	TestEqual(TEXT("one geometry record per node"), Out.Num(), 4);

	TSet<int32> Visited;
	for (const FLayoutNodeGeometry& G : Out)
	{
		Visited.Add(G.NodeId);
		// Each record carries the callback-supplied box + style for ITS node (our assembly, not vendor truth).
		TestTrue(TEXT("box matches the scripted query for this node"), G.Box == GetBox(G.NodeId));
		const FString* Width = G.Style.Properties.Find(TEXT("width"));
		TestTrue(TEXT("style carries this node's scripted slot"), Width != nullptr && *Width == FString::FromInt(G.NodeId));
	}
	TestTrue(TEXT("every node visited"), Visited.Contains(0) && Visited.Contains(1) && Visited.Contains(2) && Visited.Contains(3));

	// Pre-order: a parent precedes its descendants.
	auto IndexOf = [&Out](int32 NodeId)
	{
		for (int32 i = 0; i < Out.Num(); ++i) { if (Out[i].NodeId == NodeId) { return i; } }
		return INDEX_NONE;
	};
	TestTrue(TEXT("root precedes its children"), IndexOf(0) < IndexOf(1) && IndexOf(0) < IndexOf(2));
	TestTrue(TEXT("a parent precedes its descendant"), IndexOf(1) < IndexOf(3));

	// An empty tree yields no records (root == INDEX_NONE short-circuits).
	{
		FStubGeometrySource Empty;
		Empty.Root = INDEX_NONE;
		TArray<FLayoutNodeGeometry> None;
		WalkGeometry(Empty, GetBox, GetStyle, None);
		TestEqual(TEXT("empty tree -> no records"), None.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeLayoutBoundedStyleTest,
	"Cathode.Layout.Observation.BoundedStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutBoundedStyleTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// The vendor edge: a computed-value lookup that records WHICH properties were queried. It would answer
	// for ANY property (the full cascade), so what bounds the read is OURS, not the source.
	TArray<FString> Queried;
	auto GetValue = [&Queried](const FString& Property)
	{
		Queried.Add(Property);
		return FString::Printf(TEXT("computed(%s)"), *Property); // opaque vendor value -> its slot
	};

	const FResolvedStyle Style = MapResolvedStyle(GetValue);
	const TArray<FString>& Contract = ContractStyleProperties();

	// BOUNDED: exactly the contract property set is mapped — not more (no full-CSSOM dump), not less.
	TestEqual(TEXT("mapped property count == the contract set"), Style.Properties.Num(), Contract.Num());
	TestEqual(TEXT("only contract properties were queried"), Queried.Num(), Contract.Num());
	for (const FString& Property : Contract)
	{
		// value->slot translation: each contract property holds the value the vendor edge returned for it.
		const FString* Value = Style.Properties.Find(Property);
		TestTrue(TEXT("contract property present in its slot"), Value != nullptr);
		if (Value != nullptr)
		{
			TestEqual(TEXT("slot holds this property's computed value"), *Value, FString::Printf(TEXT("computed(%s)"), *Property));
		}
		TestTrue(TEXT("the contract property was the thing queried"), Queried.Contains(Property));
	}

	// Nothing OUTSIDE the contract leaked in (a property the source could answer is never queried/stored).
	TestFalse(TEXT("a non-contract property is not in the read"), Style.Properties.Contains(TEXT("z-index")));
	TestFalse(TEXT("a non-contract property was never queried"), Queried.Contains(TEXT("z-index")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
