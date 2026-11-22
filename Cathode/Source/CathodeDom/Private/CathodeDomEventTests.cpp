#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Specs for the event system: listener hold + gc_mark visibility (the node<->listener cycle
// hazard) + removeEventListener reference-drop; dispatch bubble ORDER; stopPropagation / preventDefault. Drive
// the C++ dispatch algorithm directly with a RECORDING invoker (the FDomEvent -> JS Event object marshaling is
// covered by the JS-binding dispatch tests below). Assert OUR bookkeeping (mark count, CurrentTarget order, flags).
// gc_mark VISIBILITY (the listener is marked) is proved by FCathodeDomListenerTest. The actual cyclic RECLAIM
// — a real JS_RunGC freeing a node<->listener reference cycle — is proved below
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
	// actual cyclic reclaim needs a real JS_RunGC -> FCathodeDomCyclicGcReclaimTest.
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

// CYCLIC JS_RunGC RECLAIM.
//
// Builds a real node<->listener reference CYCLE entirely on the C++/test-seam path (NOT via uninstalled DOM JS
// globals — the bare runtime has no document): the node's native gc_mark reports the listener (node -> listener
// edge), and a JS back-edge (listener.node = the node's wrapper) closes it (listener -> node-wrapper). With all
// external handles dropped, the cycle's refcounts never reach zero, so refcounting ALONE cannot reclaim it.
//
// The ORACLE is a MEASURED live-object DROP across a REAL JS_RunGC — distinct from gc_mark VISIBILITY
// (FCathodeDomListenerTest). The non-owning, wrapper-graph-governed node index (FindNode; ~FDomNode ->
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

// The single binding-definition mechanism: the bound members REALLY work +
// a structural check (install <-> enumerate from the ONE table).
//
// Drives the REAL installed members through JS member access (the wrapper's own/proto bindings) and asserts the
// marshaled value == FDomNode's OWN return (not a hand-typed constant). getAttribute (method, string->string|null)
// + tagName (property getter). Then asserts BOTH members are present on the real proto (install side, via
// JS_GetOwnPropertyNames) AND in the descriptor (enumerate side, GetDomNodeDescriptorJson) — both from the one
// FBoundMember table.
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

// The FULL node-core surface. Each member exercised through the REAL installed binding
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

// Reflected properties id + className (name-in-dom routing). Each
// reflects the attribute store live (getter reads / setter writes the attribute, NO backing field) so the JS
// property can never desync from setAttribute/getAttribute. className reflects the 'class' attribute (the name
// translation), proven both directions. Reflected-DOMString semantics: an absent attribute reflects as "".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomReflectedPropTest,
	"Cathode.Dom.Binding.ReflectedProps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomReflectedPropTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	FJsValue ElW = Doc->CreateElement(TEXT("div"));
	FDomNode* El = Doc->NodeFromWrapper(ElW);

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__el", JS_DupValue(Context, ElW.GetValue()));
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
	auto NativeAttr = [&](const TCHAR* Name) -> FString { FString V; El->GetAttribute(Name, V); return V; };

	// Reflected-DOMString: an ABSENT attribute reflects as "" (a string, NOT null) — the reflected/getAttribute
	// distinction. Checked on the fresh element BEFORE any id is set.
	TestTrue(TEXT("absent id reflects as '' (empty string, not null)"), EvalBool("__el.id === '' && typeof __el.id === 'string'"));

	// Name-in-dom precondition + the translation is REAL: id/className ARE bound JS properties; 'class' is
	// NOT (it is the attribute name, reached only via getAttribute/setAttribute).
	TestTrue(TEXT("'id' in el === true (name-in-dom routing precondition)"), EvalBool("('id' in __el) === true"));
	TestTrue(TEXT("'className' in el === true (name-in-dom routing precondition)"), EvalBool("('className' in __el) === true"));
	TestTrue(TEXT("'class' in el === false (class is the attribute, not a bound JS property)"), EvalBool("('class' in __el) === false"));

	// id <-> 'id' attribute, both directions, asserted live against FDomNode GetAttribute.
	TestTrue(TEXT("el.id='x' -> el.id==='x' AND getAttribute('id')==='x'"), EvalBool("(function(){__el.id='x';return __el.id==='x' && __el.getAttribute('id')==='x';})()"));
	TestEqual(TEXT("el.id='x' wrote the REAL 'id' attribute"), NativeAttr(TEXT("id")), FString(TEXT("x")));
	TestTrue(TEXT("setAttribute('id','y') -> el.id==='y' (property reflects the attribute write)"), EvalBool("(function(){__el.setAttribute('id','y');return __el.id==='y';})()"));

	// className <-> 'class' attribute (the NAME TRANSLATION), both directions.
	TestTrue(TEXT("el.className='a b' -> getAttribute('class')==='a b' (name translation)"), EvalBool("(function(){__el.className='a b';return __el.getAttribute('class')==='a b' && __el.className==='a b';})()"));
	TestEqual(TEXT("el.className='a b' wrote the REAL 'class' attribute"), NativeAttr(TEXT("class")), FString(TEXT("a b")));
	TestTrue(TEXT("setAttribute('class','c') -> el.className==='c' (property reflects the attribute write)"), EvalBool("(function(){__el.setAttribute('class','c');return __el.className==='c';})()"));

	return true;
}

