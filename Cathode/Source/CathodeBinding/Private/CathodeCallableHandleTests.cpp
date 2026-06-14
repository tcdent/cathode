#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeBinding.h"
#include "CathodeBindingTestHooks.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Spec for the held-callable: it owns a JS function (FJsValue RAII), invokes it, and frees its ref on
// destruction. Drives the REAL engine and asserts OUR bookkeeping — the invoke returns the expected
// MARSHALED value, and the held function ref balances by our free-count — never quickjs-ng internals.
// Authored, not run this sprint.

namespace
{
using namespace Cathode;

JSValue Eval(JSContext* Context, const char* Source)
{
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-test>", JS_EVAL_TYPE_GLOBAL);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeBindingCallableTest,
	"Cathode.Binding.Callable.InvokeAndBalance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeBindingCallableTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	int32 Frees = 0;
	GJsValueOnFree = [&Frees]() { ++Frees; };

	Cathode::FScopedTestRuntime Runtime;
	if (!Runtime.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	JSContext* Context = static_cast<JSContext*>(Runtime->GetNativeContextHandle());

	// A real invoke returns the expected marshaled value: a held (x)->x+1, called with 41, returns 42.
	double Result = 0.0;
	{
		FCallableHandle Handle(FJsValue(Context, Eval(Context, "(function(x){ return x + 1; })")));
		TestTrue(TEXT("the held callable is valid"), Handle.IsValid());

		FJsValue Arg = MarshalNumber(Context, 41.0);
		const JSValue Argv[1] = { Arg.GetValue() };
		FJsValue Returned = Handle.Call(JS_UNDEFINED, MakeArrayView(Argv, 1));
		TestTrue(TEXT("the call returns a value"), UnmarshalNumber(Context, Returned.GetValue(), Result));
	} // Handle (its function FJsValue), Arg, Returned all destruct here

	TestEqual(TEXT("invoke returns the expected marshaled value"), Result, 42.0);

	// Our free-count balances: the handle's function, the arg, and the returned value each freed once (3).
	// (We assert OUR call count via the observe hook, never quickjs-ng's internal refcount.)
	TestEqual(TEXT("every owned ref freed exactly once"), Frees, 3);

	GJsValueOnFree = nullptr;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
