#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// 014 slice 2 — opacity VALUE SIDE (CathodeComputed). litehtml does NOT parse opacity, so getComputedStyle sources it
// Cathode-side by parsing the element's OWN inline value ON-DEMAND in QueryComputedValue. CSS computed-value rule =
// clamp(<number> | <percentage>, [0,1]) serialized as a plain number; opacity does NOT inherit + initial = 1 -> UNSET = '1'.
// NO rendering (the Blend2D group-compositing pipeline is integration-deferred). Proven on the 013 harness (REAL litehtml
// engine + stub measurer + getComputedStyle). DISCRIMINATING, RELATIONAL:
//  - clamp/normalize (specified != computed -> parse-traced, not an echo): 1.5->'1', -0.2->'0', .5->'0.5', 90%->'0.9'.
//  - unset -> '1' (the CSS initial; opacity does NOT inherit).
//  - NON-INHERITANCE non-vacuous (SAME parent+child): (a) parent opacity:0.5 -> '0.5' (the parse WORKS) AND (b) child with
//    NO own opacity -> '1' (the initial, NOT the parent's 0.5 — opacity is not inherited). (a) is what makes (b) bite.
//  - CASCADED-OPACITY TRIPWIRE (own-inline-only; NOT a today-bite): a <style> opacity rule on a no-inline element reads '1'
//    BY CONSTRUCTION (litehtml discards opacity + the parse reads El's OWN attr only) -- it CANNOT fail today. It is non-
//    vacuous ONLY because the inline cases above show the parse DOES read inline values, so '1' here means own-inline-only,
//    not a broken parse. It bites a FUTURE wrongly-added cascade/<style> read (the honest analog of letter-spacing's
//    deferred-inheritance tripwire). The cascaded/stylesheet opacity is deferred with the rasterizer build-how.

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
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-opacity-test>", JS_EVAL_TYPE_GLOBAL);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomOpacityTest,
	"Cathode.Dom.Style.Opacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomOpacityTest::RunTest(const FString& Parameters)
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

	const FJsValue ElHigh  = MakeEl(Root, TEXT("opacity:1.5"),  nullptr); // clamp high -> '1'
	const FJsValue ElLow   = MakeEl(Root, TEXT("opacity:-0.2"), nullptr); // clamp low  -> '0'
	const FJsValue ElNorm  = MakeEl(Root, TEXT("opacity:.5"),   nullptr); // normalize  -> '0.5'
	const FJsValue ElPct   = MakeEl(Root, TEXT("opacity:90%"),  nullptr); // percentage -> '0.9'
	const FJsValue ElUnset = MakeEl(Root, TEXT("color:red"),    nullptr); // unset -> '1' (initial)
	const FJsValue Parent  = MakeEl(Root, TEXT("opacity:0.5"),  nullptr); // inheritance: parent HAS its own
	const FJsValue Child   = MakeEl(Doc->NodeFromWrapper(Parent), nullptr, nullptr); // inheritance: child has NONE

	// CASCADED-OPACITY TRIPWIRE: a <style> rule litehtml discards (opacity is non-parse) + an element matching it with NO
	// inline opacity. own-inline-only -> getComputedStyle reads '1' (the initial), NOT the cascaded 0.3.
	FJsValue StyleW = Doc->CreateElement(TEXT("style"));
	Doc->NodeFromWrapper(StyleW)->SetTextContent(TEXT(".faded { opacity: 0.3; }"));
	Root->AppendChild(StyleW);
	const FJsValue ElCascaded = MakeEl(Root, nullptr, TEXT("faded")); // class match, NO inline opacity -> '1' (tripwire)

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__high",     JS_DupValue(Context, ElHigh.GetValue()));
	JS_SetPropertyStr(Context, Global, "__low",      JS_DupValue(Context, ElLow.GetValue()));
	JS_SetPropertyStr(Context, Global, "__norm",     JS_DupValue(Context, ElNorm.GetValue()));
	JS_SetPropertyStr(Context, Global, "__pct",      JS_DupValue(Context, ElPct.GetValue()));
	JS_SetPropertyStr(Context, Global, "__unset",    JS_DupValue(Context, ElUnset.GetValue()));
	JS_SetPropertyStr(Context, Global, "__parent",   JS_DupValue(Context, Parent.GetValue()));
	JS_SetPropertyStr(Context, Global, "__child",    JS_DupValue(Context, Child.GetValue()));
	JS_SetPropertyStr(Context, Global, "__cascaded", JS_DupValue(Context, ElCascaded.GetValue()));
	JS_FreeValue(Context, Global);

	Doc->Relayout(800); // populate FResolvedStyle (QueryComputedValue parses opacity per element)

	auto Js = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// CLAMP / NORMALIZE — specified != computed (parse-traced, not an echo of the inline string).
	TestTrue(TEXT("clamp high: opacity:1.5 -> '1'"),    Js("getComputedStyle(__high)['opacity'] === '1'"));
	TestTrue(TEXT("clamp low: opacity:-0.2 -> '0'"),    Js("getComputedStyle(__low)['opacity'] === '0'"));
	TestTrue(TEXT("normalize: opacity:.5 -> '0.5'"),    Js("getComputedStyle(__norm)['opacity'] === '0.5'"));
	TestTrue(TEXT("percentage: opacity:90% -> '0.9'"),  Js("getComputedStyle(__pct)['opacity'] === '0.9'"));

	// UNSET -> '1' (the CSS initial; opacity does NOT inherit).
	TestTrue(TEXT("unset: opacity -> '1' (the CSS initial value)"), Js("getComputedStyle(__unset)['opacity'] === '1'"));

	// NON-INHERITANCE non-vacuous, SAME setup: (a) parent own -> '0.5' (parse works) THEN (b) child no-own -> '1' (initial, NOT inherited).
	TestTrue(TEXT("inheritance (a): parent opacity:0.5 -> '0.5' (own value resolves — the parse WORKS)"),
		Js("getComputedStyle(__parent)['opacity'] === '0.5'"));
	TestTrue(TEXT("inheritance (b): child no-own -> '1' (the initial, NOT the parent's 0.5 — opacity is NOT inherited; bites GIVEN (a))"),
		Js("getComputedStyle(__child)['opacity'] === '1'"));

	// CASCADED-OPACITY TRIPWIRE (own-inline-only; '1' by construction today): a <style> opacity:0.3 on a no-inline element
	// reads '1', NOT the cascaded 0.3. Non-vacuous via the inline cases above (the parse DOES read inline); bites a future cascade read.
	TestTrue(TEXT("cascaded tripwire: <style> opacity:0.3 on a no-inline element -> '1' (own-inline-only, NOT the cascaded 0.3)"),
		Js("getComputedStyle(__cascaded)['opacity'] === '1'"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