// =====================================================================================================================
// Events (the event mechanism) — DISPATCH-CORRECTNESS through the REAL JS bindings.
//
// The tests above drive the C++ DispatchEvent algorithm with a recording INVOKER (no marshaling).
// These tests drive the WHOLE path: real JS listeners installed via the bound addEventListener, fired by a
// C++-synthetic keydown (the production DispatchKeyboardEvent that builds the LIVE shared Event object), asserting
// the marshaled Event surface (type/target/currentTarget/eventPhase/key/code/isExtensible) and the exact ordering.
// =====================================================================================================================

// PROOF 1 — exact firing order + the live-shared, extensible Event object. A 3-level tree (gp > p > target);
// capture + bubble listeners at every level; AtTarget has TWO listeners (registration order). One synthetic keydown
// at the target. Asserts: order = [gp-cap, p-cap, t1, t2, p-bub, gp-bub]; currentTarget ADVANCES per node (===
// identity); target STAYS the dispatch target; eventPhase tracks 1/2/3; e.key/e.code marshal; isExtensible===true.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomEventJsDispatchTest,
	"Cathode.Dom.Event.JsDispatchOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomEventJsDispatchTest::RunTest(const FString& Parameters)
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
	FJsValue TW = Doc->CreateElement(TEXT("target"));
	FDomNode* Gp = Doc->NodeFromWrapper(GpW);
	FDomNode* P = Doc->NodeFromWrapper(PW);
	FDomNode* T = Doc->NodeFromWrapper(TW);
	Gp->AppendChild(PW);
	P->AppendChild(TW);

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__gp", JS_DupValue(Context, GpW.GetValue()));
	JS_SetPropertyStr(Context, Global, "__p", JS_DupValue(Context, PW.GetValue()));
	JS_SetPropertyStr(Context, Global, "__t", JS_DupValue(Context, TW.GetValue()));
	JS_FreeValue(Context, Global);

	// Each listener records its tag (order) + a SNAPSHOT of the live event read THROUGH the bound getters.
	const char* Setup =
		"globalThis.order = [];"
		"globalThis.cap = {};"
		"function rec(tag){ return function(e){"
		"  globalThis.order.push(tag);"
		"  globalThis.cap[tag] = { ctIsGp: e.currentTarget===__gp, ctIsP: e.currentTarget===__p,"
		"    ctIsT: e.currentTarget===__t, tgtIsT: e.target===__t, phase: e.eventPhase, type: e.type,"
		"    key: e.key, code: e.code, ext: Object.isExtensible(e) };"
		"};}"
		"__gp.addEventListener('keydown', rec('gp-cap'), true);"
		"__p.addEventListener('keydown', rec('p-cap'), true);"
		"__t.addEventListener('keydown', rec('t1'), false);"
		"__t.addEventListener('keydown', rec('t2'), false);"
		"__p.addEventListener('keydown', rec('p-bub'), false);"
		"__gp.addEventListener('keydown', rec('gp-bub'), false);";
	{
		JSValue R = Eval(Context, Setup);
		const bool bThrew = JS_IsException(R);
		JS_FreeValue(Context, R);
		if (bThrew) { AddError(TEXT("listener setup threw")); return false; }
	}

	// THE PRIMITIVE: a C++-synthetic keydown at the target (production KeyboardEvent path → live Event object).
	FDomKeyboardEvent Kbd;
	Kbd.Event.Type = TEXT("keydown");
	Kbd.Event.bBubbles = true;
	Kbd.Key = TEXT("Enter");
	Kbd.Code = TEXT("Enter");
	T->DispatchKeyboardEvent(Kbd);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { AddError(FString::Printf(TEXT("JS threw evaluating: %s"), ANSI_TO_TCHAR(Expr))); }
		bool bValue = false;
		const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};
	auto EvalStr = [&](const char* Expr) -> FString
	{
		JSValue R = Eval(Context, Expr);
		FString S;
		UnmarshalString(Context, R, S);
		JS_FreeValue(Context, R);
		return S;
	};

	// EXACT phase order: capture top-down (gp,p) → AtTarget reg order (t1,t2) → bubble bottom-up (p,gp).
	TestEqual(TEXT("exact firing order across capture/AtTarget/bubble"),
		EvalStr("globalThis.order.join(',')"), FString(TEXT("gp-cap,p-cap,t1,t2,p-bub,gp-bub")));

	// currentTarget ADVANCES per node (=== identity), target STAYS the dispatch target.
	TestTrue(TEXT("gp capture: currentTarget === gp"), EvalBool("globalThis.cap['gp-cap'].ctIsGp"));
	TestTrue(TEXT("target: currentTarget === target"), EvalBool("globalThis.cap['t1'].ctIsT"));
	TestTrue(TEXT("p bubble: currentTarget === p"), EvalBool("globalThis.cap['p-bub'].ctIsP"));
	TestTrue(TEXT("target stays the target at gp capture"), EvalBool("globalThis.cap['gp-cap'].tgtIsT"));
	TestTrue(TEXT("target stays the target at gp bubble"), EvalBool("globalThis.cap['gp-bub'].tgtIsT"));

	// eventPhase tracks 1 (capturing) / 2 (AtTarget) / 3 (bubbling).
	TestTrue(TEXT("eventPhase===1 (capturing) at gp capture"), EvalBool("globalThis.cap['gp-cap'].phase===1"));
	TestTrue(TEXT("eventPhase===2 (AtTarget) at target"), EvalBool("globalThis.cap['t1'].phase===2"));
	TestTrue(TEXT("eventPhase===3 (bubbling) at gp bubble"), EvalBool("globalThis.cap['gp-bub'].phase===3"));

	// type + KeyboardEvent key/code marshal; the event object is EXTENSIBLE (a frozen event would REJECT).
	TestTrue(TEXT("e.type === 'keydown'"), EvalBool("globalThis.cap['t1'].type==='keydown'"));
	TestTrue(TEXT("e.key === 'Enter' (KeyboardEvent)"), EvalBool("globalThis.cap['t1'].key==='Enter'"));
	TestTrue(TEXT("e.code === 'Enter' (KeyboardEvent)"), EvalBool("globalThis.cap['t1'].code==='Enter'"));
	TestTrue(TEXT("Object.isExtensible(event) === true (not frozen)"), EvalBool("globalThis.cap['t1'].ext===true"));

	return true;
}

