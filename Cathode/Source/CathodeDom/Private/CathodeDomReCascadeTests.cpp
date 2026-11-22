#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// INLINE RE-CASCADE: an element.style write -> getComputedStyle reflects the mutation, via the LAZY
// flush-on-read (getComputedStyle relayout-if-dirty, reusing the Controller's full relayout). Three discriminating cases:
//  (1) FLUSH on a LAYOUT-COMPUTED prop (width used-px): getComputedStyle reads LastLayout (NOT the style attr), so a
//      write of a DIFFERENT width (Y != the pre-write X) only shows up if the read FLUSHED -- a non-flushing read returns
//      the stale X and FAILS. (Catches a missing flush, not a no-op.)
//  (2) b2 INHERITED: a PARENT font-size write -> the child's getComputedStyle (font-size:em) re-cascades. A node-only
//      re-cascade would leave the child stale; the full re-cascade reaches descendants.
//  (3) FULL-DOC-CONTEXT (the regression tripwire): a doc-CSS '.anc .target' with .anc ABOVE a (hypothetical) subtree
//      boundary, target nested inside; a dirtying inline write within -> getComputedStyle(target).width STILL '50px'
//      (the ancestor rule applies after the re-cascade). PASSES via the full relayout today; would FAIL if the subtree
//      path (cascade-unsafe, subtree-in-isolation) were ever activated -- the tripwire for that future change.
// Write + flush are CathodeDom-side (via ILayoutEngine MarkNodeDirty/Relayout); the re-cascade is CathodeLayout-side.

namespace
{
using namespace Cathode;

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

JSValue Eval(JSContext* Context, const char* Source)
{
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-recascade-test>", JS_EVAL_TYPE_GLOBAL);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomReCascadeTest,
	"Cathode.Dom.Style.InlineReCascade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomReCascadeTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FScopedTestRuntime Runtime;
	if (!Runtime.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*Runtime);
	FStubMeasurer Measurer;
	TUniquePtr<ILayoutEngine> Engine = CreateLiteHtmlLayoutEngine(Measurer);
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*Runtime, *Registry, *Engine);
	JSContext* Context = static_cast<JSContext*>(Runtime->GetNativeContextHandle());
	FDomNode* Root = Doc->FindNode(Doc->GetRootNode());

	auto MakeEl = [&](FDomNode* Parent, const TCHAR* Style, const TCHAR* Class) -> FJsValue
	{
		FJsValue W = Doc->CreateElement(TEXT("div"));
		FDomNode* N = Doc->NodeFromWrapper(W);
		if (Style && *Style) { N->SetAttribute(TEXT("style"), Style); }
		if (Class && *Class) { N->SetAttribute(TEXT("class"), Class); }
		Parent->AppendChild(W);
		return W;
	};

	// container: width 200px (for the 50% child) + font-size 16px (for the em child / inheritance case).
	const FJsValue ParentW = MakeEl(Root, TEXT("width:200px;font-size:16px"), nullptr);
	FDomNode* Parent = Doc->NodeFromWrapper(ParentW);
	const FJsValue C1 = MakeEl(Parent, TEXT("width:50%"), nullptr);       // case 1: width 100px -> write 80px
	const FJsValue C2 = MakeEl(Parent, TEXT("font-size:1.5em"), nullptr); // case 2: 24px -> parent write -> 30px
	// case 3: a doc-CSS descendant rule + a nested target (anc -> mid -> target), so 'anc' is above a hypothetical boundary.
	FJsValue StyleW = Doc->CreateElement(TEXT("style"));
	Doc->NodeFromWrapper(StyleW)->SetTextContent(TEXT(".anc .target { width:50px; }"));
	Parent->AppendChild(StyleW);
	FDomNode* Anc = Doc->NodeFromWrapper(MakeEl(Parent, TEXT("width:200px"), TEXT("anc")));
	FDomNode* Mid = Doc->NodeFromWrapper(MakeEl(Anc, nullptr, TEXT("mid")));
	const FJsValue Target = MakeEl(Mid, nullptr, TEXT("target"));

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__c1", JS_DupValue(Context, C1.GetValue()));
	JS_SetPropertyStr(Context, Global, "__c2", JS_DupValue(Context, C2.GetValue()));
	JS_SetPropertyStr(Context, Global, "__parent", JS_DupValue(Context, ParentW.GetValue()));
	JS_SetPropertyStr(Context, Global, "__target", JS_DupValue(Context, Target.GetValue()));
	JS_FreeValue(Context, Global);

	Doc->Relayout(800); // the INITIAL layout (sets LastLayoutWidth=800; the flush re-relayouts with it)

	auto Js = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// (1) FLUSH-DISCRIMINATING on width (X != Y): pre-write computed 100px; write 80px; post-write getComputedStyle MUST
	// be 80px (only true if the read flushed the pending write into a relayout; a stale read returns 100px -> FAIL).
	TestTrue(TEXT("(1) pre-write: __c1 width === '100px' (50% of 200, initial cascade)"), Js("getComputedStyle(__c1)['width'] === '100px'"));
	TestTrue(TEXT("(1) FLUSH: write width 80px -> getComputedStyle === '80px' (a non-flushing read returns stale '100px')"),
		Js("(function(){ __c1.style.width='80px'; return getComputedStyle(__c1)['width'] === '80px'; })()"));

	// (2) INHERITED (b2): a PARENT font-size write re-cascades the child's em -> the re-cascade reaches descendants.
	TestTrue(TEXT("(2) pre-write: __c2 font-size === '24px' (1.5em of 16)"), Js("getComputedStyle(__c2)['font-size'] === '24px'"));
	TestTrue(TEXT("(2) INHERITED: parent font-size -> 20px -> child 1.5em re-cascades to '30px' (a node-only re-cascade leaves it stale '24px')"),
		Js("(function(){ __parent.style.fontSize='20px'; return getComputedStyle(__c2)['font-size'] === '30px'; })()"));

	// (3) FULL-DOC-CONTEXT (tripwire): a dirtying inline write within the subtree -> getComputedStyle(.target).width
	// STILL '50px' (the '.anc .target' ancestor rule applies after the re-cascade). Full relayout preserves it; a future
	// subtree-isolation re-cascade would drop '.anc .target' (.anc outside the subtree) -> auto != 50 -> FAIL (the tripwire).
	TestTrue(TEXT("(3) initial: __target width === '50px' (the '.anc .target' doc-CSS rule)"), Js("getComputedStyle(__target)['width'] === '50px'"));
	TestTrue(TEXT("(3) FULL-DOC-CONTEXT: a dirtying inline write within -> __target width STILL '50px' (ancestor rule preserved across the re-cascade)"),
		Js("(function(){ __target.style.color='red'; return getComputedStyle(__target)['width'] === '50px'; })()"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
