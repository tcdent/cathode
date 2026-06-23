#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Sprint 012 slice 5 — the document subsystem. Every oracle reaches the document THROUGH globalThis.document
// (what InstallAsGlobal SETS), NOT a C++ side-handle — so the proof covers InstallAsGlobal's wrap AND set-global
// (if set-global were broken, globalThis.document is undefined and the oracle throws/fails). InstallAsGlobal is
// PRODUCTION CathodeDom code -> would-it-pass-if-test-only = NO; only the OnInstallDocument seam AUTO-invocation
// (per-surface, gap-doc'd) is held. The gc_mark proof is added per the lead's ruling on its discriminating form.

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

void EvalGlobal(JSContext* Ctx, const char* Src)
{
	JSValue Result = JS_Eval(Ctx, Src, FCStringAnsi::Strlen(Src), "<document-test>", JS_EVAL_TYPE_GLOBAL);
	JS_FreeValue(Ctx, Result);
}

bool EvalBool(JSContext* Ctx, const char* Src)
{
	JSValue Result = JS_Eval(Ctx, Src, FCStringAnsi::Strlen(Src), "<document-test>", JS_EVAL_TYPE_GLOBAL);
	bool bValue = false;
	const bool bOk = UnmarshalBool(Ctx, Result, bValue);
	JS_FreeValue(Ctx, Result);
	return bOk && bValue;
}

JSValue Eval(JSContext* Ctx, const char* Src)
{
	return JS_Eval(Ctx, Src, FCStringAnsi::Strlen(Src), "<document-test>", JS_EVAL_TYPE_GLOBAL);
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomDocumentTest,
	"Cathode.Dom.Document.Subsystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomDocumentTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	// Install the document as globalThis.document (the PRODUCTION install method). Every assertion below reaches
	// it THROUGH globalThis.document, so a broken wrap OR a broken set-global fails the oracle.
	Doc->InstallAsGlobal();
	TestTrue(TEXT("document is installed as a global object"), EvalBool(Context, "typeof document === 'object' && document !== null"));

	// Expose the root + a target element so author JS can build the tree (documentElement/body are deferred).
	FJsValue RootW = Doc->GetWrapperForNode(Doc->GetRootNodeId());
	FJsValue ElW = Doc->CreateElement(TEXT("div"));
	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__root", JS_DupValue(Context, RootW.GetValue()));
	JS_SetPropertyStr(Context, Global, "__el", JS_DupValue(Context, ElW.GetValue()));
	JS_FreeValue(Context, Global);

	// --- getElementById END-TO-END === through the attribute store (review #1) ---
	// el.id = x (the slice-3 REFLECTED property -> writes the id ATTRIBUTE); append; document.getElementById(x)
	// === the SAME wrapper (S2 marshaler). REQUIRE === (identity, not resolves-to-the-right-node).
	EvalGlobal(Context, "__root.appendChild(__el); __el.id = 'x';");
	TestTrue(TEXT("getElementById(reflected el.id) === the element (identity, via globalThis.document)"),
		EvalBool(Context, "document.getElementById('x') === __el"));
	TestTrue(TEXT("getElementById(non-existent) === null"),
		EvalBool(Context, "document.getElementById('nope') === null"));
	// The OTHER write path: setAttribute('id', y) -> getElementById(y) === el; and the old id no longer matches.
	EvalGlobal(Context, "__el.setAttribute('id', 'y');");
	TestTrue(TEXT("getElementById(setAttribute id) === the element"),
		EvalBool(Context, "document.getElementById('y') === __el"));
	TestTrue(TEXT("the stale id 'x' no longer resolves (find is through the live attribute)"),
		EvalBool(Context, "document.getElementById('x') === null"));

	// --- createElement / createTextNode / createElementNS mint real nodes (through globalThis.document) ---
	TestTrue(TEXT("document.createElement mints a real element (tagName + nodeType 1)"),
		EvalBool(Context, "var e = document.createElement('div'); e.tagName === 'div' && e.nodeType === 1"));
	TestTrue(TEXT("document.createTextNode mints a real text node (nodeType 3)"),
		EvalBool(Context, "var t = document.createTextNode('hi'); t.nodeType === 3 && t.data === 'hi'"));
	TestTrue(TEXT("document.createElementNS mints by qualifiedName (HTML ns; tagName + nodeType 1)"),
		EvalBool(Context, "var s = document.createElementNS('http://www.w3.org/1999/xhtml', 'span'); s.tagName === 'span' && s.nodeType === 1"));

	return true;
}

