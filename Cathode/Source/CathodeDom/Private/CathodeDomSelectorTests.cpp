#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Selectors — querySelector (Element + Document) DISPATCH-CORRECTNESS through the REAL bindings, against
// the REAL CathodeLayout matcher (CreateSelectorService — litehtml-grounded parse + a live-tree structural match).
// The matcher walks the LIVE FDomNode tree via IMatchableNode; the load-bearing discriminator is a POST-MUTATION
// match (no relayout) — a matcher reading litehtml's per-layout snapshot would MISS it. Proves the supported set
// (tag/.class/#id/[attr]/descendant/child/:not), subtree-vs-document scoping + EXCLUDE-SELF + outside-subtree
// exclusion, first-match in pre-order, === identity, no-match -> null, and invalid/unsupported -> SyntaxError.

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
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-selector-test>", JS_EVAL_TYPE_GLOBAL);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomQuerySelectorTest,
	"Cathode.Dom.Selector.QuerySelector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomQuerySelectorTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	// The REAL matcher (CathodeLayout, litehtml-grounded). Declared BEFORE the document so it outlives the doc's
	// non-owning pointer to it.
	TUniquePtr<ISelectorService> Service = CreateSelectorService();
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout, Service.Get());
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	Doc->InstallAsGlobal(); // globalThis.document

	// Tree (under the auto-created <html> root):
	//   root(div #root) > [ a(section .outer) > [ b(p .item #first) > c(span data-x=1), d(p .item) ], e(footer) > f(span .item) ]
	// .item appears at b, d, f in pre-order; f is OUTSIDE a's subtree; e IS a <footer> (exclude-self probe).
	FDomNode* Html = Doc->FindNode(Doc->GetRootNodeId());
	auto MakeEl = [&](const TCHAR* Tag, FDomNode* Parent, TFunction<void(FDomNode&)> Init) -> FJsValue
	{
		FJsValue W = Doc->CreateElement(Tag);
		FDomNode* N = Doc->NodeFromWrapper(W);
		Init(*N);
		if (Parent != nullptr) { Parent->AppendChild(W); }
		return W;
	};
	FJsValue RootW = MakeEl(TEXT("div"), Html, [](FDomNode& N){ N.SetAttribute(TEXT("id"), TEXT("root")); });
	FDomNode* Root = Doc->NodeFromWrapper(RootW);
	FJsValue AW = MakeEl(TEXT("section"), Root, [](FDomNode& N){ N.SetAttribute(TEXT("class"), TEXT("outer")); });
	FDomNode* A = Doc->NodeFromWrapper(AW);
	FJsValue BW = MakeEl(TEXT("p"), A, [](FDomNode& N){ N.SetAttribute(TEXT("class"), TEXT("item")); N.SetAttribute(TEXT("id"), TEXT("first")); });
	FDomNode* B = Doc->NodeFromWrapper(BW);
	FJsValue CW = MakeEl(TEXT("span"), B, [](FDomNode& N){ N.SetAttribute(TEXT("data-x"), TEXT("1")); });
	FJsValue DW = MakeEl(TEXT("p"), A, [](FDomNode& N){ N.SetAttribute(TEXT("class"), TEXT("item")); });
	FJsValue EW = MakeEl(TEXT("footer"), Root, [](FDomNode&){});
	FDomNode* E = Doc->NodeFromWrapper(EW);
	FJsValue FW = MakeEl(TEXT("span"), E, [](FDomNode& N){ N.SetAttribute(TEXT("class"), TEXT("item")); });

	JSValue Global = JS_GetGlobalObject(Context);
	auto Expose = [&](const char* Name, const FJsValue& W) { JS_SetPropertyStr(Context, Global, Name, JS_DupValue(Context, W.GetValue())); };
	Expose("__root", RootW); Expose("__a", AW); Expose("__b", BW); Expose("__c", CW); Expose("__d", DW); Expose("__e", EW); Expose("__f", FW);
	JS_FreeValue(Context, Global);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw evaluating: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false;
		const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// --- Supported set, against the WHOLE tree (document.querySelector), first-match in pre-order, === identity ---
	TestTrue(TEXT("type: document.querySelector('span') === c (first span pre-order)"), EvalBool("document.querySelector('span') === __c"));
	TestTrue(TEXT(".class: document.querySelector('.item') === b (FIRST of b,d,f pre-order)"), EvalBool("document.querySelector('.item') === __b"));
	TestTrue(TEXT(".class first-match is NOT the later d or f"), EvalBool("document.querySelector('.item') !== __d && document.querySelector('.item') !== __f"));
	TestTrue(TEXT("#id: document.querySelector('#first') === b"), EvalBool("document.querySelector('#first') === __b"));
	TestTrue(TEXT("[attr]: document.querySelector('[data-x]') === c"), EvalBool("document.querySelector('[data-x]') === __c"));
	TestTrue(TEXT("[attr=val]: document.querySelector('[data-x=\"1\"]') === c"), EvalBool("document.querySelector('[data-x=\"1\"]') === __c"));
	TestTrue(TEXT("descendant 'section .item' === b"), EvalBool("document.querySelector('section .item') === __b"));
	TestTrue(TEXT("descendant 'section span' === c"), EvalBool("document.querySelector('section span') === __c"));
	TestTrue(TEXT("child 'section > p' === b (direct child)"), EvalBool("document.querySelector('section > p') === __b"));
	TestTrue(TEXT("child 'section > span' === null (c is a grandchild, not a direct child)"), EvalBool("document.querySelector('section > span') === null"));
	TestTrue(TEXT(":not 'p:not(#first)' === d (b is #first, excluded)"), EvalBool("document.querySelector('p:not(#first)') === __d"));
	TestTrue(TEXT("=== identity: two calls return the SAME wrapper"), EvalBool("document.querySelector('#root') === document.querySelector('#root')"));
	TestTrue(TEXT("=== identity: querySelector('#root') === the live element"), EvalBool("document.querySelector('#root') === __root"));
	TestTrue(TEXT("no-match -> null"), EvalBool("document.querySelector('.nonexistent') === null"));

	// --- Scoping: element.querySelector = DESCENDANTS ONLY, EXCLUDE-SELF, outside-subtree excluded ---
	TestTrue(TEXT("element scope: a.querySelector('.item') === b (first .item in a's subtree)"), EvalBool("__a.querySelector('.item') === __b"));
	TestTrue(TEXT("EXCLUDE-SELF: e.querySelector('footer') === null (e IS a footer, never returns self)"), EvalBool("__e.querySelector('footer') === null"));
	TestTrue(TEXT("EXCLUDE-SELF cross-check: document.querySelector('footer') === e (self is matchable at document scope)"), EvalBool("document.querySelector('footer') === __e"));
	TestTrue(TEXT("OUTSIDE-SUBTREE excluded: a.querySelector('footer span') === null (f is outside a)"), EvalBool("__a.querySelector('footer span') === null"));
	TestTrue(TEXT("OUTSIDE-SUBTREE cross-check: document.querySelector('footer span') === f"), EvalBool("document.querySelector('footer span') === __f"));
	TestTrue(TEXT("deep-descendant scope: a.querySelector('span') === c (a's deep descendant, not f)"), EvalBool("__a.querySelector('span') === __c"));

	// --- Invalid / UNSUPPORTED selector -> SyntaxError (never a silent mis-match) ---
	auto ThrowsSyntax = [&](const char* Sel) -> bool
	{
		const FString Expr = FString::Printf(TEXT("(function(){ try { document.querySelector(%s); return false; } catch(e){ return e instanceof SyntaxError; } })()"), ANSI_TO_TCHAR(Sel));
		return EvalBool(TCHAR_TO_UTF8(*Expr));
	};
	TestTrue(TEXT("unsupported sibling '+' -> SyntaxError"), ThrowsSyntax("'a + b'"));
	TestTrue(TEXT("unsupported pseudo ':hover' -> SyntaxError"), ThrowsSyntax("':hover'"));
	TestTrue(TEXT("unsupported pseudo-element '::before' -> SyntaxError"), ThrowsSyntax("'::before'"));
	TestTrue(TEXT("malformed selector -> SyntaxError"), ThrowsSyntax("'>>>'"));

	// --- THE LIVE-TREE DISCRIMINATOR: a POST-MUTATION match with NO relayout. A matcher reading litehtml's
	//     per-layout snapshot would MISS these (the mutation hasn't been serialized/relaid-out). ---
	TestTrue(TEXT("live-tree setAttribute: __c.setAttribute('data-y','2') then querySelector('[data-y]') === c (no relayout)"),
		EvalBool("(function(){ __c.setAttribute('data-y','2'); return document.querySelector('[data-y]') === __c; })()"));
	TestTrue(TEXT("live-tree appendChild: append a fresh #grown element then querySelector('#grown') finds it (no relayout)"),
		EvalBool("(function(){ var g = document.createElement('b'); g.setAttribute('id','grown'); __root.appendChild(g); return document.querySelector('#grown') === g; })()"));
	TestTrue(TEXT("live-tree removeAttribute: drop data-x then querySelector('[data-x]') === null (no relayout)"),
		EvalBool("(function(){ __c.removeAttribute('data-x'); return document.querySelector('[data-x]') === null; })()"));

	return true;
}

