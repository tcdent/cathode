#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Specs for the event system: listener hold + gc_mark visibility (the node<->listener cycle, THE Model-A
// hazard) + removeEventListener reference-drop; dispatch bubble ORDER; stopPropagation / preventDefault. Drive
// the C++ dispatch algorithm directly with a RECORDING invoker (the FDomEvent -> JS Event object marshaling is
// the deferred generate-from-interface part). Assert OUR bookkeeping (mark count, CurrentTarget order, flags).
// gc_mark VISIBILITY (the listener is marked) is proved by FCathodeDomListenerTest. The actual cyclic RECLAIM
// — a real JS_RunGC freeing a node<->listener reference cycle — is the 011 [c] discharge below
// (FCathodeDomCyclicGcReclaimTest): a measured live-object drop across a real GC, distinct from visibility.

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
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-test>", JS_EVAL_TYPE_GLOBAL);
}

int32 GMarkCount = 0;
void CountingMark(JSRuntime* /*Runtime*/, JSGCObjectHeader* /*Header*/) { ++GMarkCount; }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomListenerTest,
	"Cathode.Dom.Event.ListenerHoldMarkAndDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomListenerTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	JSRuntime* Rt = JS_GetRuntime(Context);

	FJsValue ParentW = Doc->CreateElement(TEXT("div"));
	FJsValue ChildW = Doc->CreateElement(TEXT("span"));
	FDomNode* Parent = Doc->NodeFromWrapper(ParentW);
	Parent->AppendChild(ChildW); // one held child wrapper

	// A listener captures the node (conceptually) -> the node<->listener cycle. Hold its fn value for removal.
	FJsValue Fn(Context, Eval(Context, "(function(){})"));
	const JSValue FnVal = Fn.GetValue();
	Parent->AddEventListener(TEXT("click"), MoveTemp(Fn));

	// gc_mark VISITS both the child wrapper AND the listener -> the cycle is GC-VISIBLE (reclaimable). The
	// actual cyclic reclaim needs a real JS_RunGC -> validation-pass.
	GMarkCount = 0;
	Parent->MarkHeldRefs(Rt, &CountingMark);
	TestEqual(TEXT("gc_mark visits child + listener (cycle GC-visible)"), GMarkCount, 2);

	// removeEventListener = the reference-DROP: the listener is no longer held -> no longer marked.
	Parent->RemoveEventListener(TEXT("click"), FnVal);
	GMarkCount = 0;
	Parent->MarkHeldRefs(Rt, &CountingMark);
	TestEqual(TEXT("after removeEventListener only the child is marked"), GMarkCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomBubbleTest,
	"Cathode.Dom.Event.DispatchBubbleOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomBubbleTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	// grandparent > parent > child, a "click" listener on each.
	FJsValue GpW = Doc->CreateElement(TEXT("gp"));
	FJsValue PW = Doc->CreateElement(TEXT("p"));
	FJsValue CW = Doc->CreateElement(TEXT("c"));
	FDomNode* Gp = Doc->NodeFromWrapper(GpW);
	FDomNode* P = Doc->NodeFromWrapper(PW);
	FDomNode* C = Doc->NodeFromWrapper(CW);
	const FDomNodeId GpId = Gp->GetNodeId();
	const FDomNodeId PId = P->GetNodeId();
	const FDomNodeId CId = C->GetNodeId();
	Gp->AppendChild(PW);
	P->AppendChild(CW);
	Gp->AddEventListener(TEXT("click"), FJsValue(Context, Eval(Context, "(function(){})")));
	P->AddEventListener(TEXT("click"), FJsValue(Context, Eval(Context, "(function(){})")));
	C->AddEventListener(TEXT("click"), FJsValue(Context, Eval(Context, "(function(){})")));

	// Dispatch on the child; the recording invoker records CurrentTarget per invocation = the bubble order.
	TArray<FDomNodeId> Order;
	FDomListenerInvoker Record = [&Order](const FJsValue&, FDomEvent& Event) { Order.Add(Event.CurrentTarget); };
	FDomEvent Event;
	Event.Type = TEXT("click");
	Event.bBubbles = true;
	C->DispatchEvent(Event, Record);

	TestTrue(TEXT("bubble order is child -> parent -> grandparent"),
		Order.Num() == 3 && Order[0] == CId && Order[1] == PId && Order[2] == GpId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomPropagationTest,
	"Cathode.Dom.Event.StopPropagationAndPreventDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomPropagationTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	FJsValue GpW = Doc->CreateElement(TEXT("gp"));
	FJsValue PW = Doc->CreateElement(TEXT("p"));
	FJsValue CW = Doc->CreateElement(TEXT("c"));
	FDomNode* Gp = Doc->NodeFromWrapper(GpW);
	FDomNode* P = Doc->NodeFromWrapper(PW);
	FDomNode* C = Doc->NodeFromWrapper(CW);
	const FDomNodeId GpId = Gp->GetNodeId();
	const FDomNodeId PId = P->GetNodeId();
	const FDomNodeId CId = C->GetNodeId();
	Gp->AppendChild(PW);
	P->AppendChild(CW);
	Gp->AddEventListener(TEXT("click"), FJsValue(Context, Eval(Context, "(function(){})")));
	P->AddEventListener(TEXT("click"), FJsValue(Context, Eval(Context, "(function(){})")));
	C->AddEventListener(TEXT("click"), FJsValue(Context, Eval(Context, "(function(){})")));

	// stopPropagation at the parent halts the ascent: grandparent's listener does NOT fire.
	TArray<FDomNodeId> Order;
	FDomListenerInvoker StopAtParent = [&Order, PId](const FJsValue&, FDomEvent& Event)
	{
		Order.Add(Event.CurrentTarget);
		if (Event.CurrentTarget == PId) { Event.bPropagationStopped = true; } // simulate listener.stopPropagation()
	};
	FDomEvent Event;
	Event.Type = TEXT("click");
	Event.bBubbles = true;
	C->DispatchEvent(Event, StopAtParent);

	TestTrue(TEXT("stopPropagation halts ascent at the parent"),
		Order.Num() == 2 && Order[0] == CId && Order[1] == PId);
	TestTrue(TEXT("grandparent was not visited"), !Order.Contains(GpId));
	TestTrue(TEXT("the event records propagation stopped"), Event.bPropagationStopped);

	// preventDefault: a listener sets the flag; it is observable on the event after dispatch.
	FDomEvent Event2;
	Event2.Type = TEXT("click");
	Event2.bBubbles = true;
	FDomListenerInvoker Prevent = [](const FJsValue&, FDomEvent& E) { E.bDefaultPrevented = true; };
	C->DispatchEvent(Event2, Prevent);
	TestTrue(TEXT("preventDefault is observable after dispatch"), Event2.bDefaultPrevented);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomSameNodeStopTest,
	"Cathode.Dom.Event.StopPropagationSameNodeRemaining",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomSameNodeStopTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	// parent > child; the CHILD has TWO click listeners; the parent has one.
	FJsValue PW = Doc->CreateElement(TEXT("p"));
	FJsValue CW = Doc->CreateElement(TEXT("c"));
	FDomNode* P = Doc->NodeFromWrapper(PW);
	FDomNode* C = Doc->NodeFromWrapper(CW);
	const FDomNodeId PId = P->GetNodeId();
	const FDomNodeId CId = C->GetNodeId();
	P->AppendChild(CW);
	C->AddEventListener(TEXT("click"), FJsValue(Context, Eval(Context, "(function(){})")));
	C->AddEventListener(TEXT("click"), FJsValue(Context, Eval(Context, "(function(){})")));
	P->AddEventListener(TEXT("click"), FJsValue(Context, Eval(Context, "(function(){})")));

	// The first child listener stops propagation; the SECOND child listener (same node) STILL fires; the
	// parent (ascent) does NOT — stopPropagation halts the ASCENT, not the same-node remaining listeners.
	int32 ChildInvocations = 0;
	TArray<FDomNodeId> Order;
	FDomListenerInvoker Invoker = [&Order, &ChildInvocations, CId](const FJsValue&, FDomEvent& Event)
	{
		Order.Add(Event.CurrentTarget);
		if (Event.CurrentTarget == CId)
		{
			++ChildInvocations;
			Event.bPropagationStopped = true; // the first child listener calls stopPropagation()
		}
	};
	FDomEvent Event;
	Event.Type = TEXT("click");
	Event.bBubbles = true;
	C->DispatchEvent(Event, Invoker);

	TestEqual(TEXT("both same-node listeners still fired after stopPropagation"), ChildInvocations, 2);
	TestTrue(TEXT("order is the two child listeners only (ascent halted)"),
		Order.Num() == 2 && Order[0] == CId && Order[1] == CId);
	TestTrue(TEXT("the parent (ascent) was not visited"), !Order.Contains(PId));

	return true;
}

// Sprint 011 [c] target 4 — CYCLIC JS_RunGC RECLAIM (the come-due discharge).
//
// Builds a real node<->listener reference CYCLE entirely on the C++/test-seam path (NOT via uninstalled DOM JS
// globals — the bare runtime has no document): the node's native gc_mark reports the listener (node -> listener
// edge), and a JS back-edge (listener.node = the node's wrapper) closes it (listener -> node-wrapper). With all
// external handles dropped, the cycle's refcounts never reach zero, so refcounting ALONE cannot reclaim it.
//
// The ORACLE is a MEASURED live-object DROP across a REAL JS_RunGC — distinct from gc_mark VISIBILITY (008,
// FCathodeDomListenerTest). The non-owning, wrapper-graph-governed node index (FindNode; ~FDomNode ->
// UnregisterNode) is the live-object probe: present before GC (cycle leaks), absent after GC (cycle reclaimed).
// The contrast (1 -> 0 across the single real GC) is the proof; without the GC it stays 1.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomCyclicGcReclaimTest,
	"Cathode.Dom.Event.CyclicGcReclaim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomCyclicGcReclaimTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	JSRuntime* Rt = JS_GetRuntime(Context);

	FDomNodeId NodeId = INDEX_NONE;
	{
		// Construct the cycle, then release every external handle so ONLY the cycle holds the wrapper.
		FJsValue NodeW = Doc->CreateElement(TEXT("div"));
		FDomNode* Node = Doc->NodeFromWrapper(NodeW);
		if (!Node) { AddError(TEXT("node wrapper did not resolve to a native node")); return false; }
		NodeId = Node->GetNodeId();

		// JS back-edge: listener.node = the node's wrapper (a dup, consumed by JS_SetPropertyStr). Now
		// node-wrapper -> native -> listener (via gc_mark) and listener -> node-wrapper (via the property) = a
		// refcount cycle. The node natively holds the listener (AddEventListener moves it in), so the listener
		// has no external handle either.
		FJsValue Listener(Context, Eval(Context, "(function(){})"));
		JS_SetPropertyStr(Context, Listener.GetValue(), "node", JS_DupValue(Context, NodeW.GetValue()));
		Node->AddEventListener(TEXT("click"), MoveTemp(Listener));
	} // NodeW (and the moved-in Listener handle) destruct here -> the only thing keeping the wrapper alive is the cycle

	// Without a GC: a refcount cycle CANNOT be reclaimed by refcounting -> the node is still live in the index.
	const bool bLiveBeforeGc = Doc->FindNode(NodeId) != nullptr;
	TestTrue(TEXT("before GC the node<->listener cycle is NOT reclaimed (refcount cycle leaks)"), bLiveBeforeGc);

	// THE REAL PRIMITIVE: a single real cycle-collecting GC.
	JS_RunGC(Rt);

	// After the real GC: the cycle is collected -> the wrapper is finalized -> ~FDomNode -> UnregisterNode ->
	// the node leaves the non-owning index.
	const bool bReclaimedAfterGc = Doc->FindNode(NodeId) == nullptr;
	TestTrue(TEXT("after JS_RunGC the cycle is reclaimed (node dropped from the index)"), bReclaimedAfterGc);

	// The measured live-object DROP (1 -> 0 across the real GC) is the discharge — real reclaim, not gc_mark
	// visibility. checked = 1 cycle reclaimed.
	TestTrue(TEXT("measured cyclic reclaim: live-node count dropped 1->0 across the real JS_RunGC"),
		bLiveBeforeGc && bReclaimedAfterGc);

	return true;
}

