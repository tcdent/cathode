#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

// 013 slice 3 — the FORK-2 raw-text escaping exemption + the path-(i) cascade, proven with the SCOPE load-bearing BOTH
// ways on the SAME escaping-requiring content (the slice-1 per-matrix lesson):
//   (b) SCOPE [CathodeDomRawTextScope]: {<style>,<script>} text with '>'/'&' is emitted UN-escaped, while RCDATA
//       (<textarea>/<title>) AND a normal <div> with the SAME '>'/'&' STAY EscapeHtmlText'd. Asserted on the serialized
//       string. An OVER-BROAD un-escape (exemption beyond {style,script}) PASSES the cascade but FAILS this.
//   (a) CASCADE [CathodeDomStyleRuleCascade]: a <style> CHILD-SELECTOR rule ('.p > .c { width:50px }') -> SerializeNode
//       keeps the '>' RAW -> litehtml cascades it -> the .c box width === a direct width:50px control (litehtml's own,
//       relational). WITHOUT the exemption SerializeNode emits '.p &gt; .c' -> invalid selector -> .c unmatched -> auto
//       -> fills the 200px container -> DIVERGES. The '>' round-trip is load-bearing.
// FORK-1: the cascade drives litehtml via CathodeLayout's ENGINE (CreateLiteHtmlLayoutEngine) -- no litehtml type in
// CathodeDom; CathodeDom -> litehtml stays absent (the engine is CathodeLayout-side, FLayoutResult is Core types).

namespace
{
using namespace Cathode;

class FStubLayoutEngine final : public ILayoutEngine
{
public:
	virtual void SetDocument(const ILayoutDocumentSource&) override {}
	virtual void MarkNodeDirty(int32, EDirtyKind) override {}
	virtual FLayoutResult Relayout(int32) override { return FLayoutResult(); }
};

class FStubMeasurer final : public ITextMeasurer
{
public:
	virtual FLayoutFontRef ResolveFont(const FString&, int32, int32, bool) override { return FLayoutFontRef(); }
	virtual float MeasureWidth(const FString&, FLayoutFontRef) override { return 0.0f; }
	virtual void GetFontMetrics(FLayoutFontRef, float& OutAscent, float& OutDescent, float& OutHeight) override
	{
		OutAscent = 12.0f; OutDescent = 4.0f; OutHeight = 16.0f;
	}
};

FDomNode* AddChildWithText(FDomDocument& Doc, FDomNode* Parent, const TCHAR* Tag, const TCHAR* Text)
{
	FJsValue W = Doc.CreateElement(Tag);
	FDomNode* N = Doc.NodeFromWrapper(W);
	N->SetTextContent(Text);
	Parent->AppendChild(W);
	return N;
}
} // namespace

// ===== (b) SCOPE: {style,script} un-escaped, RCDATA + normal STILL escaped — on the SAME '>'/'&' =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomRawTextScopeTest,
	"Cathode.Dom.Serialize.RawTextExemptionScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomRawTextScopeTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FScopedTestRuntime Runtime;
	if (!Runtime.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*Runtime);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*Runtime, *Registry, Layout);
	FDomNode* Root = Doc->FindNode(Doc->GetRootNode());

	AddChildWithText(*Doc, Root, TEXT("style"),    TEXT(".a > .b { color: red; }"));   // raw-text: '>' stays raw
	AddChildWithText(*Doc, Root, TEXT("script"),   TEXT("if (a > b && c) { d(); }"));  // raw-text: '>' '&' stay raw
	AddChildWithText(*Doc, Root, TEXT("textarea"), TEXT("x > y & z"));                 // RCDATA: STILL escaped
	AddChildWithText(*Doc, Root, TEXT("title"),    TEXT("p > q & r"));                 // RCDATA: STILL escaped
	AddChildWithText(*Doc, Root, TEXT("div"),      TEXT("m > n & o"));                 // normal: STILL escaped

	const FString Html = Doc->GetDocumentHtml();

	// (POSITIVE) {style,script} content is UN-escaped — the exemption.
	TestTrue(TEXT("<style> content un-escaped: '.a > .b' raw (NOT '.a &gt; .b')"),
		Html.Contains(TEXT(".a > .b")) && !Html.Contains(TEXT(".a &gt; .b")));
	TestTrue(TEXT("<script> content un-escaped: 'a > b && c' raw (NOT '&gt;' / '&amp;&amp;')"),
		Html.Contains(TEXT("a > b && c")) && !Html.Contains(TEXT("a &gt; b")));

	// (NEGATIVE-SCOPING) RCDATA + normal STILL escaped on the SAME '>'/'&' -> the exemption is SCOPED, not over-broad.
	TestTrue(TEXT("<textarea> (RCDATA) STILL escaped: 'x &gt; y &amp; z' (NOT raw 'x > y & z')"),
		Html.Contains(TEXT("x &gt; y &amp; z")) && !Html.Contains(TEXT("x > y & z")));
	TestTrue(TEXT("<title> (RCDATA) STILL escaped: 'p &gt; q &amp; r' (NOT raw 'p > q & r')"),
		Html.Contains(TEXT("p &gt; q &amp; r")) && !Html.Contains(TEXT("p > q & r")));
	TestTrue(TEXT("<div> (normal) text STILL escaped: 'm &gt; n &amp; o' (NOT raw 'm > n & o')"),
		Html.Contains(TEXT("m &gt; n &amp; o")) && !Html.Contains(TEXT("m > n & o")));

	return true;
}