// Sprint 012 slice 5 — the DISPOSE-RECLAIM discriminating test (replaces the vacuous gc_mark survival test).
// MECHANISM ATTRIBUTION: the reclaim mechanism is EXISTING — 008's FDomDocument::Dispose (drops RootWrapper) +
// the node gc_mark/T4 (reclaims unrooted intra-tree cycles). Slice 5 did NOT invent it. But slice 5's
// getElementById + the running engine make 008's DEFERRED reclaim VALIDATION-PASS expressible for the first time
// (FindNode-as-the-index-witness), so this test DISCHARGES that deferred reclaim proof: T4 proved gc-VISIBILITY
// (a cycle is collectable); actual reclaim THROUGH Dispose was never tested. (Bonus: a real end-to-end reclaim
// observed de-risks the quickjs.c-not-vendored caveat.) document.gc_mark is intentionally ABSENT — see the
// lifetime note in CathodeDom/COVERAGE.md (carry-not-rediscover: it returns when the event API is bound to JS).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomDocumentDisposeReclaimTest,
	"Cathode.Dom.Document.DisposeReclaim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomDocumentDisposeReclaimTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	JSRuntime* Rt = JS_GetRuntime(Context);

	// document is the global (the tree's root is held by the document's C++ RootWrapper).
	Doc->InstallAsGlobal();

	FDomNodeId TargetId = INDEX_NONE;
	{
		// Append a target under the document's root, give it a node<->listener CYCLE (so refcount alone CANNOT
		// reclaim it — JS_RunGC must), then DROP every external handle: the target lives ONLY via the tree
		// (RootWrapper -> root -> Children -> target) + its own cycle.
		FJsValue RootW = Doc->GetWrapperForNode(Doc->GetRootNodeId());
		FDomNode* Root = Doc->NodeFromWrapper(RootW);
		FJsValue TargetW = Doc->CreateElement(TEXT("div"));
		FDomNode* Target = Doc->NodeFromWrapper(TargetW);
		if (Root == nullptr || Target == nullptr) { AddError(TEXT("root/target wrapper did not resolve")); return false; }
		TargetId = Target->GetNodeId();
		Root->AppendChild(TargetW); // the tree (root.Children) holds the target wrapper

		FJsValue Listener(Context, Eval(Context, "(function(){})"));
		JS_SetPropertyStr(Context, Listener.GetValue(), "node", JS_DupValue(Context, TargetW.GetValue())); // listener -> target
		Target->AddEventListener(TEXT("click"), MoveTemp(Listener)); // target -> listener  =>  a refcount cycle
	} // RootW + TargetW destruct -> every external handle dropped; the target lives only via the tree + its cycle

	// THE DISCRIMINATING (leaks-WITHOUT-Dispose) HALF: RootWrapper (the document's C++ owning root ref) pins the
	// root, so the target is reachable via the tree -> a real JS_RunGC does NOT reclaim it. (Were it reclaimed
	// here, the "reclaimed after Dispose" half would be vacuous — this is the tension that must fire.)
	JS_RunGC(Rt);
	TestTrue(TEXT("WITHOUT Dispose: RootWrapper pins the tree -> the cyclic target is NOT reclaimed (leaks-without-Dispose)"),
		Doc->FindNode(TargetId) != nullptr);

	// THE RECLAIM HALF: Dispose() drops RootWrapper (runtime alive) -> the root is unrooted (acyclic cascade) ->
	// the target's cycle is unreachable -> JS_RunGC reclaims it (node gc_mark/T4). The reclaimed node finalizes
	// -> ~FDomNode -> UnregisterNode -> it leaves the index.
	Doc->Dispose();
	JS_RunGC(Rt);
	TestTrue(TEXT("AFTER Dispose + JS_RunGC: the tree IS reclaimed (target left the index) — the 008 deferred reclaim proof"),
		Doc->FindNode(TargetId) == nullptr);

	return true;
}