// PROOF 2 — LIVE-SHARED stopPropagation (same-node-remaining) + preventDefault, all via the bound methods. The
// first AtTarget listener calls e.stopPropagation() + e.preventDefault(); the SECOND same-node listener STILL
// fires and reads e.defaultPrevented===true back off the SAME live object (live-shared); the bubble phase does
// NOT fire (ascent halted). The C++ event records bPropagationStopped + bDefaultPrevented after dispatch.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomEventJsStopPreventTest,
	"Cathode.Dom.Event.JsStopAndPrevent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomEventJsStopPreventTest::RunTest(const FString& Parameters)
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
	FJsValue TW = Doc->CreateElement(TEXT("target"));
	FDomNode* Gp = Doc->NodeFromWrapper(GpW);
	FDomNode* P = Doc->NodeFromWrapper(PW);
	FDomNode* T = Doc->NodeFromWrapper(TW);
	Gp->AppendChild(PW);
	P->AppendChild(TW);

	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__gp", JS_DupValue(Context, GpW.GetValue()));
	JS_SetPropertyStr(Context, Global, "__p", JS_DupValue(Context, PW.GetValue()));
	JS_SetPropertyStr(Context, Global, "__t", JS_DupValue(Context, TW.GetValue()));
	JS_FreeValue(Context, Global);

	const char* Setup =
		"globalThis.order = [];"
		"globalThis.dpSeenByT2 = null;"
		"__gp.addEventListener('keydown', function(e){ globalThis.order.push('gp-cap'); }, true);"
		"__p.addEventListener('keydown', function(e){ globalThis.order.push('p-cap'); }, true);"
		"__t.addEventListener('keydown', function(e){ globalThis.order.push('t1'); e.stopPropagation(); e.preventDefault(); }, false);"
		"__t.addEventListener('keydown', function(e){ globalThis.order.push('t2'); globalThis.dpSeenByT2 = e.defaultPrevented; }, false);"
		"__p.addEventListener('keydown', function(e){ globalThis.order.push('p-bub'); }, false);"
		"__gp.addEventListener('keydown', function(e){ globalThis.order.push('gp-bub'); }, false);";
	{
		JSValue R = Eval(Context, Setup);
		const bool bThrew = JS_IsException(R);
		JS_FreeValue(Context, R);
		if (bThrew) { AddError(TEXT("listener setup threw")); return false; }
	}

	FDomKeyboardEvent Kbd;
	Kbd.Event.Type = TEXT("keydown");
	Kbd.Event.bBubbles = true;
	Kbd.Key = TEXT("Enter");
	Kbd.Code = TEXT("Enter");
	T->DispatchKeyboardEvent(Kbd);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { AddError(FString::Printf(TEXT("JS threw evaluating: %s"), ANSI_TO_TCHAR(Expr))); }
		bool bValue = false;
		const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};
	auto EvalStr = [&](const char* Expr) -> FString
	{
		JSValue R = Eval(Context, Expr);
		FString S;
		UnmarshalString(Context, R, S);
		JS_FreeValue(Context, R);
		return S;
	};

	// stopPropagation halts the ASCENT but NOT the same-node remaining listener: capture (gp,p) + AtTarget (t1,t2),
	// then NO bubble. t2 (same node, after t1's stopPropagation) STILL fires.
	TestEqual(TEXT("stopPropagation halts ascent; same-node remaining still fires"),
		EvalStr("globalThis.order.join(',')"), FString(TEXT("gp-cap,p-cap,t1,t2")));

	// LIVE-SHARED: t2 read defaultPrevented===true back off the SAME event object t1 mutated.
	TestTrue(TEXT("t2 sees defaultPrevented===true off the live shared event"), EvalBool("globalThis.dpSeenByT2===true"));

	// The C++ event records both flags after dispatch.
	TestTrue(TEXT("C++ event: bPropagationStopped after stopPropagation()"), Kbd.Event.bPropagationStopped);
	TestTrue(TEXT("C++ event: bDefaultPrevented after preventDefault()"), Kbd.Event.bDefaultPrevented);

	return true;
}

