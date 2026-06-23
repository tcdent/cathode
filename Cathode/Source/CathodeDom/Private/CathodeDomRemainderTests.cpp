#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Sprint 012 DOM-completion remainder — the focus model (focus/blur/activeElement over the document's FocusedNodeId,
// STATE-ONLY), the document accessors (body/documentElement), and namespaceURI (the slice-5 ns come-due reversal).
// This CLOSES DOM-completion. Load-bearing: blur is ELEMENT-SCOPED (no-op if not the active element) + the ns
// set-site completeness (every element mint funnels through CreateElementNS, the single store site).

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

JSValue Eval(JSContext* Context, const char* Source)
{
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-remainder-test>", JS_EVAL_TYPE_GLOBAL);
}
} // namespace

// ===== Focus model — focus/blur/activeElement (STATE-ONLY; latest-wins; blur element-scoped; not serialized) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomFocusTest,
	"Cathode.Dom.Completion.Focus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomFocusTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	Doc->InstallAsGlobal();

	FDomNode* Root = Doc->FindNode(Doc->GetRootNodeId());
	FJsValue El1W = Doc->CreateElement(TEXT("input")); FDomNode* El1 = Doc->NodeFromWrapper(El1W); Root->AppendChild(El1W);
	FJsValue El2W = Doc->CreateElement(TEXT("input")); FDomNode* El2 = Doc->NodeFromWrapper(El2W); Root->AppendChild(El2W);

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__el1", JS_DupValue(Context, El1W.GetValue()));
	JS_SetPropertyStr(Context, Global, "__el2", JS_DupValue(Context, El2W.GetValue()));
	JS_FreeValue(Context, Global);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// INITIAL: activeElement === null (cleared state).
	TestTrue(TEXT("initial document.activeElement === null"), EvalBool("document.activeElement === null"));
	// IDENTITY: el.focus() -> activeElement === el (the SAME wrapper).
	TestTrue(TEXT("el1.focus() -> document.activeElement === el1 (identity)"),
		EvalBool("(function(){ __el1.focus(); return document.activeElement === __el1; })()"));
	// C++ read-back AGREES with JS activeElement.
	TestEqual(TEXT("C++ GetFocusedNode() agrees with JS activeElement (el1)"), Doc->GetFocusedNode(), El1->GetNodeId());
	// LATEST-WINS (single-valued): focusing el2 replaces el1.
	TestTrue(TEXT("latest-wins: el2.focus() -> activeElement === el2"),
		EvalBool("(function(){ __el2.focus(); return document.activeElement === __el2; })()"));
	// BLUR IS ELEMENT-SCOPED: el1.blur() WHILE el2 is active is a NO-OP (does NOT clear someone else's focus).
	TestTrue(TEXT("blur element-scoped: el1.blur() while el2 is active is a NO-OP (activeElement stays el2)"),
		EvalBool("(function(){ __el1.blur(); return document.activeElement === __el2; })()"));
	// el2.blur() (THE active element) clears focus.
	TestTrue(TEXT("el2.blur() (the active element) -> activeElement === null"),
		EvalBool("(function(){ __el2.blur(); return document.activeElement === null; })()"));
	// CLEARED === INITIAL (both null) + C++ read-back agrees.
	TestEqual(TEXT("C++ GetFocusedNode() === INDEX_NONE after blur (cleared === initial)"), Doc->GetFocusedNode(), FDomNodeId(INDEX_NONE));

	// NOT SERIALIZED (runtime-only, like the forms live value): focusing does NOT change the layout-feed markup.
	{
		JSValue R = Eval(Context, "__el1.focus();"); JS_FreeValue(Context, R);
		const FString HtmlFocused = Doc->GetDocumentHtml();
		JSValue R2 = Eval(Context, "__el1.blur();"); JS_FreeValue(Context, R2);
		const FString HtmlBlurred = Doc->GetDocumentHtml();
		TestEqual(TEXT("focus state is NOT serialized: the markup is identical focused vs blurred"), HtmlFocused, HtmlBlurred);
		TestTrue(TEXT("focus state is NOT serialized: no focus marker in the markup"),
			!HtmlFocused.Contains(TEXT("activeElement")) && !HtmlFocused.Contains(TEXT("autofocus")));
	}

	return true;
}

// ===== Document accessors — body / documentElement (S2 identity) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomDocAccessorsTest,
	"Cathode.Dom.Completion.DocumentAccessors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomDocAccessorsTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	Doc->InstallAsGlobal();

	FDomNode* Root = Doc->FindNode(Doc->GetRootNodeId()); // the auto-created <html>
	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__html", Doc->GetWrapperForNode(Doc->GetRootNodeId()).Release());

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// documentElement === the <html> root (RootWrapper = CreateElement('html')).
	TestTrue(TEXT("document.documentElement === the <html> root"), EvalBool("document.documentElement === __html"));
	// 028 body-seed: a fresh doc now SEEDS <body> (the mount contract), so document.body is the SEEDED element
	// (non-null) on a fresh doc — not null. document.body === the seeded <body> (identity), then the no-body
	// accessor path preserved (remove the body -> null).
	const FDomNodeId SeededBodyId = Doc->FindBody();
	TestTrue(TEXT("document.body resolves the SEEDED <body> on a fresh doc (non-null)"), SeededBodyId != INDEX_NONE);
	if (SeededBodyId != INDEX_NONE)
	{
		JS_SetPropertyStr(Context, Global, "__body", Doc->GetWrapperForNode(SeededBodyId).Release());
		TestTrue(TEXT("document.body === the seeded <body> element (identity)"), EvalBool("document.body === __body"));

		// The no-body accessor path: remove the seeded <body> -> document.body === null.
		Root->RemoveChild(Doc->GetWrapperForNode(SeededBodyId));
		TestTrue(TEXT("document.body === null after the <body> is removed (the no-body accessor path)"), EvalBool("document.body === null"));
	}
	JS_FreeValue(Context, Global);

	return true;
}

// ===== namespaceURI — the come-due reversal; set-site completeness (every element mint stores the ns) =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomNamespaceTest,
	"Cathode.Dom.Completion.NamespaceURI",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomNamespaceTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	Doc->InstallAsGlobal();

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// createElement defaults to the HTML ns (the store site, via CreateElementNS).
	TestTrue(TEXT("createElement('div').namespaceURI === the HTML ns"),
		EvalBool("document.createElement('div').namespaceURI === 'http://www.w3.org/1999/xhtml'"));
	// createElementNS STORES + reads back the passed ns (the slice-5 reversal — was accepted-but-dropped).
	TestTrue(TEXT("createElementNS(svg ns,'svg').namespaceURI === that ns (the come-due reversal)"),
		EvalBool("document.createElementNS('http://www.w3.org/2000/svg','svg').namespaceURI === 'http://www.w3.org/2000/svg'"));
	// A TEXT node has a null namespaceURI.
	TestTrue(TEXT("a text node -> namespaceURI === null"),
		EvalBool("document.createTextNode('hi').namespaceURI === null"));
	// SET-SITE COMPLETENESS: the auto-<html> root (created via CreateElement) also stores the ns.
	TestTrue(TEXT("the <html> root (a CreateElement mint) has the HTML ns too"),
		EvalBool("document.documentElement.namespaceURI === 'http://www.w3.org/1999/xhtml'"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