// 028 S1 — the body-seed (the tcdent mount contract, locked 2026-06-22). A FRESH FDomDocument's CTOR seeds
// documentElement > [head, body] — the THREE nodes ONLY (no root div / #app / config). document.body/head resolve as
// the mount/structure targets; an appendChild into body lands on the real retained tree. Every assertion is DIRECT
// off the source-of-truth FDomNode tree (Model-A: the children ARE the wrapper refs), never a mediated
// querySelector/getComputedStyle/JS read-back (the direct-vs-indirect discipline).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomBodySeedTest,
	"Cathode.Dom.Document.BodySeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomBodySeedTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	// A JUST-CONSTRUCTED document — the ctor seeds the mount contract (the change), NOT a manual create.
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);

	// (A) THE EXACT CHILD SET — documentElement's children are EXACTLY [head, body]: count==2, in order, NOTHING
	// extra (a root div / #app seed would be a 3rd child -> this REDs). The mount-contract's "nothing else" IS count==2.
	FDomNode* Root = Doc->FindNode(Doc->GetRootNodeId());
	if (Root == nullptr) { AddError(TEXT("[028-BODYSEED] documentElement (the root) is null")); return false; }
	TArray<FDomNodeId> RootKids;
	Root->GetChildNodes(RootKids);
	TestEqual(TEXT("[028-BODYSEED] documentElement has EXACTLY 2 children — head + body, nothing extra (no root div / #app / config)"), RootKids.Num(), 2);
	if (RootKids.Num() == 2)
	{
		const FDomNode* Head = Doc->FindNode(RootKids[0]);
		const FDomNode* Body = Doc->FindNode(RootKids[1]);
		TestTrue(TEXT("[028-BODYSEED] documentElement's FIRST child is <head> (document.head non-null)"),
			Head != nullptr && Head->GetTagName() == TEXT("head"));
		TestTrue(TEXT("[028-BODYSEED] documentElement's SECOND child is <body> (head precedes body — document order)"),
			Body != nullptr && Body->GetTagName() == TEXT("body"));
	}

	// (C) FRESH-DOC document.body NON-NULL — the ctor seeded it; a no-seed ctor -> FindBody == INDEX_NONE -> RED.
	const FDomNodeId BodyId = Doc->FindBody();
	TestTrue(TEXT("[028-BODYSEED] document.body resolves NON-NULL on a JUST-CONSTRUCTED doc (FindBody != INDEX_NONE — the ctor seeded body)"),
		BodyId != INDEX_NONE);

	// (B) THE DIRECT-TREE APPENDCHILD — append a probe into body; assert DIRECT off the body FDomNode's
	// GetChildNodes (the retained C++ source-of-truth), NOT a querySelector / getComputedStyle / JS read-back.
	if (BodyId != INDEX_NONE)
	{
		FDomNode* Body = Doc->FindNode(BodyId);
		const FJsValue ProbeW = Doc->CreateElement(TEXT("div"));
		const FDomNode* Probe = Doc->NodeFromWrapper(ProbeW);
		const FDomNodeId ProbeId = (Probe != nullptr) ? Probe->GetNodeId() : INDEX_NONE;
		if (Body != nullptr) { Body->AppendChild(ProbeW); }

		TArray<FDomNodeId> BodyKids;
		if (Body != nullptr) { Body->GetChildNodes(BodyKids); }
		TestTrue(TEXT("[028-BODYSEED] appendChild into document.body LANDS on the real source-of-truth tree (body FDomNode's GetChildNodes contains the probe — DIRECT, not a mediated read)"),
			BodyKids.Contains(ProbeId));
	}

	AddInfo(FString::Printf(TEXT("[028-BODYSEED] fresh-doc seed: documentElement children=%d (expect 2: head, body) | document.body resolved=%d"),
		RootKids.Num(), BodyId != INDEX_NONE ? 1 : 0));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
