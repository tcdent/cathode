#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeBinding.h"
#include "CathodeBindingTestHooks.h"
#include "CathodeJsRuntime.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Specs for the Model A teardown mechanisms — gc_mark visibility, the finalizer freeing the native side,
// and the rt-based release. We assert OUR bookkeeping (mark count, native-dtor count, rt-free count) by
// invoking our gc_mark / finalizer callbacks DIRECTLY. We do NOT trigger a real GC: actual cycle
// reclamation / finalizer-fires-on-GC is GC-timed and DEFERRED to the validation-pass. Authored, not run
// this sprint.

namespace
{
using namespace Cathode;

// A recording native object: holds two JS refs (the cross-heap shape) + the runtime so its destructor can
// release them via the finalizer-safe rt-based path. Counts its own destruction.
struct FRecordingNative
{
	JSRuntime* Runtime = nullptr;
	FJsValue RefA;
	FJsValue RefB;
	int32* DtorCounter = nullptr;

	~FRecordingNative()
	{
		// Finalizer path: release held refs via the runtime (NEVER the ctx-based destructor — a finalizer
		// has no ctx).
		RefA.FreeRT(Runtime);
		RefB.FreeRT(Runtime);
		if (DtorCounter != nullptr)
		{
			++*DtorCounter;
		}
	}
};

// The class's gc_mark: mark both held refs so the collector can traverse the cycle (Model A cycle visibility).
void RecordingGcMark(JSRuntime* Runtime, JSValueConst Value, JS_MarkFunc* MarkFunc)
{
	// TODO-PREBUILD(signature): JS_GetAnyOpaque (quickjs.h:1042) — see FinalizeNativeObject.
	JSClassID ClassId;
	if (FRecordingNative* Native = static_cast<FRecordingNative*>(JS_GetAnyOpaque(Value, &ClassId)))
	{
		Native->RefA.Mark(Runtime, MarkFunc);
		Native->RefB.Mark(Runtime, MarkFunc);
	}
}

// A no-op JS_MarkFunc for the direct gc_mark invocation (we count our Mark CALLS via the hook, not what the
// collector does with them).
void NoOpMark(JSRuntime* /*Runtime*/, JSGCObjectHeader* /*Header*/) {}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeBindingTeardownTest,
	"Cathode.Binding.Teardown.GcMarkAndFinalize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeBindingTeardownTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	int32 Marks = 0;
	int32 RtFrees = 0;
	int32 CtxFrees = 0;
	GJsValueOnMark = [&Marks]() { ++Marks; };
	GJsValueOnFreeRT = [&RtFrees]() { ++RtFrees; };
	GJsValueOnFree = [&CtxFrees]() { ++CtxFrees; };

	TUniquePtr<ICathodeJsRuntime> Runtime = CreateJsRuntime();
	JSContext* Context = static_cast<JSContext*>(Runtime->GetNativeContextHandle());
	JSRuntime* Rt = JS_GetRuntime(Context);
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*Runtime);

	// Register a class wired with our gc_mark + the generic finalizer.
	FNativeClassDef Def;
	Def.Name = "CathodeTeardownNative";
	Def.Finalizer = &FinalizeNativeObject<FRecordingNative>;
	Def.GcMark = &RecordingGcMark;
	const JSClassID ClassId = Registry->RegisterClass(Def);

	// A native holding two real JS refs; wrap it (opaque = the native).
	int32 DtorCount = 0;
	FRecordingNative* Native = new FRecordingNative{
		Rt, FJsValue(Context, JS_NewObject(Context)), FJsValue(Context, JS_NewObject(Context)), &DtorCount };
	FJsValue Wrapped = Registry->CreateWrappedObject(ClassId, Native);

	// gc_mark: invoking the class's gc_mark marks BOTH held refs (our visibility bookkeeping).
	RecordingGcMark(Rt, Wrapped.GetValue(), &NoOpMark);
	TestEqual(TEXT("gc_mark marks each held ref exactly once"), Marks, 2);

	// finalizer: invoking it frees the native side exactly once, releasing held refs via the rt-based path
	// (never the ctx path).
	FinalizeNativeObject<FRecordingNative>(Rt, Wrapped.GetValue());
	TestEqual(TEXT("finalizer frees the native side once"), DtorCount, 1);
	TestEqual(TEXT("held refs released via the rt-based path"), RtFrees, 2);
	TestEqual(TEXT("no held ref took the ctx-based path"), CtxFrees, 0);

	// The native is gone; clear the opaque so the wrapper's own free does not double-finalize, then let
	// Wrapped free the JS object.
	JS_SetOpaque(Wrapped.GetValue(), nullptr);

	GJsValueOnMark = nullptr;
	GJsValueOnFreeRT = nullptr;
	GJsValueOnFree = nullptr;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
