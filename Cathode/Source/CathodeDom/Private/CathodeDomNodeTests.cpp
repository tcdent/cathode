#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Specs for the node-class + retained tree. Drive the REAL engine (like the CathodeBinding tests) and assert OUR
// bookkeeping — the wrapper-governed index (FindNode), the gc_mark visiting held child refs (our counting
// mark func), and the finalizer de-registering + the FreeRT cascade reclaiming a sole-held child (index
// bookkeeping, never GC internals). The binding observe hooks are private to CathodeBinding, so we count via our
// own JS_MarkFunc + the index.

namespace
{
using namespace Cathode;

// A recording layout engine — the document only stores it (no layout driven in these tests). Avoids pulling
// in CathodeLayout's real litehtml engine + a text measurer.
class FStubLayoutEngine final : public ILayoutEngine
{
public:
	virtual void SetDocument(const ILayoutDocumentSource&) override {}
	virtual void MarkNodeDirty(int32 NodeId, EDirtyKind Kind) override { DirtyMarks.Add(TPair<int32, EDirtyKind>(NodeId, Kind)); }
	virtual FLayoutResult Relayout(int32) override { return FLayoutResult(); }
	TArray<TPair<int32, EDirtyKind>> DirtyMarks;
};

// A counting JS_MarkFunc (free function — JS_MarkValue needs a function pointer, not a capturing lambda).
int32 GMarkCount = 0;
void CountingMark(JSRuntime* /*Runtime*/, JSGCObjectHeader* /*Header*/) { ++GMarkCount; }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomNodeRegistrationTest,
	"Cathode.Dom.Node.RegistrationAndTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomNodeRegistrationTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	JSRuntime* Rt = JS_GetRuntime(Context);

	{
		// Registration round-trip: the created wrapper carries the C++ node as its opaque; the index resolves it.
		FJsValue Parent = Doc->CreateElement(TEXT("div"));
		FDomNode* ParentNode = Doc->NodeFromWrapper(Parent);
		TestTrue(TEXT("wrapper resolves to a node"), ParentNode != nullptr);
		if (ParentNode != nullptr)
		{
			TestTrue(TEXT("element kind + tag round-trip"), ParentNode->GetNodeType() == EDomNodeType::Element
				&& ParentNode->GetTagName() == FString(TEXT("div")));
			TestEqual(TEXT("the index resolves the node id back to the node"),
				Doc->FindNode(ParentNode->GetNodeId()), ParentNode);
		}

		// Retained tree via JS refs: appendChild makes the parent hold the child's WRAPPER; gc_mark visits it.
		FJsValue Child = Doc->CreateTextNode(TEXT("hi"));
		if (ParentNode != nullptr)
		{
			ParentNode->AppendChild(Child);
			GMarkCount = 0;
			ParentNode->MarkHeldRefs(Rt, &CountingMark);
			TestEqual(TEXT("gc_mark visits the one held child wrapper"), GMarkCount, 1);
		}
	} // Parent + Child wrappers free here (Doc + runtime still alive)

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomNodeFinalizerTest,
	"Cathode.Dom.Node.FinalizerDeregistersAndCascades",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomNodeFinalizerTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	JSRuntime* Rt = JS_GetRuntime(Context);

	FJsValue Parent = Doc->CreateElement(TEXT("div"));
	FDomNode* ParentNode = Doc->NodeFromWrapper(Parent);
	const FDomNodeId ParentId = ParentNode ? ParentNode->GetNodeId() : INDEX_NONE;

	FDomNodeId ChildId = INDEX_NONE;
	{
		FJsValue Child = Doc->CreateTextNode(TEXT("hi"));
		FDomNode* ChildNode = Doc->NodeFromWrapper(Child);
		ChildId = ChildNode ? ChildNode->GetNodeId() : INDEX_NONE;
		if (ParentNode != nullptr)
		{
			ParentNode->AppendChild(Child); // parent now holds an owning ref to the child wrapper
		}
	} // drop the TEST's child ref => the parent is now the SOLE holder of the child wrapper

