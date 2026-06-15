#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeLayout.h"

#include "Misc/AutomationTest.h"

// Sprint 011 [c] target 1 — REAL litehtml layout output (the come-due discharge).
//
// Drives the REAL FLiteHtmlLayoutEngine (CreateLiteHtmlLayoutEngine) on real HTML — NOT the recording/stub
// backend the other CathodeLayout tests use — and asserts against litehtml's OWN computed boxes. The source
// emits HTML carrying the data-node-id correlation handle (the same handle FDomDocument::SerializeNode emits in
// production); the engine maps each NodeId to litehtml's placed element and reads get_placement(). Per the
// oracle, the assertions are RELATIONAL/STRUCTURAL invariants only real layout produces (non-zero box on a
// sized element; child nests in its parent; sibling blocks stack at increasing Y; the parent encloses its
// stacked children) — NEVER a hand-typed pixel. checked = the boxes asserted against litehtml's output.

namespace
{
using namespace Cathode;

// A real document source: nested blocks with explicit inline sizes + the data-node-id handle. Tree: 1 -> [2,3].
class FRealHtmlSource final : public ILayoutDocumentSource
{
public:
	virtual int32 GetRootNode() const override { return 1; }
	virtual int32 GetParentNode(int32 NodeId) const override { return (NodeId == 2 || NodeId == 3) ? 1 : INDEX_NONE; }
	virtual void GetChildNodes(int32 NodeId, TArray<int32>& OutChildren) const override
	{
		if (NodeId == 1) { OutChildren = { 2, 3 }; }
	}
	virtual bool IsLayoutBoundary(int32) const override { return false; }

	virtual FString GetDocumentHtml() const override
	{
		// A 200px container with two sized block children — litehtml computes nesting + vertical stacking.
		return TEXT(
			"<div data-node-id=\"1\" style=\"width:200px\">"
			"<div data-node-id=\"2\" style=\"width:50px;height:30px\"></div>"
			"<div data-node-id=\"3\" style=\"width:60px;height:40px\"></div>"
			"</div>");
	}
	virtual FString GetSubtreeHtml(int32) const override { return GetDocumentHtml(); }
	virtual FString GetDocumentCss() const override { return FString(); }
};

// Minimal text measurer — block layout with explicit sizes barely exercises it (no text content here).
class FStubMeasurer final : public ITextMeasurer
{
public:
	virtual FLayoutFontRef ResolveFont(const FString&, int32, int32, bool) override { return FLayoutFontRef(); }
	virtual float MeasureWidth(const FString&, FLayoutFontRef) override { return 0.0f; }
	virtual void GetFontMetrics(FLayoutFontRef, float& OutAscent, float& OutDescent, float& OutHeight) override
	{
		OutAscent = 12.0f;
		OutDescent = 4.0f;
		OutHeight = 16.0f;
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeLayoutLiteHtmlBoxTest,
	"Cathode.Layout.LiteHtml.ComputedBoxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutLiteHtmlBoxTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FStubMeasurer Measurer;
	TUniquePtr<ILayoutEngine> Engine = CreateLiteHtmlLayoutEngine(Measurer);
	FRealHtmlSource Source;
	Engine->SetDocument(Source);

	// Drive the REAL engine: createFromString -> render -> the data-node-id map -> get_placement per NodeId.
	const FLayoutResult Result = Engine->Relayout(800);

	TMap<int32, FBox2f> Boxes;
	for (const FLayoutNodeGeometry& Geometry : Result.Geometry)
	{
		Boxes.Add(Geometry.NodeId, Geometry.Box);
	}
	const FBox2f* Container = Boxes.Find(1);
	const FBox2f* First = Boxes.Find(2);
	const FBox2f* Second = Boxes.Find(3);
	if (Container == nullptr || First == nullptr || Second == nullptr)
	{
		AddError(FString::Printf(TEXT("litehtml produced no box for one of the nodes (have %d geometry records) -- "
			"the data-node-id correlation or the real render did not resolve"), Result.Geometry.Num()));
		return false;
	}

	// ORACLE: relational/structural invariants against litehtml's OWN computed boxes (no hand-typed pixels).
	// (1) a sized element has a non-zero box (litehtml actually laid it out, not the stub's ForceInit).
	TestTrue(TEXT("sized child #2 has a non-zero computed box"), First->GetSize().X > 0.0f && First->GetSize().Y > 0.0f);
	TestTrue(TEXT("sized child #3 has a non-zero computed box"), Second->GetSize().X > 0.0f && Second->GetSize().Y > 0.0f);
	// (2) each child NESTS within its parent's x-extent.
	TestTrue(TEXT("child #2 nests within the parent's x-extent"), First->Min.X >= Container->Min.X && First->Max.X <= Container->Max.X);
	TestTrue(TEXT("child #3 nests within the parent's x-extent"), Second->Min.X >= Container->Min.X && Second->Max.X <= Container->Max.X);
	// (3) sibling blocks STACK at increasing Y (#3 starts at/after #2's bottom).
	TestTrue(TEXT("sibling blocks stack at increasing Y"), Second->Min.Y >= First->Max.Y);
	// (4) the parent ENCLOSES its stacked children's height.
	TestTrue(TEXT("the parent encloses its stacked children"), Container->GetSize().Y >= First->GetSize().Y + Second->GetSize().Y);

	// checked = boxes asserted against litehtml's own output.
	AddInfo(FString::Printf(TEXT("[CATHODE-T1] %d boxes asserted against litehtml's computed output"), Boxes.Num()));
	TestEqual(TEXT("three boxes were read from litehtml's computed layout"), Boxes.Num(), 3);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