// PROOF 3 — removeEventListener IDENTITY (all 4 cases) + DOM dedup, all via the bound add/remove. The identity key
// is the (type, fn-object, capture) triple. Each sub-case uses a FRESH element so listener sets never bleed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomEventJsIdentityTest,
	"Cathode.Dom.Event.RemoveListenerIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomEventJsIdentityTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { AddError(FString::Printf(TEXT("JS threw evaluating: %s"), ANSI_TO_TCHAR(Expr))); }
		bool bValue = false;
		const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// Each case: a fresh element exposed at the given global; run the add/remove JS; fire a synthetic keydown of
	// the given type; read the resulting count off globalThis.
	auto RunCase = [&](const TCHAR* Label, const char* GlobalName, const char* SetupJs, const TCHAR* FireType,
		const char* CountExpr)
	{
		FJsValue ElW = Doc->CreateElement(TEXT("div"));
		FDomNode* El = Doc->NodeFromWrapper(ElW);
		JSValue G = JS_GetGlobalObject(Context);
		JS_SetPropertyStr(Context, G, GlobalName, JS_DupValue(Context, ElW.GetValue()));
		JS_FreeValue(Context, G);
		{
			JSValue R = Eval(Context, SetupJs);
			if (JS_IsException(R)) { AddError(FString::Printf(TEXT("%s: setup threw"), Label)); }
			JS_FreeValue(Context, R);
		}
		FDomKeyboardEvent Kbd;
		Kbd.Event.Type = FireType;
		Kbd.Event.bBubbles = false;
		Kbd.Key = TEXT("Enter");
		Kbd.Code = TEXT("Enter");
		El->DispatchKeyboardEvent(Kbd);
		TestTrue(Label, EvalBool(CountExpr)); // the expected count is encoded in CountExpr (=== N)
	};

	// CASE 1 (positive triple) — add F(type x, capture false), remove the SAME triple → F gone → 0 fires.
	RunCase(TEXT("positive: matching (type,fn,capture) removed → 0 fires"), "__e1",
		"globalThis.c1=0; var F1=function(){globalThis.c1++;};"
		"__e1.addEventListener('x', F1, false); __e1.removeEventListener('x', F1, false);",
		TEXT("x"), "globalThis.c1===0");

	// CASE 2 (capture mismatch) — added with capture=false, removed with capture=TRUE → NO-OP → 1 fire.
	RunCase(TEXT("capture mismatch: removeEventListener is a NO-OP → 1 fire"), "__e2",
		"globalThis.c2=0; var F2=function(){globalThis.c2++;};"
		"__e2.addEventListener('x', F2, false); __e2.removeEventListener('x', F2, true);",
		TEXT("x"), "globalThis.c2===1");

	// CASE 3 (type mismatch) — added on 'x', removed on 'y' → NO-OP → 1 fire on 'x'.
	RunCase(TEXT("type mismatch: removeEventListener is a NO-OP → 1 fire"), "__e3",
		"globalThis.c3=0; var F3=function(){globalThis.c3++;};"
		"__e3.addEventListener('x', F3, false); __e3.removeEventListener('y', F3, false);",
		TEXT("x"), "globalThis.c3===1");

	// CASE 4 (fn mismatch) — added F, removed a DIFFERENT fn G (same type+capture) → NO-OP → F fires → 1.
	RunCase(TEXT("fn mismatch: removeEventListener is a NO-OP → 1 fire"), "__e4",
		"globalThis.c4=0; var F4=function(){globalThis.c4++;}; var G4=function(){};"
		"__e4.addEventListener('x', F4, false); __e4.removeEventListener('x', G4, false);",
		TEXT("x"), "globalThis.c4===1");

	// DEDUP — the SAME (type,fn,capture) triple added TWICE registers ONCE → 1 fire (DOM dedup).
	RunCase(TEXT("dedup: same triple added twice fires ONCE"), "__e5",
		"globalThis.c5=0; var F5=function(){globalThis.c5++;};"
		"__e5.addEventListener('x', F5, false); __e5.addEventListener('x', F5, false);",
		TEXT("x"), "globalThis.c5===1");

	// DISTINCT-CAPTURE NOT DEDUP — same (type,fn) but DIFFERENT capture are TWO listeners → both fire (one in
	// capture phase, one in bubble) → 2. (Guards that dedup keys on capture too, not just type+fn.)
	RunCase(TEXT("distinct capture is NOT dedup: both fire → 2"), "__e6",
		"globalThis.c6=0; var F6=function(){globalThis.c6++;};"
		"__e6.addEventListener('x', F6, true); __e6.addEventListener('x', F6, false);",
		TEXT("x"), "globalThis.c6===2");

	return true;
}