// Selectors TRIO — querySelectorAll / matches / closest over the SAME matcher (the querySelector test's
// ParseAndValidate + MatchChain; compose-not-duplicate — one service drives all four shapes). Proves: all-matches
// pre-order + the STATIC-snapshot discriminator (capture a list, mutate the tree, the captured list is UNCHANGED);
// matches(self) bool; closest INCLUDES-self (distinct from querySelector's exclude-self) + ancestor-walk + null;
// invalid -> SyntaxError carries. Same tree as the querySelector test: .item at b, d, f (pre-order); f outside a's subtree.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomSelectorTrioTest,
	"Cathode.Dom.Selector.Trio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomSelectorTrioTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<ISelectorService> Service = CreateSelectorService();
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout, Service.Get());
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	Doc->InstallAsGlobal();

	FDomNode* Html = Doc->FindNode(Doc->GetRootNodeId());
	auto MakeEl = [&](const TCHAR* Tag, FDomNode* Parent, TFunction<void(FDomNode&)> Init) -> FJsValue
	{
		FJsValue W = Doc->CreateElement(Tag);
		FDomNode* N = Doc->NodeFromWrapper(W);
		Init(*N);
		if (Parent != nullptr) { Parent->AppendChild(W); }
		return W;
	};
	FJsValue RootW = MakeEl(TEXT("div"), Html, [](FDomNode& N){ N.SetAttribute(TEXT("id"), TEXT("root")); });
	FDomNode* Root = Doc->NodeFromWrapper(RootW);
	FJsValue AW = MakeEl(TEXT("section"), Root, [](FDomNode& N){ N.SetAttribute(TEXT("class"), TEXT("outer")); });
	FDomNode* A = Doc->NodeFromWrapper(AW);
	FJsValue BW = MakeEl(TEXT("p"), A, [](FDomNode& N){ N.SetAttribute(TEXT("class"), TEXT("item")); N.SetAttribute(TEXT("id"), TEXT("first")); });
	FDomNode* B = Doc->NodeFromWrapper(BW);
	FJsValue CW = MakeEl(TEXT("span"), B, [](FDomNode& N){ N.SetAttribute(TEXT("data-x"), TEXT("1")); });
	FJsValue DW = MakeEl(TEXT("p"), A, [](FDomNode& N){ N.SetAttribute(TEXT("class"), TEXT("item")); });
	FJsValue EW = MakeEl(TEXT("footer"), Root, [](FDomNode&){});
	FDomNode* E = Doc->NodeFromWrapper(EW);
	FJsValue FW = MakeEl(TEXT("span"), E, [](FDomNode& N){ N.SetAttribute(TEXT("class"), TEXT("item")); });

	JSValue Global = JS_GetGlobalObject(Context);
	auto Expose = [&](const char* Name, const FJsValue& W) { JS_SetPropertyStr(Context, Global, Name, JS_DupValue(Context, W.GetValue())); };
	Expose("__root", RootW); Expose("__a", AW); Expose("__b", BW); Expose("__c", CW); Expose("__d", DW); Expose("__e", EW); Expose("__f", FW);
	JS_FreeValue(Context, Global);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw evaluating: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false;
		const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// --- querySelectorAll: a NodeList of ALL matches in pre-order; scoping mirrors querySelector ---
	TestTrue(TEXT("querySelectorAll returns a NodeList"), EvalBool("document.querySelectorAll('.item') instanceof NodeList"));
	TestTrue(TEXT("document.querySelectorAll('.item') = [b,d,f] in pre-order"),
		EvalBool("(function(){ var l = document.querySelectorAll('.item'); return l.length === 3 && l[0] === __b && l[1] === __d && l[2] === __f; })()"));
	TestTrue(TEXT("element scope + exclude-self: a.querySelectorAll('.item') = [b,d] (f is outside a)"),
		EvalBool("(function(){ var l = __a.querySelectorAll('.item'); return l.length === 2 && l[0] === __b && l[1] === __d; })()"));
	TestTrue(TEXT("no-match querySelectorAll -> empty NodeList (length 0, not null)"),
		EvalBool("(function(){ var l = document.querySelectorAll('.nonexistent'); return (l instanceof NodeList) && l.length === 0; })()"));

	// --- STATIC-SNAPSHOT discriminator: a captured list does NOT change when the tree mutates after capture ---
	TestTrue(TEXT("querySelectorAll is a STATIC snapshot: captured list unchanged after a later mutation; re-query reflects it"),
		EvalBool("(function(){"
			"  var captured = document.querySelectorAll('.item');"      // capture: b,d,f -> length 3
			"  var before = captured.length;"
			"  var g = document.createElement('p'); g.setAttribute('class','item'); __root.appendChild(g);" // mutate: add a 4th .item
			"  var stillCaptured = captured.length;"                    // the CAPTURED list is UNCHANGED (static)
			"  var requeried = document.querySelectorAll('.item').length;" // a fresh query SEES the mutation
			"  return before === 3 && stillCaptured === 3 && requeried === 4;"
			"})()"));

	// --- matches(self): the primitive tested on the element itself -> bool ---
	TestTrue(TEXT("matches: b.matches('.item') true; b.matches('#first') true; b.matches('p.item') true"),
		EvalBool("__b.matches('.item') && __b.matches('#first') && __b.matches('p.item')"));
	TestTrue(TEXT("matches: b.matches('span') false; b.matches('div') false (b is a p)"),
		EvalBool("!__b.matches('span') && !__b.matches('div')"));
	TestTrue(TEXT("matches: invalid selector -> SyntaxError"),
		EvalBool("(function(){ try { __b.matches('>>>'); return false; } catch(e){ return e instanceof SyntaxError; } })()"));

	// --- closest: nearest of self-or-ANCESTOR (INCLUDES self, distinct from querySelector's exclude-self), or null ---
	TestTrue(TEXT("closest INCLUDES self: b.closest('.item') === b (self matches)"), EvalBool("__b.closest('.item') === __b"));
	TestTrue(TEXT("closest INCLUDES self: b.closest('p') === b"), EvalBool("__b.closest('p') === __b"));
	TestTrue(TEXT("closest ancestor-walk: c.closest('.item') === b (nearest matching ancestor)"), EvalBool("__c.closest('.item') === __b"));
	TestTrue(TEXT("closest ancestor-walk: c.closest('section') === a"), EvalBool("__c.closest('section') === __a"));
	TestTrue(TEXT("closest ancestor-walk: c.closest('#root') === root"), EvalBool("__c.closest('#root') === __root"));
	TestTrue(TEXT("closest no-match -> null"), EvalBool("__c.closest('.nonexistent') === null"));

	// One matcher drives all four shapes (querySelector floor + this trio) via the ONE CathodeLayout service —
	// compose-not-duplicate is structural (a single ISelectorService over the shared ParseAndValidate + MatchChain).

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
