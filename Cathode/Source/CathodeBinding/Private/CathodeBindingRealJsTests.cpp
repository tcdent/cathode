#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeBinding.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// 017 slice 1 — the OPAQUE ROUND-TRIP THROUGH REAL JS (the genuine gap: the existing Registry.OpaqueRoundTrip is
// C++->wrapped->C++; the wrapped object never passes through a real eval). Over 016's real Evaluate: C++ constructs a
// wrapped instance (CreateWrappedObject, opaque -> a C++-COMPUTED field), sets it as a JS global, real JS HOLDS + RETURNS
// it (an identity fn), C++ recovers the opaque (the production FNativeObjectHandle::GetOpaque == JS_GetOpaque) == the
// EXACT original ptr, and reads back the distinct C++-computed field. Evaluate carries no value (the Bridge's seam), so
// the object round-trips via GLOBALS (C++ sets, real JS moves, C++ reads via the raw context).
//
// DISCRIMINATING (the PRIMARY reject = a "constructed OK"/instanceof/bThrew==false-only assertion -- a no-op binding
// constructs a plain object): TWO instances with DISTINCT computed fields each recover their OWN field (a shared/null/stub
// opaque -> both same -> FAILS). FRESH-PER-RUNTIME class-id (the 012 AV): the chain runs across >=2 SEQUENTIAL runtimes
// (a stale process-global class-id grafts the exotic onto a foreign class on the 2nd+ runtime -> reinterpret -> AV; a
// first-runtime-only proof masks it). WRONG-CLASS GetOpaque -> NULL (a plain {} -> NULL, no reinterpret/AV).

namespace
{
using namespace Cathode;

struct FRoundTripNative { int32 Field = 0; }; // the opaque target; Field is the C++-computed discriminator
void NoopFinalizer(JSRuntime* /*Rt*/, JSValueConst /*Val*/) {} // the native is a stack sentinel the test owns (no free)

int32 ComputeField(int32 Seed) { return Seed * Seed + 1; } // f(seed): distinct per seed, computed C++-side
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeBindingRoundTripThroughJsTest,
	"Cathode.Binding.Registry.OpaqueRoundTripThroughJs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeBindingRoundTripThroughJsTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// THE CHAIN over one runtime: register -> 2 instances (distinct fields) -> set globals -> real JS holds/returns
	// (identity fn) -> C++ recovers the EXACT opaque + the distinct field; wrong-class -> NULL. Returns true on success.
	auto RunChain = [this](ICathodeJsRuntime& Rt, int32 Run) -> void
	{
		JSContext* const Ctx = static_cast<JSContext*>(Rt.GetNativeContextHandle());
		if (Ctx == nullptr) { AddError(FString::Printf(TEXT("run %d: no native context"), Run)); return; }

		TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(Rt);
		FNativeClassDef Def;
		Def.Name = "CathodeRoundTripNative";
		Def.Finalizer = &NoopFinalizer;
		const JSClassID ClassId = Registry->RegisterClass(Def);
		TestTrue(FString::Printf(TEXT("run %d: a class id was allocated (fresh per runtime — the 012 AV gate)"), Run), ClassId != 0);

		// Two native sentinels with DISTINCT C++-computed fields.
		FRoundTripNative N1{ ComputeField(3) }; // 10
		FRoundTripNative N2{ ComputeField(5) }; // 26
		FJsValue W1 = Registry->CreateWrappedObject(ClassId, &N1);
		FJsValue W2 = Registry->CreateWrappedObject(ClassId, &N2);
		TestTrue(FString::Printf(TEXT("run %d: both wrapped objects created"), Run), W1.IsValid() && W2.IsValid());

		// Hand them to real JS: set as globals (the global owns a dup; W1/W2 keep their own ref, freed at scope).
		JSValue Global = JS_GetGlobalObject(Ctx);
		JS_SetPropertyStr(Ctx, Global, "__o1", JS_DupValue(Ctx, W1.GetValue()));
		JS_SetPropertyStr(Ctx, Global, "__o2", JS_DupValue(Ctx, W2.GetValue()));
		JS_FreeValue(Ctx, Global);

		// THE TRANSIT — a GENUINE JS operation on the objects (NOT an unrelated eval): real JS HOLDS both wrapped objects
		// in an array, the engine validates it manages 2 elements, then MOVES them back out by index -> __r1/__r2. This is
		// what makes the live engine demonstrably hold/move the wrapped object (a broken binding -> non-object -> the array
		// ops would fault). A plain {} is set for the wrong-class negative.
		const FEvalResult R = Rt.Evaluate(
			TEXT("globalThis.__held = [globalThis.__o1, globalThis.__o2];")
			TEXT("if (globalThis.__held.length !== 2) throw new Error('the engine lost the wrapped objects');")
			TEXT("globalThis.__r1 = globalThis.__held[0]; globalThis.__r2 = globalThis.__held[1]; globalThis.__plain = {};"),
			TEXT("<roundtrip>"), EEvalKind::Global);
		TestFalse(FString::Printf(TEXT("run %d: the JS transit (array hold + move-back) did not throw"), Run), R.bThrew);

		// C++ recovers the opaque from the objects REAL JS held + returned (the production FNativeObjectHandle::GetOpaque).
		Global = JS_GetGlobalObject(Ctx);
		const FNativeObjectHandle H1(FJsValue(Ctx, JS_GetPropertyStr(Ctx, Global, "__r1")));
		const FNativeObjectHandle H2(FJsValue(Ctx, JS_GetPropertyStr(Ctx, Global, "__r2")));
		const FNativeObjectHandle HPlain(FJsValue(Ctx, JS_GetPropertyStr(Ctx, Global, "__plain")));
		JS_FreeValue(Ctx, Global);

		void* const Op1 = H1.GetOpaque(ClassId);
		void* const Op2 = H2.GetOpaque(ClassId);

		// EXACT PER-INSTANCE: each recovers its OWN native pointer (a shared/null/stub opaque -> both equal -> FAILS).
		TestEqual(FString::Printf(TEXT("run %d: o1 round-trips through real JS -> recovers N1's EXACT pointer"), Run), Op1, static_cast<void*>(&N1));
		TestEqual(FString::Printf(TEXT("run %d: o2 round-trips through real JS -> recovers N2's EXACT pointer"), Run), Op2, static_cast<void*>(&N2));
		if (Op1 != nullptr && Op2 != nullptr)
		{
			// the DISTINCT C++-computed fields surface after real JS held the objects (the discriminator).
			TestEqual(FString::Printf(TEXT("run %d: o1's C++-computed field surfaces (10)"), Run), static_cast<FRoundTripNative*>(Op1)->Field, ComputeField(3));
			TestEqual(FString::Printf(TEXT("run %d: o2's DISTINCT C++-computed field surfaces (26) — per-instance, not shared"), Run), static_cast<FRoundTripNative*>(Op2)->Field, ComputeField(5));
		}

		// WRONG-CLASS (negative): GetOpaque on a plain {} (our class id) -> NULL, handled safely (no reinterpret/AV).
		TestTrue(FString::Printf(TEXT("run %d: GetOpaque on a wrong-class object (plain {}) -> NULL"), Run), HPlain.GetOpaque(ClassId) == nullptr);
	};