	TestTrue(TEXT("both nodes registered before finalize"),
		Doc->FindNode(ParentId) != nullptr && Doc->FindNode(ChildId) != nullptr);

	// Directly invoke the parent's finalizer (simulating collection — we do not trigger a real GC). It
	// de-registers the parent AND FreeRTs its child wrapper; the child, now unreferenced, is reclaimed and its
	// finalizer de-registers it too — the cascade, observed purely via the index.
	FinalizeNativeObject<FDomNode>(Rt, Parent.GetValue());

	TestEqual(TEXT("parent de-registered by its finalizer"), Doc->FindNode(ParentId), static_cast<FDomNode*>(nullptr));
	TestEqual(TEXT("sole-held child reclaimed via the FreeRT cascade + de-registered"),
		Doc->FindNode(ChildId), static_cast<FDomNode*>(nullptr));

	// The parent C++ object is gone; null the wrapper's opaque so the wrapper's own free does not double-finalize.
	JS_SetOpaque(Parent.GetValue(), nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomDisposeTest,
	"Cathode.Dom.Document.DisposeReclaimsTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomDisposeTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);

	// The document owns a root; give it a child so Dispose's reclaim is a real cascade.
	const FDomNodeId RootId = Doc->GetRootNodeId();
	FDomNode* RootNode = Doc->FindNode(RootId);
	FDomNodeId ChildId = INDEX_NONE;
	{
		FJsValue Child = Doc->CreateElement(TEXT("div"));
		ChildId = Doc->NodeFromWrapper(Child)->GetNodeId();
		if (RootNode != nullptr) { RootNode->AppendChild(Child); } // root holds the child wrapper
	} // drop the test's child ref => the root is the sole holder of the child

	TestTrue(TEXT("document owns a root + child, both registered"),
		RootId != INDEX_NONE && Doc->FindNode(RootId) != nullptr && Doc->FindNode(ChildId) != nullptr);

	// Dispose = the runtime release: drop the root ref -> root reclaimed -> FreeRT child -> child reclaimed.
	// The whole acyclic tree de-registers via the cascade (index bookkeeping; runtime alive, document alive).
	Doc->Dispose();
	TestEqual(TEXT("dispose drops the root -> de-registered"), Doc->FindNode(RootId), static_cast<FDomNode*>(nullptr));
	TestEqual(TEXT("dispose cascades -> child de-registered"), Doc->FindNode(ChildId), static_cast<FDomNode*>(nullptr));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomOrphanTest,
	"Cathode.Dom.Document.OrphanSurvivorOnTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomOrphanTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());
	JSRuntime* Rt = JS_GetRuntime(Context);

	// A node held OUTSIDE the document so it survives the document (the detached-survivor case).
	FJsValue Survivor;
	FDomNode* SurvivorNode = nullptr;
	FDomNodeId SurvivorId = INDEX_NONE;
	{
		TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
		Survivor = Doc->CreateElement(TEXT("div"));
		SurvivorNode = Doc->NodeFromWrapper(Survivor);
		SurvivorId = SurvivorNode ? SurvivorNode->GetNodeId() : INDEX_NONE;
		TestTrue(TEXT("registered + not orphaned while the document lives"),
			SurvivorNode != nullptr && Doc->FindNode(SurvivorId) == SurvivorNode && !SurvivorNode->IsOrphaned());
	} // Doc destructs here -> the PASSIVE PRODUCTION dtor runs (context-free Release + orphan survivors)

	// The passive dtor orphaned the survivor — deterministic, context-free (pure C++ back-ref null).
	TestTrue(TEXT("passive dtor orphaned the survivor"), SurvivorNode != nullptr && SurvivorNode->IsOrphaned());

	// Now invoke the survivor's finalizer: orphaned -> Document==null guard -> it SKIPS the gone index (no
	// de-register, no dangling), only releasing its own held refs.
	FinalizeNativeObject<FDomNode>(Rt, Survivor.GetValue());

	// Prevent double-finalize when the held wrapper frees at scope end.
	JS_SetOpaque(Survivor.GetValue(), nullptr);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
