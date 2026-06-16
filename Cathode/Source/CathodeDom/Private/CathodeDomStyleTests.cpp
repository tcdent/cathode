#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Sprint 012 style (with CSS stubs) — element.style (a LIVE write-through CSSStyleDeclaration over the canonical
// 'style' attribute) + getComputedStyle (a GLOBAL returning a READ-ONLY CSSStyleDeclaration over FResolvedStyle, the
// stub joint). CSSStyleDeclaration is a C++-EXOTIC class (open-ended names via get_own_property/set_property). The
// load-bearing proof is the 'style'-attribute-as-single-source-of-truth CROSS-PATH discriminator (BOTH directions)
// — a parallel store would pass same-path but DESYNC cross-path. The actual cascade is STUBBED (the CSS epic).

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

JSValue Eval(JSContext* Context, const char* Source)
{
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-style-test>", JS_EVAL_TYPE_GLOBAL);
}
} // namespace

// ===== element.style — the LIVE write-through CSSStyleDeclaration over the canonical 'style' attribute =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomElementStyleTest,
	"Cathode.Dom.Style.ElementStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomElementStyleTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	FJsValue ElW = Doc->CreateElement(TEXT("div"));
	Doc->FindNode(Doc->GetRootNode())->AppendChild(ElW); // in the tree (for the serialization test)

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__el", JS_DupValue(Context, ElW.GetValue()));
	JS_FreeValue(Context, Global);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// instanceof + the type.
	TestTrue(TEXT("el.style instanceof CSSStyleDeclaration"), EvalBool("__el.style instanceof CSSStyleDeclaration"));

	// CROSS-PATH (a) WRITE via style -> READ via the 'style' ATTRIBUTE (the SoT): the named write reaches the attr.
	TestTrue(TEXT("(a) style.color='red' -> getAttribute('style') carries it AND el.style.color round-trips"),
		EvalBool("(function(){ __el.style.color='red'; var s=__el.getAttribute('style'); return s.indexOf('red')>=0 && __el.style.color==='red'; })()"));

	// CROSS-PATH (b) WRITE via the ATTRIBUTE -> READ via style: setAttribute is seen by style (no parallel store).
	TestTrue(TEXT("(b) setAttribute('style','color: blue') -> el.style.color === 'blue'"),
		EvalBool("(function(){ __el.setAttribute('style','color: blue'); return __el.style.color==='blue'; })()"));

	// cssText IS the attribute string (both directions).
	TestTrue(TEXT("cssText get === the 'style' attribute"),
		EvalBool("(function(){ __el.setAttribute('style','color: blue'); return __el.style.cssText==='color: blue'; })()"));
	TestTrue(TEXT("cssText set -> getAttribute('style') updated AND style.color / style.fontSize reflect"),
		EvalBool("(function(){ __el.style.cssText='color:red;font-size:12px'; return __el.getAttribute('style')==='color:red;font-size:12px' && __el.style.color==='red' && __el.style.fontSize==='12px'; })()"));

	// OPEN-ENDED names route through the C++ EXOTIC handler (not just color/font-size): a custom prop + camelCase.
	TestTrue(TEXT("arbitrary custom prop routes through the handler: style['--custom']='42' round-trips"),
		EvalBool("(function(){ var s=__el.style; s['--custom']='42'; return s['--custom']==='42' && __el.getAttribute('style').indexOf('--custom')>=0; })()"));
	TestTrue(TEXT("camelCase routes to dash-case: style.fontSize='9px' -> getAttribute('style') has 'font-size'"),
		EvalBool("(function(){ __el.style.fontSize='9px'; return __el.style.fontSize==='9px' && __el.getAttribute('style').indexOf('font-size')>=0; })()"));

	// setProperty (the var()/custom-props path — required) writes through (name AS-IS, dash-case/--custom).
	TestTrue(TEXT("setProperty('--theme','red') write-through: style['--theme']==='red'"),
		EvalBool("(function(){ __el.style.setProperty('--theme','red'); return __el.style['--theme']==='red' && __el.getAttribute('style').indexOf('--theme')>=0; })()"));

	// SERIALIZATION (the INVERSE of forms): the 'style' attribute IS canonical -> it SHOULD reach the layout-feed
	// markup (unlike forms' runtime-only live value which must NOT leak). The inline write reaches GetDocumentHtml.
	{
		JSValue R = Eval(Context, "(function(){ __el.setAttribute('style',''); __el.style.color='green'; return true; })()");
		JS_FreeValue(Context, R);
		const FString Html = Doc->GetDocumentHtml();
		TestTrue(TEXT("serialization (INVERSE of forms): the inline style reaches the layout-feed markup (style attr serializes)"),
			Html.Contains(TEXT("style=")) && Html.Contains(TEXT("green")));
	}

	return true;
}

// ===== getComputedStyle — a GLOBAL returning a READ-ONLY CSSStyleDeclaration over FResolvedStyle (the stub joint) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomGetComputedStyleTest,
	"Cathode.Dom.Style.GetComputedStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomGetComputedStyleTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	// The REAL litehtml engine + a Relayout populates FResolvedStyle (with the slice-5 STUB empty values) so the
	// getComputedStyle READ PATH reaches it. (The values are empty = the stubbed cascade — that is CORRECT.)
	FStubMeasurer Measurer;
	TUniquePtr<ILayoutEngine> RealEngine = CreateLiteHtmlLayoutEngine(Measurer);
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, *RealEngine);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	FJsValue ElW = Doc->CreateElement(TEXT("div"));
	FDomNode* El = Doc->NodeFromWrapper(ElW);
	El->SetAttribute(TEXT("style"), TEXT("width:50px;height:30px"));
	Doc->FindNode(Doc->GetRootNode())->AppendChild(ElW);
	Doc->Relayout(800); // populate FResolvedStyle (empty stub values for the contract properties)

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__el", JS_DupValue(Context, ElW.GetValue()));
	JS_FreeValue(Context, Global);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// getComputedStyle is a GLOBAL function returning a CSSStyleDeclaration (same type as element.style).
	TestTrue(TEXT("getComputedStyle is a global function"), EvalBool("typeof getComputedStyle === 'function'"));
	TestTrue(TEXT("getComputedStyle(el) instanceof CSSStyleDeclaration"), EvalBool("getComputedStyle(__el) instanceof CSSStyleDeclaration"));

	// READ PATH wired to the STUB joint: a contract property reads from FResolvedStyle -> '' (the stubbed empty
	// value — CORRECT; 1-line-real when the epic implements QueryComputedValue). NOT that styling cascades.
	TestTrue(TEXT("getComputedStyle(el).color === '' (the FResolvedStyle read path; empty = the stubbed cascade)"),
		EvalBool("getComputedStyle(__el).color === ''"));

	// READ-ONLY: a write is a silent NO-OP per spec (distinct from element.style's write-through).
	TestTrue(TEXT("getComputedStyle(el) is READ-ONLY: a write is a no-op (.color stays '')"),
		EvalBool("(function(){ var cs=getComputedStyle(__el); cs.color='x'; return cs.color===''; })()"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