// PROOF 4a (gc_mark) — a REAL JS listener installed via addEventListener that CLOSES OVER its own node
// wrapper forms the node↔listener cycle ENTIRELY through the bound API (distinct from the C++-seam JS_SetPropertyStr
// cycle in FCathodeDomCyclicGcReclaimTest). The node is standalone (NOT in the document graph),
// so once every external handle is dropped only the cycle holds the wrapper; a single real JS_RunGC RECLAIMS it
// (FindNode 1→0). This is the node gc_mark reporting the listener edge → the cycle collector frees the cycle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomEventJsClosureReclaimTest,
	"Cathode.Dom.Event.JsListenerClosureReclaim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomEventJsClosureReclaimTest::RunTest(const FString& Parameters)
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
		FJsValue NodeW = Doc->CreateElement(TEXT("div"));
		FDomNode* Node = Doc->NodeFromWrapper(NodeW);
		if (!Node) { AddError(TEXT("node wrapper did not resolve to a native node")); return false; }
		NodeId = Node->GetNodeId();

		JSValue Global = JS_GetGlobalObject(Context);
		JS_SetPropertyStr(Context, Global, "__node", JS_DupValue(Context, NodeW.GetValue()));
		JS_FreeValue(Context, Global);

		// The listener CLOSES OVER the node wrapper (var n = __node) and is installed via the REAL addEventListener
		// (the node natively holds it). Then drop the only external handle (globalThis.__node) — the cycle
		// node-wrapper → native → listener → closure(n) → node-wrapper is all that remains.
		JSValue R = Eval(Context,
			"(function(){ var n = __node; __node.addEventListener('click', function(e){ return n; }, false); })();"
			"delete globalThis.__node;");
		const bool bThrew = JS_IsException(R);
		JS_FreeValue(Context, R);
		if (bThrew) { AddError(TEXT("closure listener setup threw")); return false; }
	} // NodeW destructs here → only the cycle holds the wrapper

	const bool bLiveBeforeGc = Doc->FindNode(NodeId) != nullptr;
	TestTrue(TEXT("before GC the addEventListener-closure cycle leaks (refcount cycle)"), bLiveBeforeGc);

	JS_RunGC(Rt);

	const bool bReclaimedAfterGc = Doc->FindNode(NodeId) == nullptr;
	TestTrue(TEXT("after JS_RunGC the addEventListener-closure cycle is RECLAIMED (node dropped 1→0)"), bReclaimedAfterGc);
	TestTrue(TEXT("measured reclaim across the real GC (distinct from T4 gc_mark visibility)"),
		bLiveBeforeGc && bReclaimedAfterGc);

	return true;
}