// ===== (a) CASCADE: a <style> child-selector rule round-trips RAW through the exemption + litehtml cascades it =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomStyleRuleCascadeTest,
	"Cathode.Dom.Style.StyleRuleCascade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomStyleRuleCascadeTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FScopedTestRuntime Runtime;
	if (!Runtime.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*Runtime);
	FStubMeasurer Measurer;
	TUniquePtr<ILayoutEngine> Engine = CreateLiteHtmlLayoutEngine(Measurer);
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*Runtime, *Registry, *Engine);
	FDomNode* Root = Doc->FindNode(Doc->GetRootNode());

	// container(200) > [ <style '.p > .c { w:50; h:30 }'>, div.p > div.c (TARGET), div control(w:50,h:30) ]
	FJsValue ContW = Doc->CreateElement(TEXT("div"));
	FDomNode* Cont = Doc->NodeFromWrapper(ContW);
	Cont->SetAttribute(TEXT("style"), TEXT("width:200px"));
	Root->AppendChild(ContW);

	FDomNode* Style = AddChildWithText(*Doc, Cont, TEXT("style"), TEXT(".p > .c { width:50px; height:30px; }"));
	(void)Style;

	FJsValue PW = Doc->CreateElement(TEXT("div"));
	FDomNode* P = Doc->NodeFromWrapper(PW);
	P->SetAttribute(TEXT("class"), TEXT("p"));
	Cont->AppendChild(PW);

	FJsValue CW = Doc->CreateElement(TEXT("div"));
	FDomNode* C = Doc->NodeFromWrapper(CW);
	C->SetAttribute(TEXT("class"), TEXT("c"));
	P->AppendChild(CW);

	FJsValue CtrlW = Doc->CreateElement(TEXT("div"));
	FDomNode* Ctrl = Doc->NodeFromWrapper(CtrlW);
	Ctrl->SetAttribute(TEXT("style"), TEXT("width:50px;height:30px"));
	Cont->AppendChild(CtrlW);

	const int32 ContId = Cont->GetNodeId();
	const int32 CId = C->GetNodeId();
	const int32 CtrlId = Ctrl->GetNodeId();

	Engine->SetDocument(*Doc); // FDomDocument IS-A ILayoutDocumentSource -> GetDocumentHtml() (SerializeNode + exemption)
	const FLayoutResult Result = Engine->Relayout(800);

	TMap<int32, FBox2f> Boxes;
	for (const FLayoutNodeGeometry& G : Result.Geometry) { Boxes.Add(G.NodeId, G.Box); }
	const FBox2f* ContBox = Boxes.Find(ContId);
	const FBox2f* CBox = Boxes.Find(CId);    // .c -- width via the '.p > .c' rule
	const FBox2f* CtrlBox = Boxes.Find(CtrlId);
	if (ContBox == nullptr || CBox == nullptr || CtrlBox == nullptr)
	{
		AddError(FString::Printf(TEXT("litehtml produced no box for one of the nodes (have %d) -- render/correlation did not resolve"), Result.Geometry.Num()));
		return false;
	}

	const float ContWidth = ContBox->GetSize().X;
	const float CWidth = CBox->GetSize().X;
	const float CtrlWidth = CtrlBox->GetSize().X;

	// DISCRIMINATING precondition: control 50 distinctly < container 200, so an UNMATCHED .c (-> auto -> ~container) diverges.
	TestTrue(FString::Printf(TEXT("control width:50px is non-default (%.1f distinctly < container %.1f)"), CtrlWidth, ContWidth),
		CtrlWidth > 0.0f && CtrlWidth < ContWidth - 1.0f);

	// LOAD-BEARING: the '.p > .c' child-selector cascaded -> .c width === control 50. WITHOUT the raw-text exemption,
	// SerializeNode emits '.p &gt; .c' -> invalid selector -> .c unmatched -> auto -> ~container(200) != 50 -> diverges.
	const bool bCascaded = FMath::IsNearlyEqual(CWidth, CtrlWidth, 0.5f);
	AddInfo(FString::Printf(TEXT("[CATHODE-S3-CASCADE] .c(via '.p > .c' <style>)=%.1f control=%.1f container=%.1f -> %s"),
		CWidth, CtrlWidth, ContWidth, bCascaded ? TEXT("CASCADED (raw '>' round-tripped + litehtml applied)") : TEXT("DIVERGED (exemption/cascade FAILED)")));
	TestTrue(TEXT("the <style> '.p > .c' child-selector rule CASCADED through the raw-text exemption -> .c width === control"), bCascaded);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
