#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeBinding.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Spec for the BESPOKE appendable class-registration round-trip: register a native class, create a wrapped
// object with an opaque native pointer, recover the SAME pointer via GetOpaque. Drives the REAL engine
// (like 002 lifecycle) and asserts OUR bookkeeping — that the pointer we attached is the pointer we get
// back — NEVER quickjs-ng GC internals or vendor output. Authored, not run this sprint.

namespace
{
using namespace Cathode;

// A finalizer for the test class. No-op: the native object the round-trip uses is a stack sentinel the test
// owns; the finalizer's real job (free the C++ side) is exercised in the teardown slice, not here.
void TestClassFinalizer(JSRuntime* /*Runtime*/, JSValueConst /*Value*/)
{
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeBindingClassRegistrationTest,
	"Cathode.Binding.Registry.OpaqueRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeBindingClassRegistrationTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime Runtime;
	if (!Runtime.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*Runtime);

	FNativeClassDef Def;
	Def.Name = "CathodeTestNative";
	Def.Finalizer = &TestClassFinalizer;
	// Def.GcMark stays null -> behaves exactly as today (no cycle-collection policy this slice).

	const JSClassID ClassId = Registry->RegisterClass(Def);
	TestTrue(TEXT("a class id was allocated"), ClassId != 0);
	TestTrue(TEXT("the class reports registered"), Registry->IsRegistered(ClassId));
	TestFalse(TEXT("an unregistered id reports false"), Registry->IsRegistered(ClassId + 12345));

	// Round-trip OUR opaque pointer through the wrapped object — our bookkeeping (the pointer in == the
	// pointer out), never any quickjs-ng GC state.
	int32 NativeSentinel = 0;
	void* const Opaque = &NativeSentinel;

	FJsValue Wrapped = Registry->CreateWrappedObject(ClassId, Opaque);
	TestTrue(TEXT("a wrapped object was created"), Wrapped.IsValid());

	const FNativeObjectHandle Handle(MoveTemp(Wrapped));
	TestEqual(TEXT("GetOpaque returns the exact pointer attached"), Handle.GetOpaque(ClassId), Opaque);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