// PROOF 4b (gc_mark) — the SAFE branch that proves document.gc_mark STAYS ABSENT. A node appended to the
// document graph carries a listener that CLOSES OVER globalThis.document (the listener→document-wrapper cycle the
// event API makes formable). Dropping globalThis.document while the C++ FDomDocument stays alive does NOT collect
// the document: the C++-owning RootWrapper (null finalizer, NOT an in-graph gc_mark edge) pins the document graph,
// so the node SURVIVES the GC (FindNode != null) — benign over-retention, NO premature collection, NO UAF. This is
// EXACTLY the cycle that, were document.gc_mark (unsafely) re-added, would let the collector free the C++-owned root
// out from under the live RootWrapper. We assert the safe behavior; we do NOT re-add gc_mark. (COVERAGE.md:
// with the event API bound, re-adding it is UNSAFE and not needed.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomEventDocOverRetentionTest,
	"Cathode.Dom.Event.ListenerDocumentOverRetention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomEventDocOverRetentionTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	JSRuntime* Rt = JS_GetRuntime(Context);

	Doc->InstallAsGlobal(); // globalThis.document = the doc wrapper (RootWrapper-owned, null finalizer)

	FDomNodeId NodeId = INDEX_NONE;
	{
		FJsValue NodeW = Doc->CreateElement(TEXT("div"));
		FDomNode* Node = Doc->NodeFromWrapper(NodeW);
		if (!Node) { AddError(TEXT("node wrapper did not resolve to a native node")); return false; }
		NodeId = Node->GetNodeId();

		// Append to the document root → the C++ RootWrapper graph reaches the node (the over-retention anchor).
		FDomNode* Root = Doc->FindNode(Doc->GetRootNodeId());
		if (!Root) { AddError(TEXT("document root did not resolve")); return false; }
		Root->AppendChild(NodeW);

		JSValue Global = JS_GetGlobalObject(Context);
		JS_SetPropertyStr(Context, Global, "__node", JS_DupValue(Context, NodeW.GetValue()));
		JS_FreeValue(Context, Global);

		// Listener closes over globalThis.document → a listener→document-wrapper cycle. Drop BOTH JS handles
		// (globalThis.document + globalThis.__node) while the C++ FDomDocument (Doc) stays alive.
		JSValue R = Eval(Context,
			"(function(){ var d = globalThis.document; __node.addEventListener('click', function(e){ return d; }, false); })();"
			"delete globalThis.document; delete globalThis.__node;");
		const bool bThrew = JS_IsException(R);
		JS_FreeValue(Context, R);
		if (bThrew) { AddError(TEXT("document-closure listener setup threw")); return false; }
	}

	JS_RunGC(Rt);

	// SAFE BRANCH: document.gc_mark is ABSENT → the collector does NOT treat the C++-owned root as collectible →
	// the RootWrapper pins the document graph → the node SURVIVES (no premature collection, no UAF).
	const bool bSurvives = Doc->FindNode(NodeId) != nullptr;
	TestTrue(TEXT("listener→document cycle is benignly OVER-RETAINED across GC (root survives, no UAF)"), bSurvives);

	return true;
}

