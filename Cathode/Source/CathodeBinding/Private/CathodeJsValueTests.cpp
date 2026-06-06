#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeBinding.h"
#include "CathodeBindingTestHooks.h"
#include "CathodeJsRuntime.h"

#include "Misc/AutomationTest.h"

// quickjs-ng confined to this TU (the test constructs real ref-counted JSValues to exercise the RAII).
THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Specs for FJsValue's BESPOKE refcount-RAII semantics (copy dups, move transfers, scope frees, Release
// hands off). Like 002's lifecycle tests they drive the REAL engine (CreateJsRuntime + a real ref-counted
// JSValue) and assert OUR bookkeeping — the dup/free CALL COUNTS via the observe hooks — NEVER quickjs-ng's
// internal refcount (vendor GC state). Authored, not run this sprint; pass by construction at execution.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeBindingJsValueRaiiTest,
	"Cathode.Binding.Value.DupFreeBalance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeBindingJsValueRaiiTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	int32 Dups = 0;
	int32 Frees = 0;
	GJsValueOnDup = [&Dups]() { ++Dups; };
	GJsValueOnFree = [&Frees]() { ++Frees; };

	TUniquePtr<ICathodeJsRuntime> Runtime = CreateJsRuntime();
	JSContext* Context = static_cast<JSContext*>(Runtime->GetNativeContextHandle());

	// Copy + scope: one owned value, one copy (one dup), both freed at scope exit.
	{
		FJsValue Original(Context, JS_NewObject(Context)); // takes ownership of one ref (no dup)
		FJsValue Copy = Original;                          // copy -> exactly one dup
		TestEqual(TEXT("a copy performs exactly one dup"), Dups, 1);
		TestEqual(TEXT("nothing freed yet"), Frees, 0);
	}
	TestEqual(TEXT("both holders free at scope exit"), Frees, 2);

	// Move: transfer the ref, no dup; only the destination frees (the source was emptied).
	Dups = 0;
	Frees = 0;
	{
		FJsValue Source(Context, JS_NewObject(Context));
		FJsValue Moved = MoveTemp(Source); // move -> no dup; Source emptied
		TestEqual(TEXT("move does not dup"), Dups, 0);
	}
	TestEqual(TEXT("move transfers ownership: exactly one free"), Frees, 1);

	// Release: hand off the ref without freeing; the holder then frees nothing.
	Dups = 0;
	Frees = 0;
	{
		FJsValue Holder(Context, JS_NewObject(Context));
		JSValue Raw = Holder.Release(); // ownership relinquished — no free here
		TestEqual(TEXT("Release does not free"), Frees, 0);
		JS_FreeValue(Context, Raw);     // the caller now owns the ref and frees it directly
	}
	TestEqual(TEXT("Release transferred ownership: the holder freed nothing"), Frees, 0);

	GJsValueOnDup = nullptr;
	GJsValueOnFree = nullptr;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
