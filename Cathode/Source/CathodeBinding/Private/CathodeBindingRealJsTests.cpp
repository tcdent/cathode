#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeBinding.h"
#include "CathodeBoundMember.h"
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

// 017 slice 2 — the method-access test class K. The FBoundMember `compute` thunk reads the opaque (class-id-checked) AND
// the JS arg, returning arg*field — the GENUINE product. GKComputeClassId is the test-side per-run class-id the thunk
// reads; it is OVERWRITTEN each runtime registration (-> always the current run's id), and the binding's own class-id
// stays LOCAL-per-runtime (RegisterClass allocates fresh) — no production static.
struct FKTestNative { int32 Field = 0; };
JSClassID GKComputeClassId = 0;
JSValue ComputeThunk(JSContext* Ctx, JSValueConst ThisVal, int /*Argc*/, JSValueConst* Argv)
{
	FKTestNative* Self = static_cast<FKTestNative*>(JS_GetOpaque2(Ctx, ThisVal, GKComputeClassId)); // class-id-checked
	if (Self == nullptr) { return JS_EXCEPTION; }                                                  // wrong class -> JS_GetOpaque2 threw
	int32 Arg = 0;
	JS_ToInt32(Ctx, &Arg, Argv[0]);
	return JS_NewInt32(Ctx, Arg * Self->Field); // reads BOTH the JS arg AND the opaque field -> the both-factors product
}
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