// Sprint 012 slice 1 — the single binding-definition mechanism, criterion 2 (the first members REALLY work) +
// a criterion-1 structural check (install <-> enumerate from the ONE table).
//
// Drives the REAL installed members through JS member access (the wrapper's own/proto bindings) and asserts the
// marshaled value == FDomNode's OWN return (not a hand-typed constant). getAttribute (method, string->string|null)
// + tagName (property getter). Then asserts BOTH members are present on the real proto (install side, via
// JS_GetOwnPropertyNames) AND in the descriptor (enumerate side, GetDomNodeDescriptorJson) — both from the one
// FBoundMember table. (The full faculty lockstep + the discriminating omit-case is the next step.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomMemberBindingTest,
	"Cathode.Dom.Binding.MemberRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomMemberBindingTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout); // installs the bound members
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	FJsValue NodeW = Doc->CreateElement(TEXT("div"));
	FDomNode* Node = Doc->NodeFromWrapper(NodeW);
	Node->SetAttribute(TEXT("data-x"), TEXT("hello"));

	// Expose the wrapper so member access goes through the REAL installed bindings (not a back-door C++ call).
	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__node", JS_DupValue(Context, NodeW.GetValue()));
	JS_FreeValue(Context, Global);

	// CRITERION 2 — getAttribute (method): JS call -> thunk -> FDomNode::GetAttribute -> marshaled return,
	// asserted == the primitive's OWN output.
	{
		JSValue Result = Eval(Context, "__node.getAttribute('data-x')");
		FString Got;
		const bool bOk = UnmarshalString(Context, Result, Got);
		JS_FreeValue(Context, Result);
		FString NativeOut;
		Node->GetAttribute(TEXT("data-x"), NativeOut); // the real primitive's own output
		TestTrue(TEXT("getAttribute returned a JS string"), bOk);
		TestEqual(TEXT("node.getAttribute(JS) == FDomNode::GetAttribute output"), Got, NativeOut);
	}
	// absent attribute -> JS null (the thunk's optional-return discipline).
	{
		JSValue Result = Eval(Context, "__node.getAttribute('missing')");
		TestTrue(TEXT("absent attribute reads JS null"), JS_IsNull(Result));
		JS_FreeValue(Context, Result);
	}
	// CRITERION 2 — tagName (property getter): JS property access -> getter thunk -> FDomNode::GetTagName.
	{
		JSValue Result = Eval(Context, "__node.tagName");
		FString Tag;
		UnmarshalString(Context, Result, Tag);
		JS_FreeValue(Context, Result);
		TestEqual(TEXT("node.tagName(JS) == FDomNode::GetTagName output"), Tag, Node->GetTagName());
	}

	// CRITERION 1 (structural) — both members are on the REAL proto (install side, JS_GetOwnPropertyNames via JS)
	// AND in the descriptor (enumerate side) — from the one FBoundMember table.
	{
		JSValue Result = Eval(Context, "Object.getOwnPropertyNames(Object.getPrototypeOf(__node)).join(',')");
		FString ProtoMembers;
		UnmarshalString(Context, Result, ProtoMembers);
		JS_FreeValue(Context, Result);
		const FString Descriptor = GetDomNodeDescriptorJson();
		TestTrue(TEXT("getAttribute is installed on the real proto"), ProtoMembers.Contains(TEXT("getAttribute")));
		TestTrue(TEXT("tagName is installed on the real proto"), ProtoMembers.Contains(TEXT("tagName")));
		TestTrue(TEXT("getAttribute is in the descriptor"), Descriptor.Contains(TEXT("\"name\":\"getAttribute\"")));
		TestTrue(TEXT("tagName is in the descriptor"), Descriptor.Contains(TEXT("\"name\":\"tagName\"")));
	}

	return true;
}