	// >=2 SEQUENTIAL runtimes (FScopedTestRuntime is single-instance): the 012 stale-class-id AV fires on the 2nd+ run.
	for (int32 Run = 0; Run < 2; ++Run)
	{
		FScopedTestRuntime Runtime;
		if (!Runtime.IsValid() || !Runtime->IsValid()) { AddError(FString::Printf(TEXT("run %d: runtime failed to start"), Run)); return false; }
		RunChain(Runtime.Get(), Run);
	}

	// LEAK — empirical bounded-growth: N construct+round-trip cycles (incl the wrapped object) must NOT grow memory
	// UNBOUNDED (bounded, NOT byte-exact). The FJsValue dup/free is balanced by construction; a leak grows malloc_size ~N.
	{
		FScopedTestRuntime LeakRt;
		if (!LeakRt.IsValid() || !LeakRt.Get().IsValid()) { AddError(TEXT("leak: runtime failed to start")); return false; }
		JSContext* const Ctx = static_cast<JSContext*>(LeakRt.Get().GetNativeContextHandle());
		JSRuntime* const Rt = JS_GetRuntime(Ctx);
		TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(LeakRt.Get());
		FNativeClassDef Def; Def.Name = "CathodeRoundTripNative"; Def.Finalizer = &NoopFinalizer;
		const JSClassID ClassId = Registry->RegisterClass(Def);
		FRoundTripNative Native{ ComputeField(7) };

		auto OneCycle = [&]()
		{
			FJsValue W = Registry->CreateWrappedObject(ClassId, &Native);
			JSValue Global = JS_GetGlobalObject(Ctx);
			JS_SetPropertyStr(Ctx, Global, "__o", JS_DupValue(Ctx, W.GetValue())); // overwrites the prior -> prior ref dropped
			JS_FreeValue(Ctx, Global);
			LeakRt.Get().Evaluate(TEXT("globalThis.__held = [globalThis.__o]; globalThis.__r = globalThis.__held[0];"), TEXT("<leak>"), EEvalKind::Global);
			Global = JS_GetGlobalObject(Ctx);
			const FNativeObjectHandle H(FJsValue(Ctx, JS_GetPropertyStr(Ctx, Global, "__r")));
			JS_FreeValue(Ctx, Global);
			(void)H.GetOpaque(ClassId);
		};
		auto MallocSizeAfterGC = [&]() -> uint64 { JS_RunGC(Rt); JSMemoryUsage U; JS_ComputeMemoryUsage(Rt, &U); return static_cast<uint64>(U.malloc_size); };

		for (int32 i = 0; i < 5; ++i) { OneCycle(); }   // warm up
		const uint64 Baseline = MallocSizeAfterGC();
		for (int32 i = 0; i < 50; ++i) { OneCycle(); }  // 10x
		const uint64 After = MallocSizeAfterGC();
		TestTrue(FString::Printf(TEXT("(LEAK) malloc_size BOUNDED after 50 construct+round-trip cycles (baseline=%llu after=%llu)"), Baseline, After),
			After <= Baseline + (128 * 1024));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
