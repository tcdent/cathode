#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// getComputedStyle returns litehtml's REAL resolved values (QueryComputedValue); an
// empty value is a DEFECT. Each of the 17 litehtml-computed props carries its OWN
// DISCRIMINATING specified!=computed fixture (so a wrong-source/echo read fails, not just the stub); opacity reads its
// OWN-INLINE value (CathodeComputed); font-family is RELATIONAL-TO-CAPTURE (a unique controlled face
// round-trips input->Container-capture->read-back, env-independent -- no system font, the stub measurer does no matching).
// Drives the REAL litehtml engine over a real FDomDocument; getComputedStyle is CathodeDom-side over the
// engine-free FResolvedStyle (FString) seam.

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
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-computedstyle-test>", JS_EVAL_TYPE_GLOBAL);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomComputedStyleTest,
	"Cathode.Dom.Style.ComputedStyleRealValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomComputedStyleTest::RunTest(const FString& Parameters)
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

	auto MakeEl = [&](FDomNode* Parent, const TCHAR* Style) -> FJsValue
	{
		FJsValue W = Doc->CreateElement(TEXT("div"));
		Doc->NodeFromWrapper(W)->SetAttribute(TEXT("style"), Style);
		Parent->AppendChild(W);
		return W;
	};

	// A 200x200 container (font-size:16px so the em/unitless font fixtures are deterministic). Separate child elements
	// per prop-group so box-model props don't perturb each other.
	FDomNode* Cont = Doc->NodeFromWrapper(MakeEl(Root, TEXT("width:200px;height:200px;font-size:16px")));
	const FJsValue A = MakeEl(Cont, TEXT("width:50%;height:50%"));                                                   // box + display/position/top/left
	const FJsValue B = MakeEl(Cont, TEXT("margin:7px;padding:7px;border:2px solid black"));                          // box-model edges (css() absolute)
	const FJsValue C = MakeEl(Cont, TEXT("color:#ff0000;background-color:#00ff00;text-align:center;visibility:hidden;opacity:0.5")); // color/enum/opacity
	const FJsValue D = MakeEl(Cont, TEXT("font-size:1.5em;line-height:1.5;font-weight:bold;font-family:cathodeprobeface")); // font

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__a", JS_DupValue(Context, A.GetValue()));
	JS_SetPropertyStr(Context, Global, "__b", JS_DupValue(Context, B.GetValue()));
	JS_SetPropertyStr(Context, Global, "__c", JS_DupValue(Context, C.GetValue()));
	JS_SetPropertyStr(Context, Global, "__d", JS_DupValue(Context, D.GetValue()));
	JS_FreeValue(Context, Global);

	Doc->Relayout(800); // populate each node's FResolvedStyle from the REAL QueryComputedValue

	auto Gcs = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// BOX-MODEL used px from get_placement -- 50% in a 200px container -> '100px' (proves box-read, not echoed '50%').
	TestTrue(TEXT("width: 50% of 200px -> '100px' (used px, not echo '50%')"), Gcs("getComputedStyle(__a)['width'] === '100px'"));
	TestTrue(TEXT("height: 50% of 200px -> '100px'"), Gcs("getComputedStyle(__a)['height'] === '100px'"));
	// top/left/position on a STATIC element -> the missing->'auto' / 'static' COMPUTED transforms (not echoes).
	TestTrue(TEXT("top: unset on static -> 'auto'"), Gcs("getComputedStyle(__a)['top'] === 'auto'"));
	TestTrue(TEXT("left: unset on static -> 'auto'"), Gcs("getComputedStyle(__a)['left'] === 'auto'"));
	TestTrue(TEXT("position: unset -> 'static' (litehtml computed keyword)"), Gcs("getComputedStyle(__a)['position'] === 'static'"));
	TestTrue(TEXT("display: div -> 'block' (computed keyword)"), Gcs("getComputedStyle(__a)['display'] === 'block'"));
	// BOX-MODEL EDGES via css() -- ABSOLUTE fixture exact-value (catches stub '' AND a wrong-read).
	TestTrue(TEXT("margin: 7px -> '7px' (css() exact-value)"), Gcs("getComputedStyle(__b)['margin'] === '7px'"));
	TestTrue(TEXT("padding: 7px -> '7px'"), Gcs("getComputedStyle(__b)['padding'] === '7px'"));
	TestTrue(TEXT("border-width: 2px solid -> '2px'"), Gcs("getComputedStyle(__b)['border-width'] === '2px'"));
	// COLOR -> rgb() comma-form (reformat, not echo '#ff0000').
	TestTrue(TEXT("color: #ff0000 -> 'rgb(255,0,0)' (reformat)"), Gcs("getComputedStyle(__c)['color'] === 'rgb(255,0,0)'"));
	TestTrue(TEXT("background-color: #00ff00 -> 'rgb(0,255,0)'"), Gcs("getComputedStyle(__c)['background-color'] === 'rgb(0,255,0)'"));
	// ENUMS -> litehtml's computed keyword (not the echoed attr).
	TestTrue(TEXT("text-align: center -> 'center'"), Gcs("getComputedStyle(__c)['text-align'] === 'center'"));
	TestTrue(TEXT("visibility: hidden -> 'hidden'"), Gcs("getComputedStyle(__c)['visibility'] === 'hidden'"));
	// OPACITY -> '0.5' (CathodeComputed parses __c's OWN opacity:0.5; an unimplemented read returns '' here and FAILS).
	TestTrue(TEXT("opacity: 0.5 -> '0.5' (CathodeComputed own-inline; 014 slice 2 flip from '')"), Gcs("getComputedStyle(__c)['opacity'] === '0.5'"));
	// FONT -> resolved px / numeric; font-family relational-to-capture (round-trip, env-independent).
	TestTrue(TEXT("font-size: 1.5em of 16px -> '24px' (resolved px)"), Gcs("getComputedStyle(__d)['font-size'] === '24px'"));
	TestTrue(TEXT("line-height: 1.5 unitless * 24px -> '36px' (resolved px, its OWN fixture)"), Gcs("getComputedStyle(__d)['line-height'] === '36px'"));
	TestTrue(TEXT("font-weight: bold -> '700' (resolved numeric, not echo 'bold')"), Gcs("getComputedStyle(__d)['font-weight'] === '700'"));
	TestTrue(TEXT("font-family: 'cathodeprobeface' -> the captured face (input->capture->read-back round-trip)"), Gcs("getComputedStyle(__d)['font-family'] === 'cathodeprobeface'"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