// THE DOCUMENT-BUBBLE PROOF: the document is an EventTarget at the TOP of the bubble path. A keydown dispatched
// at a CHILD bubbles UP to document.addEventListener('keydown') (the keyboard-input handshake target). Asserts: bubble order is
// child→document (the document fires LAST, after the tree), e.key reads off the LIVE event ('ArrowUp'), e.type==='keydown',
// and e.currentTarget === document (the document-as-currentTarget seam). A document that is not an EventTarget or not in
// the bubble path NEVER fires a document keydown listener -> FAILS.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomDocumentBubbleTargetTest,
	"Cathode.Dom.Event.DocumentBubbleTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomDocumentBubbleTargetTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	Doc->InstallAsGlobal(); // globalThis.document — so document.addEventListener resolves

	// A child appended to the document root → the dispatch target bubbles UP through the tree to the document.
	FJsValue ChildW = Doc->CreateElement(TEXT("div"));
	FDomNode* Child = Doc->NodeFromWrapper(ChildW);
	if (!Child) { AddError(TEXT("child wrapper did not resolve")); return false; }
	FDomNode* Root = Doc->FindNode(Doc->GetRootNodeId());
	if (!Root) { AddError(TEXT("document root did not resolve")); return false; }
	Root->AppendChild(ChildW);
	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__child", JS_DupValue(Context, ChildW.GetValue()));
	JS_FreeValue(Context, Global);

	// A bubble listener on the child + one on the document; the document one snapshots the live event.
	{
		JSValue R = Eval(Context,
			"globalThis.order = [];"
			"globalThis.cap = {};"
			"__child.addEventListener('keydown', function(e){ globalThis.order.push('child'); }, false);"
			"document.addEventListener('keydown', function(e){"
			"  globalThis.order.push('doc');"
			"  globalThis.cap.key = e.key; globalThis.cap.type = e.type;"
			"  globalThis.cap.ctIsDoc = (e.currentTarget === document);"
			"}, false);");
		const bool bThrew = JS_IsException(R);
		JS_FreeValue(Context, R);
		if (bThrew) { AddError(TEXT("document/child listener setup threw (document.addEventListener missing?)")); return false; }
	}

	// A host-synthetic keydown at the CHILD → the real capture/at-target/bubble walk → bubbles up to the document.
	FDomKeyboardEvent Kbd;
	Kbd.Event.Type = TEXT("keydown");
	Kbd.Event.bBubbles = true;
	Kbd.Key = TEXT("ArrowUp");
	Kbd.Code = TEXT("ArrowUp");
	Child->DispatchKeyboardEvent(Kbd);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { AddError(FString::Printf(TEXT("JS threw evaluating: %s"), ANSI_TO_TCHAR(Expr))); }
		bool bValue = false;
		const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};
	auto EvalStr = [&](const char* Expr) -> FString
	{
		JSValue R = Eval(Context, Expr);
		FString S;
		UnmarshalString(Context, R, S);
		JS_FreeValue(Context, R);
		return S;
	};

	TestEqual(TEXT("bubble order: the child fires, THEN the document at the bubble top"),
		EvalStr("globalThis.order.join(',')"), FString(TEXT("child,doc")));
	TestTrue(TEXT("document keydown listener fired with e.key === 'ArrowUp' (read off the live event)"),
		EvalBool("globalThis.cap.key === 'ArrowUp'"));
	TestTrue(TEXT("e.type === 'keydown' at the document"), EvalBool("globalThis.cap.type === 'keydown'"));
	TestTrue(TEXT("e.currentTarget === document at the document listener (the document-as-currentTarget seam)"),
		EvalBool("globalThis.cap.ctIsDoc === true"));

	return true;
}

