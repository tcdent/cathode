#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// letter-spacing VALUE SIDE (CathodeComputed). litehtml does NOT parse letter-spacing, so getComputedStyle
// sources it Cathode-side by parsing the element's OWN inline value ON-DEMAND in QueryComputedValue (own-element-only;
// inheritance + rendering are NOT implemented). NO CathodeText/shaping, NO layout
// effect. Proven on the REAL litehtml engine + stub measurer + getComputedStyle (as CathodeDomComputedStyleTests).
// DISCRIMINATING, RELATIONAL:
//  - px own value: 'letter-spacing:2px' -> '2px'.
//  - em (LOAD-BEARING, not an echo): own font-size 16px + '0.1em' -> '1.6px'. specified '0.1em' != computed '1.6px', so
//    the value MUST trace to the Cathode-side parse (em x font-size), not the inline string. font-size is set ON THE
//    ELEMENT (deterministic — NOT relying on font-size inheritance, a separate concern).
//  - NON-VACUOUS unimplemented-inheritance (SAME parent+child setup): (a) the parent's OWN letter-spacing resolves non-empty
//    (the parse WORKS on this machinery) AND (b) the child with NO own letter-spacing is '' (own-element-only, NOT
//    inherited). (a) is what makes (b) bite — without it, child-'' can't distinguish own-element-only from a broken parse.
//  - unset -> '' (NOT 'normal' — the documented own-inline partial).
//  - NAME-keyed read (status is informational): the same element reads TWO CathodeComputed props at DISTINCT values —
//    letter-spacing '1.6px' AND opacity '1' (the initial). A status-keyed read couldn't return two different values for
//    ONE status; the per-NAME branch does. The bite is the DISTINCT values, NOT 'both non-empty'.

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
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-letterspacing-test>", JS_EVAL_TYPE_GLOBAL);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomLetterSpacingTest,
	"Cathode.Dom.Style.LetterSpacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomLetterSpacingTest::RunTest(const FString& Parameters)
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
		FDomNode* N = Doc->NodeFromWrapper(W);
		if (Style && *Style) { N->SetAttribute(TEXT("style"), Style); }
		Parent->AppendChild(W);
		return W;
	};

	const FJsValue ElPx    = MakeEl(Root, TEXT("letter-spacing:2px"));                  // px own value
	const FJsValue ElEm    = MakeEl(Root, TEXT("font-size:16px;letter-spacing:0.1em")); // em (load-bearing) + opacity contrast
	const FJsValue Parent  = MakeEl(Root, TEXT("letter-spacing:3px"));                  // inheritance: parent HAS its own
	const FJsValue Child   = MakeEl(Doc->NodeFromWrapper(Parent), nullptr);             // inheritance: child has NONE
	const FJsValue ElUnset = MakeEl(Root, TEXT("color:red"));                           // unset -> ''

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__px",     JS_DupValue(Context, ElPx.GetValue()));
	JS_SetPropertyStr(Context, Global, "__em",     JS_DupValue(Context, ElEm.GetValue()));
	JS_SetPropertyStr(Context, Global, "__parent", JS_DupValue(Context, Parent.GetValue()));
	JS_SetPropertyStr(Context, Global, "__child",  JS_DupValue(Context, Child.GetValue()));
	JS_SetPropertyStr(Context, Global, "__unset",  JS_DupValue(Context, ElUnset.GetValue()));
	JS_FreeValue(Context, Global);

	Doc->Relayout(800); // populate FResolvedStyle (QueryComputedValue parses letter-spacing per element)

	auto Js = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// px own value.
	TestTrue(TEXT("px: getComputedStyle(__px).letter-spacing === '2px'"),
		Js("getComputedStyle(__px)['letter-spacing'] === '2px'"));

	// em LOAD-BEARING: 0.1em of own font-size 16px -> '1.6px' (specified '0.1em' != computed '1.6px' -> an echo FAILS).
	TestTrue(TEXT("em (load-bearing): getComputedStyle(__em).letter-spacing === '1.6px' (0.1em of 16px; not an echo of '0.1em')"),
		Js("getComputedStyle(__em)['letter-spacing'] === '1.6px'"));

	// NON-VACUOUS unimplemented-inheritance, SAME setup: (a) parent own non-empty (parse works) THEN (b) child no-own '' (own-element-only).
	TestTrue(TEXT("inheritance (a): getComputedStyle(__parent).letter-spacing === '3px' (own value resolves — the parse WORKS)"),
		Js("getComputedStyle(__parent)['letter-spacing'] === '3px'"));
	TestTrue(TEXT("inheritance (b): getComputedStyle(__child).letter-spacing === '' (no own -> own-element-only, NOT inherited; bites GIVEN (a))"),
		Js("getComputedStyle(__child)['letter-spacing'] === ''"));

	// unset -> '' (NOT 'normal').
	TestTrue(TEXT("unset: getComputedStyle(__unset).letter-spacing === '' (own-inline partial; not 'normal')"),
		Js("getComputedStyle(__unset)['letter-spacing'] === ''"));

	// NAME-KEYED read (status informational): __em reads TWO CathodeComputed props at DISTINCT values — letter-spacing
	// '1.6px' AND opacity '1' (initial; __em sets no own opacity). A STATUS-keyed read could NOT yield two different values
	// for ONE status; the per-NAME QueryComputedValue branch does. The BITE is the distinct values ('1.6px' != '1'), NOT
	// 'both non-empty' (vacuous: opacity's initial is '1').
	TestTrue(TEXT("NAME-keyed: __em letter-spacing === '1.6px' AND opacity === '1' (two CathodeComputed props, distinct per-name values)"),
		Js("getComputedStyle(__em)['letter-spacing'] === '1.6px' && getComputedStyle(__em)['opacity'] === '1'"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