// Sprint 012 slice 2 — the FULL node-core surface. Each member exercised through the REAL installed binding
// (JS member access -> thunk -> FDomNode) against the primitive's OWN output: === identity on node-returning
// paths (incl the DETACHED parentNode case the borrowed back-ref makes complete), real-state mutator read-backs
// (the parent's ACTUAL children / the real attribute), nodeType/nodeName/data, and the childNodes NodeList-typed
// snapshot (spec surface, NO array superset).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomNodeCoreTest,
	"Cathode.Dom.Binding.NodeCore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomNodeCoreTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	// __p is a DETACHED subtree root (created, never appended to the document root) — so child.parentNode === __p
	// exercises exactly the Option-A detached case (unresolvable under the parent-scan; complete via the borrow).
	FJsValue ParentW = Doc->CreateElement(TEXT("div"));
	FJsValue Child1W = Doc->CreateElement(TEXT("span"));
	FJsValue Child2W = Doc->CreateElement(TEXT("p"));
	FJsValue TextW = Doc->CreateTextNode(TEXT("t0"));
	FDomNode* Parent = Doc->NodeFromWrapper(ParentW);
	FDomNode* Child1 = Doc->NodeFromWrapper(Child1W);
	FDomNode* Child2 = Doc->NodeFromWrapper(Child2W);

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__p", JS_DupValue(Context, ParentW.GetValue()));
	JS_SetPropertyStr(Context, Global, "__c1", JS_DupValue(Context, Child1W.GetValue()));
	JS_SetPropertyStr(Context, Global, "__c2", JS_DupValue(Context, Child2W.GetValue()));
	JS_SetPropertyStr(Context, Global, "__t", JS_DupValue(Context, TextW.GetValue()));
	JS_FreeValue(Context, Global);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { AddError(FString::Printf(TEXT("JS threw evaluating: %s"), ANSI_TO_TCHAR(Expr))); }
		bool bValue = false;
		const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};
	auto NativeChildIds = [&](FDomNode* N) -> TArray<FDomNodeId> { TArray<FDomNodeId> Ids; N->GetChildNodes(Ids); return Ids; };

	// appendChild — === identity + real-state read-back (the parent's ACTUAL children now contain the child).
	TestTrue(TEXT("appendChild(c1) === c1 (identity)"), EvalBool("__p.appendChild(__c1) === __c1"));
	TestTrue(TEXT("appendChild: parent's REAL children contain c1"), NativeChildIds(Parent).Contains(Child1->GetNodeId()));

	// childNodes — NodeList-typed snapshot: identity, spec surface, NO array superset, snapshot depth.
	TestTrue(TEXT("childNodes is a NodeList (instanceof)"), EvalBool("__p.childNodes instanceof NodeList"));
	TestTrue(TEXT("childNodes.length === 1"), EvalBool("__p.childNodes.length === 1"));
	TestTrue(TEXT("childNodes[0] === c1 (identity)"), EvalBool("__p.childNodes[0] === __c1"));
	TestTrue(TEXT("firstChild === c1 === childNodes[0] (one wrapper)"), EvalBool("__p.firstChild === __c1 && __p.firstChild === __p.childNodes[0]"));
	TestTrue(TEXT("childNodes spec surface: item(0) === c1"), EvalBool("typeof __p.childNodes.item === 'function' && __p.childNodes.item(0) === __c1"));
	TestTrue(TEXT("childNodes has NO array superset (map/filter/push undefined)"), EvalBool("__p.childNodes.map === undefined && __p.childNodes.filter === undefined && __p.childNodes.push === undefined"));
	TestTrue(TEXT("childNodes.forEach walks the snapshot once"), EvalBool("(function(){var n=0,last=null;__p.childNodes.forEach(function(x){n++;last=x;});return n===1&&last===__c1;})()"));
	TestTrue(TEXT("childNodes is a SNAPSHOT (a captured list is unaffected by a later append)"),
		EvalBool("(function(){var snap=__p.childNodes;var before=snap.length;__p.appendChild(__c2);var after=snap.length;__p.removeChild(__c2);return before===1&&after===1;})()"));

	// insertBefore — === identity + real-state order [c2, c1].
	TestTrue(TEXT("insertBefore(c2, c1) === c2 (identity)"), EvalBool("__p.insertBefore(__c2, __c1) === __c2"));
	TestTrue(TEXT("insertBefore positions c2 before c1 (JS)"), EvalBool("__p.childNodes.length === 2 && __p.childNodes[0] === __c2 && __p.childNodes[1] === __c1"));
	{
		const TArray<FDomNodeId> Ids = NativeChildIds(Parent);
		TestTrue(TEXT("insertBefore: REAL child order is [c2, c1]"),
			Ids.Num() == 2 && Ids[0] == Child2->GetNodeId() && Ids[1] == Child1->GetNodeId());
	}

	// parentNode / nextSibling — === identity, incl the DETACHED parentNode case (__p is never attached to root).
	TestTrue(TEXT("c1.parentNode === p — the DETACHED case (Option A)"), EvalBool("__c1.parentNode === __p"));
	TestTrue(TEXT("c2.nextSibling === c1 (identity)"), EvalBool("__c2.nextSibling === __c1"));
	TestTrue(TEXT("c1.nextSibling === null (last child)"), EvalBool("__c1.nextSibling === null"));

	// removeChild — === identity + detach (real-state).
	TestTrue(TEXT("removeChild(c2) === c2 (identity)"), EvalBool("__p.removeChild(__c2) === __c2"));
	TestTrue(TEXT("removed c2.parentNode === null"), EvalBool("__c2.parentNode === null"));
	TestTrue(TEXT("removeChild: parent's REAL children no longer contain c2"), !NativeChildIds(Parent).Contains(Child2->GetNodeId()));

	// remove() — self-detach (real-state): parent ends empty.
	TestTrue(TEXT("c1.remove() detaches: c1.parentNode === null"), EvalBool("(function(){__c1.remove();return __c1.parentNode===null;})()"));
	TestTrue(TEXT("remove: parent now has no children (REAL state)"), NativeChildIds(Parent).Num() == 0);

	// setAttribute / removeAttribute — real-state read-back (getAttribute + the REAL attribute map).
	TestTrue(TEXT("setAttribute then getAttribute reads it back"), EvalBool("(function(){__p.setAttribute('id','x');return __p.getAttribute('id')==='x';})()"));
	{
		FString V; Parent->GetAttribute(TEXT("id"), V);
		TestEqual(TEXT("setAttribute: the REAL attribute value"), V, FString(TEXT("x")));
	}
	TestTrue(TEXT("removeAttribute then getAttribute === null"), EvalBool("(function(){__p.removeAttribute('id');return __p.getAttribute('id')===null;})()"));

	// nodeType / nodeName.
	TestTrue(TEXT("element nodeType === 1"), EvalBool("__p.nodeType === 1"));
	TestTrue(TEXT("text nodeType === 3"), EvalBool("__t.nodeType === 3"));
	TestTrue(TEXT("text nodeName === '#text'"), EvalBool("__t.nodeName === '#text'"));
	TestTrue(TEXT("element nodeName === tagName"), EvalBool("__p.nodeName === __p.tagName"));

	// data / nodeValue (Text) — round-trip + the REAL character data.
	TestTrue(TEXT("text.data round-trips (JS)"), EvalBool("(function(){__t.data='hi';return __t.data==='hi';})()"));
	TestEqual(TEXT("text.data: the REAL character data"), Doc->NodeFromWrapper(TextW)->GetData(), FString(TEXT("hi")));
	TestTrue(TEXT("text nodeValue reflects data; element nodeValue === null"), EvalBool("__t.nodeValue === 'hi' && __p.nodeValue === null"));
	TestTrue(TEXT("text nodeValue set updates data"), EvalBool("(function(){__t.nodeValue='bye';return __t.data==='bye';})()"));

	// textContent set/get.
	TestTrue(TEXT("textContent set then get"), EvalBool("(function(){__p.textContent='words';return __p.textContent==='words';})()"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