// THE SAFE DOCUMENT-LISTENER INVARIANT.
// document.gc_mark STAYS nullptr — and that is SAFE precisely because the production document is ALWAYS rooted (globalThis
// .document, the per-surface InstallAsGlobal repoint). This proves THAT production invariant: a CYCLIC listener ON the document
// (the listener closes over the document wrapper) + a detached node N hung off it, while the document stays globalThis-rooted
// (the real production state), SURVIVES a real JS_RunGC with NO premature collection and NO UAF. The document holds the
// listener (C++ refcount) → the listener holds N → all reachable via the rooted wrapper → benignly retained, not garbage (the
// listeners are the app's live keydown handlers for the surface's lifetime).
//   WHY NOT a reclaim-direction test: reclaim would only happen under an artificial `delete globalThis.document` that
//   production never does — and re-adding a document gc_mark to make THAT reclaimable CRASHES with the
//   UAF COVERAGE.md describes (the null-finalizer document outlives its wrapper → reclaiming the cycle dangles its listener refs).
//   The node has a non-null finalizer that coordinates listener teardown; the document does not. See CathodeDom/COVERAGE.md.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomDocumentListenerOverRetentionTest,
	"Cathode.Dom.Event.DocumentListenerOverRetention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomDocumentListenerOverRetentionTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	JSRuntime* Rt = JS_GetRuntime(Context);
	Doc->InstallAsGlobal(); // globalThis.document = the wrapper — ROOTED (the production state; NEVER deleted here)

	FDomNodeId NodeId = INDEX_NONE;
	{
		// A DETACHED node N reachable ONLY via the document listener's closure (listener.node = N). With the document
		// rooted, the chain globalThis.document → (FDomDocument::Listeners) → listener → N keeps N alive.
		FJsValue NodeW = Doc->CreateElement(TEXT("div"));
		FDomNode* Node = Doc->NodeFromWrapper(NodeW);
		if (!Node) { AddError(TEXT("node wrapper did not resolve to a native node")); return false; }
		NodeId = Node->GetNodeId();

		// A listener ON THE DOCUMENT closing over the document wrapper (the cyclic shape) + holding N. The document natively
		// holds the listener (AddEventListener moves it in); the document stays globalThis-rooted, so nothing is collectable.
		JSValue Global = JS_GetGlobalObject(Context);
		JSValue DocW = JS_GetPropertyStr(Context, Global, "document"); // a dup, owned here
		JS_FreeValue(Context, Global);
		FJsValue Listener(Context, Eval(Context, "(function(){})"));
		JS_SetPropertyStr(Context, Listener.GetValue(), "doc", DocW); // closes over the document wrapper (the cyclic shape)
		JS_SetPropertyStr(Context, Listener.GetValue(), "node", JS_DupValue(Context, NodeW.GetValue()));
		Doc->AddEventListener(TEXT("click"), MoveTemp(Listener));
	} // external handles drop — N + the listener now live only via the (rooted) document's Listeners

	// The document stays globalThis-rooted (the production state) → a real cycle-collecting GC does NOT reclaim the cycle.
	JS_RunGC(Rt);

	// SAFE: the rooted document pins its Listeners (C++ refcount) → the listener pins N → N SURVIVES, no premature collection,
	// no UAF (document.gc_mark is nullptr; a reclaim would only matter under an artificial unroot, which production never does).
	const bool bSurvives = Doc->FindNode(NodeId) != nullptr;
	TestTrue(TEXT("a cyclic listener ON a ROOTED document is benignly OVER-RETAINED across GC (no premature collection, no UAF)"),
		bSurvives);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
