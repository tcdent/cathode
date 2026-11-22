#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeBinding.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Specs for the BESPOKE marshaling: the accept/reject classification over the vocabulary, and the typed
// primitive converters. Drive the REAL engine (like the CathodeRuntime lifecycle tests) and assert OUR classification +
// OUR converted values — never quickjs-ng internals.

namespace
{
using namespace Cathode;

JSValue Eval(JSContext* Context, const char* Source)
{
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-test>", JS_EVAL_TYPE_GLOBAL);
}

void TestClassFinalizer(JSRuntime* /*Runtime*/, JSValueConst /*Value*/) {}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeBindingClassifyTest,
	"Cathode.Binding.Marshal.ClassifyVocabulary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeBindingClassifyTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime Runtime;
	if (!Runtime.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	JSContext* Context = static_cast<JSContext*>(Runtime->GetNativeContextHandle());
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*Runtime);

	auto ClassifyAndFree = [&](JSValue Value) -> EJsValueKind
	{
		const EJsValueKind Kind = ClassifyJsValue(Context, Value, Registry.Get());
		JS_FreeValue(Context, Value);
		return Kind;
	};

	// --- ACCEPT each vocabulary kind ---
	TestTrue(TEXT("number"),    ClassifyAndFree(JS_NewFloat64(Context, 1.5)) == EJsValueKind::Number);
	TestTrue(TEXT("string"),    ClassifyAndFree(JS_NewString(Context, "x")) == EJsValueKind::String);
	TestTrue(TEXT("bool"),      ClassifyAndFree(JS_NewBool(Context, true)) == EJsValueKind::Bool);
	TestTrue(TEXT("null"),      ClassifyAndFree(JS_NULL) == EJsValueKind::Null);
	TestTrue(TEXT("undefined"), ClassifyAndFree(JS_UNDEFINED) == EJsValueKind::Undefined);
	TestTrue(TEXT("array"),     ClassifyAndFree(JS_NewArray(Context)) == EJsValueKind::Array);
	TestTrue(TEXT("record"),    ClassifyAndFree(JS_NewObject(Context)) == EJsValueKind::Record);
	TestTrue(TEXT("callable"),  ClassifyAndFree(Eval(Context, "(function(){})")) == EJsValueKind::Callable);

	// NativeObject: a wrapped instance of a registered class -> NativeObject WHEN the registry is supplied.
	{
		FNativeClassDef Def;
		Def.Name = "CathodeTestNative";
		Def.Finalizer = &TestClassFinalizer;
		const JSClassID ClassId = Registry->RegisterClass(Def);
		int32 Sentinel = 0;
		FJsValue Wrapped = Registry->CreateWrappedObject(ClassId, &Sentinel);
		TestTrue(TEXT("native object (with registry)"),
			ClassifyJsValue(Context, Wrapped.GetValue(), Registry.Get()) == EJsValueKind::NativeObject);
		// Without the registry it cannot be confirmed as ours -> best-effort Record (documented).
		TestTrue(TEXT("native object without registry falls back to record"),
			ClassifyJsValue(Context, Wrapped.GetValue(), nullptr) == EJsValueKind::Record);
	}

	// --- REJECT each exotic kind ---
	TestTrue(TEXT("symbol rejected"),      ClassifyAndFree(Eval(Context, "Symbol()")) == EJsValueKind::Rejected);
	TestTrue(TEXT("bigint rejected"),      ClassifyAndFree(Eval(Context, "1n")) == EJsValueKind::Rejected);
	TestTrue(TEXT("date rejected"),        ClassifyAndFree(Eval(Context, "new Date()")) == EJsValueKind::Rejected);
	TestTrue(TEXT("typed-array rejected"), ClassifyAndFree(Eval(Context, "new Uint8Array(1)")) == EJsValueKind::Rejected);
	TestTrue(TEXT("arraybuffer rejected"), ClassifyAndFree(Eval(Context, "new ArrayBuffer(1)")) == EJsValueKind::Rejected);
	TestTrue(TEXT("proxy rejected"),       ClassifyAndFree(Eval(Context, "new Proxy({}, {})")) == EJsValueKind::Rejected);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeBindingPrimitiveRoundTripTest,
	"Cathode.Binding.Marshal.PrimitiveRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeBindingPrimitiveRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime Runtime;
	if (!Runtime.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	JSContext* Context = static_cast<JSContext*>(Runtime->GetNativeContextHandle());

	// Each primitive marshals out and unmarshals back to the SAME value (our converted values).
	{
		FJsValue V = MarshalNumber(Context, 3.5);
		double Out = 0.0;
		TestTrue(TEXT("number unmarshals"), UnmarshalNumber(Context, V.GetValue(), Out));
		TestEqual(TEXT("number round-trips"), Out, 3.5);
	}
	{
		FJsValue V = MarshalString(Context, TEXT("hello"));
		FString Out;
		TestTrue(TEXT("string unmarshals"), UnmarshalString(Context, V.GetValue(), Out));
		TestEqual(TEXT("string round-trips"), Out, FString(TEXT("hello")));
	}
	{
		FJsValue V = MarshalBool(Context, true);
		bool Out = false;
		TestTrue(TEXT("bool unmarshals"), UnmarshalBool(Context, V.GetValue(), Out));
		TestTrue(TEXT("bool round-trips"), Out);
	}

	// Reject path: a typed converter refuses a mismatched kind.
	{
		FJsValue Str = MarshalString(Context, TEXT("not a number"));
		double Out = 0.0;
		TestFalse(TEXT("UnmarshalNumber rejects a string"), UnmarshalNumber(Context, Str.GetValue(), Out));
	}

	// Array COMPOSES the element converters (no tagged-union): build [10, 20] from MarshalNumber, classify
	// it as Array, read elements back with the element converter.
	{
		JSValue Array = JS_NewArray(Context);
		JS_SetPropertyUint32(Context, Array, 0, MarshalNumber(Context, 10.0).Release());
		JS_SetPropertyUint32(Context, Array, 1, MarshalNumber(Context, 20.0).Release());

		TestTrue(TEXT("composed array classifies as Array"), ClassifyJsValue(Context, Array) == EJsValueKind::Array);

		double E0 = 0.0;
		double E1 = 0.0;
		JSValue V0 = JS_GetPropertyUint32(Context, Array, 0);
		JSValue V1 = JS_GetPropertyUint32(Context, Array, 1);
		TestTrue(TEXT("element 0 unmarshals"), UnmarshalNumber(Context, V0, E0));
		TestTrue(TEXT("element 1 unmarshals"), UnmarshalNumber(Context, V1, E1));
		TestEqual(TEXT("array element 0"), E0, 10.0);
		TestEqual(TEXT("array element 1"), E1, 20.0);
		JS_FreeValue(Context, V0);
		JS_FreeValue(Context, V1);
		JS_FreeValue(Context, Array);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
