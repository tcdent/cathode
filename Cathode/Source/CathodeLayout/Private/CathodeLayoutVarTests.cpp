#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeLayout.h"

#include "Misc/AutomationTest.h"

// var() FEASIBILITY proof. Drives the REAL litehtml engine
// (CreateLiteHtmlLayoutEngine) on an inline custom-property + var() and asserts litehtml RESOLVES var(--w) -> the
// custom value, observed through the computed-box read (width:var(--w) -> the box width) — NO
// getComputedStyle/QueryComputedValue and NO <style> ingress; inline --custom in the canonical 'style'
// attribute, driving litehtml's own cascade (incl subst_vars) directly.
//
// ORACLE — RELATIONAL, never a hand-typed pixel: the var()-driven element's width === a sibling's DIRECT width:50px,
// both litehtml's OWN computation. DISCRIMINATING: if litehtml did NOT resolve var() (subst_vars stub/partial, or var()
// left unparsed), 'width:var(--w)' is an invalid length -> the block defaults to auto -> fills the 200px container ->
// the var() width DIVERGES from the 50px control and the equality FAILS. A green that can't tell resolved from
// unresolved proves nothing; the control gap is built to expose a stub.
//
// FIREWALL: this lives CathodeLayout-side (litehtml-private); CathodeDom is not involved -> no CathodeDom->litehtml link.

namespace
{
using namespace Cathode;

// 200px container with two sized block children:
//   #2 = direct  width:50px;height:30px            (CONTROL)
//   #3 = --w:50px; width:var(--w); height:30px     (VAR — width via var(--w))
class FVarHtmlSource final : public ILayoutDocumentSource
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
		return TEXT(
			"<div data-node-id=\"1\" style=\"width:200px\">"
			"<div data-node-id=\"2\" style=\"width:50px;height:30px\"></div>"
			"<div data-node-id=\"3\" style=\"--w:50px;width:var(--w);height:30px\"></div>"
			"</div>");
	}
	virtual FString GetSubtreeHtml(int32) const override { return GetDocumentHtml(); }
	virtual FString GetDocumentCss() const override { return FString(); }
};

class FVarStubMeasurer final : public ITextMeasurer
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
	FCathodeLayoutVarResolutionTest,
	"Cathode.Layout.LiteHtml.VarResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutVarResolutionTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FVarStubMeasurer Measurer;
	TUniquePtr<ILayoutEngine> Engine = CreateLiteHtmlLayoutEngine(Measurer);
	FVarHtmlSource Source;
	Engine->SetDocument(Source);
	const FLayoutResult Result = Engine->Relayout(800);

	TMap<int32, FBox2f> Boxes;
	for (const FLayoutNodeGeometry& G : Result.Geometry) { Boxes.Add(G.NodeId, G.Box); }
	const FBox2f* Container = Boxes.Find(1);
	const FBox2f* Control = Boxes.Find(2); // direct width:50px
	const FBox2f* Var = Boxes.Find(3);     // width:var(--w)
	if (Container == nullptr || Control == nullptr || Var == nullptr)
	{
		AddError(FString::Printf(TEXT("litehtml produced no box for one of the nodes (have %d geometry records) -- the render/correlation did not resolve"), Result.Geometry.Num()));
		return false;
	}

	const float ControlW = Control->GetSize().X;
	const float VarW = Var->GetSize().X;
	const float ContainerW = Container->GetSize().X;

	// DISCRIMINATING PRECONDITION: the control width:50px is NON-default (distinctly < the container). So if var() did NOT
	// resolve, the var() block would default to auto (-> ~container width) and be DISTINGUISHABLE from the control. Without
	// this gap, the equality below couldn't tell resolved from unresolved.
	TestTrue(FString::Printf(TEXT("control width:50px is non-default (%.1f distinctly < container %.1f) -- the gap an unresolved var would expose"), ControlW, ContainerW),
		ControlW > 0.0f && ControlW < ContainerW - 1.0f);

	// RELATIONAL ORACLE: litehtml RESOLVED var(--w) -> 50px, so the var()-driven box width === the direct width:50px
	// control (both litehtml's OWN computation, never hand-typed). If subst_vars is a stub / var() is unresolved, VarW
	// defaults (!= ControlW) and this FAILS -> the tripwire fires AT THE PROOF (empirical wall, not a header guess).
	TestTrue(FString::Printf(TEXT("var(--w) RESOLVED: var-driven width (%.1f) === direct width:50px control (%.1f) -- litehtml's own resolution"), VarW, ControlW),
		FMath::IsNearlyEqual(VarW, ControlW, 0.5f));

	AddInfo(FString::Printf(TEXT("[CATHODE-S2-VAR] control=%.1f var=%.1f container=%.1f -> var() %s"),
		ControlW, VarW, ContainerW, FMath::IsNearlyEqual(VarW, ControlW, 0.5f) ? TEXT("RESOLVED (feasible)") : TEXT("UNRESOLVED (WALL)")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
