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

#endif // WITH_DEV_AUTOMATION_TESTS