// 017 slice 2 — METHOD ACCESS FROM REAL JS (FBoundMember) on the binding's OWN class K, over 016's real Evaluate. C++
// constructs K (CreateWrappedObject, opaque -> field) -> set as a JS global (NO JS constructor; C++-side construct) ->
// Evaluate("if (o.compute(arg) === expected) throw ...") -> the `compute` thunk reads the opaque (class-id-checked) AND
// the JS arg, returns arg*field -> the success-triggered throw carries the computed value. THROW-ON-SUCCESS (=== expected
// -> throw -> bThrew==true + Contains(value); a bThrew==false-only assertion is the PRIMARY reject).
//
// THE BOTH-FACTORS VARIATION (test-the-breaking-geometry on the product): TWO checks with DISTINCT (arg, field) pairs --
// o1(field=6).compute(7)===42 AND o2(field=10).compute(5)===50 -- bites EVERY degenerate thunk:
//   constant-42 (returns 42)             -> 42 != 50 -> check2 no-throw -> bThrew==false -> FAILS.
//   arg*const (arg*6, hardcodes field)   -> 5*6=30 != 50 -> check2 FAILS.
//   field*const (field*7, hardcodes arg) -> 10*7=70 != 50 -> check2 FAILS.
//   stub (no method/undefined) / echo (returns arg) -> check1 FAILS.
// Only the genuine arg*field (reading BOTH) passes both. The chain runs across >=2 SEQUENTIAL runtimes -- the FBoundMember
// install is JS_SetClassProto, so the PROTO is per-runtime (a stale class-id/proto would break o.compute on run 2).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeBindingMethodAccessThroughJsTest,
	"Cathode.Binding.Registry.MethodAccessThroughJs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeBindingMethodAccessThroughJsTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	auto RunMethodChain = [this](ICathodeJsRuntime& Rt, int32 Run) -> void
	{
		JSContext* const Ctx = static_cast<JSContext*>(Rt.GetNativeContextHandle());
		if (Ctx == nullptr) { AddError(FString::Printf(TEXT("run %d: no native context"), Run)); return; }

		TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(Rt);
		FNativeClassDef Def;
		Def.Name = "CathodeKTest";
		Def.Finalizer = &NoopFinalizer;
		const JSClassID ClassId = Registry->RegisterClass(Def);
		GKComputeClassId = ClassId; // the thunk reads this (per-run; overwritten each runtime -> always the current id)

		const FBoundMember KMembers[] = {
			{ "compute", "TestK", EBoundMemberKind::Method, "test", nullptr, &ComputeThunk, nullptr, nullptr }
		};
		InstallBoundMembers(Ctx, ClassId, KMembers, UE_ARRAY_COUNT(KMembers)); // JS_SetClassProto -- the per-runtime proto

		FKTestNative K1{ 6 };
		FKTestNative K2{ 10 };
		FJsValue O1 = Registry->CreateWrappedObject(ClassId, &K1);
		FJsValue O2 = Registry->CreateWrappedObject(ClassId, &K2);
		JSValue Global = JS_GetGlobalObject(Ctx);
		JS_SetPropertyStr(Ctx, Global, "o1", JS_DupValue(Ctx, O1.GetValue()));
		JS_SetPropertyStr(Ctx, Global, "o2", JS_DupValue(Ctx, O2.GetValue()));
		JS_FreeValue(Ctx, Global);

		// (arg, field) = (7, 6) -> 42, throw-on-success carrying "42".
		const FEvalResult Ra = Rt.Evaluate(TEXT("if (o1.compute(7) === 42) throw new Error('p:' + o1.compute(7))"), TEXT("<method>"), EEvalKind::Global);
		TestTrue(FString::Printf(TEXT("run %d (7*6): o.compute(7) === 42 -> throw-on-success (the method ran)"), Run), Ra.bThrew);
		TestTrue(FString::Printf(TEXT("run %d (7*6): the computed product '42' surfaces"), Run), Ra.Error.Message.Contains(TEXT("42")));

		// (arg, field) = (5, 10) -> 50, DISTINCT on BOTH axes -> bites constant-42 / arg*const / field*const.
		const FEvalResult Rb = Rt.Evaluate(TEXT("if (o2.compute(5) === 50) throw new Error('q:' + o2.compute(5))"), TEXT("<method>"), EEvalKind::Global);
		TestTrue(FString::Printf(TEXT("run %d (5*10): o.compute(5) === 50 -> throw-on-success (both-factors: reads arg AND field)"), Run), Rb.bThrew);
		TestTrue(FString::Printf(TEXT("run %d (5*10): the DISTINCT computed product '50' surfaces"), Run), Rb.Error.Message.Contains(TEXT("50")));
	};

	// >=2 SEQUENTIAL runtimes: the FBoundMember proto + the class-id are per-runtime (a stale one breaks o.compute on run 2).
	for (int32 Run = 0; Run < 2; ++Run)
	{
		FScopedTestRuntime Runtime;
		if (!Runtime.IsValid() || !Runtime->IsValid()) { AddError(FString::Printf(TEXT("run %d: runtime failed to start"), Run)); return false; }
		RunMethodChain(Runtime.Get(), Run);
	}

	// LEAK — N construct+method-call cycles -> malloc_size bounded (the thunk's opaque read + arg-marshal + result don't leak).
	{
		FScopedTestRuntime LeakRt;
		if (!LeakRt.IsValid() || !LeakRt.Get().IsValid()) { AddError(TEXT("leak: runtime failed to start")); return false; }
		JSContext* const Ctx = static_cast<JSContext*>(LeakRt.Get().GetNativeContextHandle());
		JSRuntime* const Rt = JS_GetRuntime(Ctx);
		TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(LeakRt.Get());
		FNativeClassDef Def; Def.Name = "CathodeKTest"; Def.Finalizer = &NoopFinalizer;
		const JSClassID ClassId = Registry->RegisterClass(Def);
		GKComputeClassId = ClassId;
		const FBoundMember KMembers[] = { { "compute", "TestK", EBoundMemberKind::Method, "test", nullptr, &ComputeThunk, nullptr, nullptr } };
		InstallBoundMembers(Ctx, ClassId, KMembers, UE_ARRAY_COUNT(KMembers));
		FKTestNative Native{ 6 };

		auto OneCycle = [&]()
		{
			FJsValue W = Registry->CreateWrappedObject(ClassId, &Native);
			JSValue Global = JS_GetGlobalObject(Ctx);
			JS_SetPropertyStr(Ctx, Global, "o", JS_DupValue(Ctx, W.GetValue())); // overwrites the prior -> prior ref dropped
			JS_FreeValue(Ctx, Global);
			LeakRt.Get().Evaluate(TEXT("globalThis.__x = globalThis.o.compute(7);"), TEXT("<leak>"), EEvalKind::Global);
		};
		auto MallocSizeAfterGC = [&]() -> uint64 { JS_RunGC(Rt); JSMemoryUsage U; JS_ComputeMemoryUsage(Rt, &U); return static_cast<uint64>(U.malloc_size); };

		for (int32 i = 0; i < 5; ++i) { OneCycle(); }
		const uint64 Baseline = MallocSizeAfterGC();
		for (int32 i = 0; i < 50; ++i) { OneCycle(); }
		const uint64 After = MallocSizeAfterGC();
		TestTrue(FString::Printf(TEXT("(LEAK) malloc_size BOUNDED after 50 construct+method-call cycles (baseline=%llu after=%llu)"), Baseline, After),
			After <= Baseline + (128 * 1024));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
