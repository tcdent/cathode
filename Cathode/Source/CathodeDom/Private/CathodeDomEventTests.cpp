#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Specs for the event system: listener hold + gc_mark visibility (the node<->listener cycle, THE Model-A
// hazard) + removeEventListener reference-drop; dispatch bubble ORDER; stopPropagation / preventDefault. Drive
// the C++ dispatch algorithm directly with a RECORDING invoker (the FDomEvent -> JS Event object marshaling is
// the deferred generate-from-interface part). Assert OUR bookkeeping (mark count, CurrentTarget order, flags).
// ZERO GC-internal/refcount; actual cyclic RECLAIM needs a real JS_RunGC -> validation-pass. Authored, not run.

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

	TUniquePtr<ICathodeJsRuntime> RuntimePtr = CreateJsRuntime();
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

	TUniquePtr<ICathodeJsRuntime> RuntimePtr = CreateJsRuntime();
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

	TUniquePtr<ICathodeJsRuntime> RuntimePtr = CreateJsRuntime();
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

	TUniquePtr<ICathodeJsRuntime> RuntimePtr = CreateJsRuntime();
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

#endif // WITH_DEV_AUTOMATION_TESTS
