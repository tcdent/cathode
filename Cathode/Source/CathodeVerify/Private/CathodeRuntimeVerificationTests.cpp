#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeBuildId.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Greppable coherence line: carries the cross-module aggregate the faculty verified (the cross-module analog
// of CathodeRuntime's [CATHODE-BUILD-SIGNATURE]) so a run log witnesses any Cathode-module relink across runs.
DEFINE_LOG_CATEGORY_STATIC(LogCathodeVerify, Log, All);

// Sprint 011 [b] gate: drive the runtime-verification faculty in the REAL production Cathode runtime and assert
// its structured self-report. The runtime is BARE today (the production OnInstall* assembly seams are unwired —
// itself the faculty-A finding under test), so the faculty must report a massively UNDER-exposed surface, the
// DOM/globals reconcile dormant (no 012 descriptor), the CSS axis under-exposed (no CSSOM), faculty-B's
// self-test green — and, the review's core [b] requirement, the reported PRODUCTION surface must EXCLUDE every
// name the harness injects to drive/capture the faculty (no false over-exposure). This gate also drift-proofs
// that exclusion: it asserts the faculty's declared exclusion set covers exactly what this driver injects.

namespace
{
using namespace Cathode;

// Native emit-sink capture. The faculty calls globalThis.__cathodeEmitReport(json); this C function captures it
// (the bare runtime has no console, and Evaluate carries no completion value, so the native sink is the only
// path). File-static is safe + idiomatic here — the test is serial and single-threaded (mirrors the binding
// tests' GJsValueOnFree global hook).
TArray<FString> GVerifyEmittedReports;

JSValue CathodeVerifyEmitReport(JSContext* Context, JSValueConst /*ThisVal*/, int Argc, JSValueConst* Argv)
{
	if (Argc >= 1)
	{
		const char* Utf8 = JS_ToCString(Context, Argv[0]);
		if (Utf8)
		{
			GVerifyEmittedReports.Add(FString(UTF8_TO_TCHAR(Utf8)));
			JS_FreeCString(Context, Utf8);
		}
	}
	return JS_UNDEFINED;
}

// Evaluate a source as a global script; on an uncaught exception, surface the message and return false.
bool EvalGlobal(JSContext* Context, const FString& Source, const char* SourceName, FString& OutError)
{
	const FTCHARToUTF8 Utf8(*Source);
	JSValue Result = JS_Eval(Context, Utf8.Get(), Utf8.Length(), SourceName, JS_EVAL_TYPE_GLOBAL);
	bool bOk = true;
	if (JS_IsException(Result))
	{
		bOk = false;
		JSValue Exc = JS_GetException(Context);
		const char* Msg = JS_ToCString(Context, Exc);
		OutError = Msg ? FString(UTF8_TO_TCHAR(Msg)) : TEXT("<unknown JS exception>");
		if (Msg)
		{
			JS_FreeCString(Context, Msg);
		}
		JS_FreeValue(Context, Exc);
	}
	JS_FreeValue(Context, Result);
	return bOk;
}

// Collect a JSON string-array field into a TSet for membership checks.
TSet<FString> ArrayFieldToSet(const TSharedPtr<FJsonObject>& Obj, const FString& Field)
{
	TSet<FString> Out;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Obj.IsValid() && Obj->TryGetArrayField(Field, Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S))
			{
				Out.Add(S);
			}
		}
	}
	return Out;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeVerificationTest,
	"Cathode.Verify.RuntimeSelfReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeVerificationTest::RunTest(const FString& Parameters)
{
	GVerifyEmittedReports.Reset();

	// 1) The real production runtime on the test thread (finding-#1 seam). Gate on IsValid() before any use.
	FScopedTestRuntime Runtime;
	if (!Runtime.IsValid())
	{
		AddError(TEXT("Cathode test runtime failed to initialize"));
		return false;
	}
	JSContext* Context = static_cast<JSContext*>(Runtime->GetNativeContextHandle());
	if (!Context)
	{
		AddError(TEXT("runtime exposes no native JS context"));
		return false;
	}

	// 2) Locate + load the faculty JS from the plugin dir.
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
	if (!Plugin.IsValid())
	{
		AddError(TEXT("Cathode plugin not found"));
		return false;
	}
	const FString JsDir = Plugin->GetBaseDir() / TEXT("Source/CathodeVerify/JS");

	FString WalkSource, FacultySource;
	if (!FFileHelper::LoadFileToString(WalkSource, *(JsDir / TEXT("surface-walk.js"))))
	{
		AddError(TEXT("could not load surface-walk.js"));
		return false;
	}
	if (!FFileHelper::LoadFileToString(FacultySource, *(JsDir / TEXT("runtime-verify.js"))))
	{
		AddError(TEXT("could not load runtime-verify.js"));
		return false;
	}

	// 3) Install the harness scaffolding on the live global, tracking EXACTLY what we inject. This injected set
	//    is the drift-guard baseline: faculty-A must exclude precisely these names from the reported production
	//    surface (else it reports the harness-augmented runtime, not production).
	TSet<FString> InjectedNames;
	{
		JSValue Global = JS_GetGlobalObject(Context);

		JS_SetPropertyStr(Context, Global, "__cathodeEmitReport",
			JS_NewCFunction(Context, &CathodeVerifyEmitReport, "__cathodeEmitReport", 1));
		InjectedNames.Add(TEXT("__cathodeEmitReport"));

		// Cross-module AGGREGATE coherence anchor (read LIVE at faculty time): every loaded Cathode module's PE
		// link-id, folded. A relink of ANY Cathode module advances it — the single-module build-id couldn't
		// witness a sibling's relink.
		const FString BuildId = Cathode::CathodeAggregateBuildId();
		JS_SetPropertyStr(Context, Global, "__CATHODE_BUILD_ID__", JS_NewString(Context, TCHAR_TO_UTF8(*BuildId)));
		InjectedNames.Add(TEXT("__CATHODE_BUILD_ID__"));

		// NB: no CSS declared source is injected in [b]. The CSS axis is DORMANT — its declared source is
		// Cathode's OWN litehtml-grounded CSS declaration, which is authored later on the convention track (NOT
		// in [b], and NEVER by reading/copying panels/, which is another dev's area). So the faculty's CSS
		// reconcile reports the dormant slot, parallel to DOM/globals (whose source is the 012 descriptor).

		JS_FreeValue(Context, Global);
	}

	// 4) Evaluate the shared walk (installs globalThis.CathodeSurfaceWalk) then the faculty (an IIFE that
	//    enumerates the surface + emits via the native sink). The walk installs ONE global; the faculty installs
	//    none — so the harness's total global additions == InjectedNames.
	FString EvalError;
	if (!EvalGlobal(Context, WalkSource, "surface-walk.js", EvalError))
	{
		AddError(FString::Printf(TEXT("surface-walk.js threw: %s"), *EvalError));
		return false;
	}
	InjectedNames.Add(TEXT("CathodeSurfaceWalk"));

	if (!EvalGlobal(Context, FacultySource, "runtime-verify.js", EvalError))
	{
		AddError(FString::Printf(TEXT("runtime-verify.js threw: %s"), *EvalError));
		return false;
	}

	// 5) Exactly one self-report, parseable.
	if (!TestEqual(TEXT("the faculty emitted exactly one self-report"), GVerifyEmittedReports.Num(), 1))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Report;
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(GVerifyEmittedReports[0]);
		if (!FJsonSerializer::Deserialize(Reader, Report) || !Report.IsValid())
		{
			AddError(TEXT("self-report is not valid JSON"));
			return false;
		}
	}

	// --- schema + coherence ---
	FString Schema;
	TestTrue(TEXT("report has a schema"), Report->TryGetStringField(TEXT("schema"), Schema));
	TestEqual(TEXT("schema is the runtime-verification schema"), Schema, TEXT("cathode.runtime-verification/1"));

	const TSharedPtr<FJsonObject>* Coherence = nullptr;
	if (TestTrue(TEXT("report has coherence"), Report->TryGetObjectField(TEXT("coherence"), Coherence)) && Coherence)
	{
		FString ReportedBuildId;
		(*Coherence)->TryGetStringField(TEXT("buildId"), ReportedBuildId);
		// Round-trip: the reported anchor == the live cross-module aggregate.
		TestEqual(TEXT("coherence buildId == the live cross-module aggregate"), ReportedBuildId, Cathode::CathodeAggregateBuildId());
		// Completeness (visible in the aggregate string): the coherence-critical modules are folded in —
		// CathodeRuntime (the superset / negative-control anchor) and CathodeVerify (the faculty itself).
		TestTrue(TEXT("aggregate folds in CathodeRuntime"), ReportedBuildId.Contains(TEXT("CathodeRuntime=")));
		TestTrue(TEXT("aggregate folds in CathodeVerify"), ReportedBuildId.Contains(TEXT("CathodeVerify=")));

		// FULL completeness ASSERTED (not just logged): independently re-enumerate the loaded Cathode* modules
		// (the test's own FModuleManager query — not the mechanism's output) and assert the aggregate folds in
		// EVERY one. A dropped module fails here, not just in the logged line — closing the cross-module gap for
		// real (an uncovered module's relink would otherwise be an invisible coherence hole).
		TArray<FModuleStatus> LoadedModules;
		FModuleManager::Get().QueryModules(LoadedModules);
		int32 CathodeModuleCount = 0;
		for (const FModuleStatus& Mod : LoadedModules)
		{
			if (!Mod.bIsLoaded || !Mod.Name.StartsWith(TEXT("Cathode")))
			{
				continue;
			}
			++CathodeModuleCount;
			TestTrue(*FString::Printf(TEXT("aggregate folds in loaded module %s"), *Mod.Name),
				ReportedBuildId.Contains(Mod.Name + TEXT("=")));
		}
		TestTrue(TEXT("the loaded Cathode module set is non-trivial (runtime + faculty at least)"), CathodeModuleCount >= 2);
		// Validity: no folded module degraded to an unreadable PE (that would mask a coherence hole).
		TestFalse(TEXT("no folded module is unresolved"), ReportedBuildId.Contains(TEXT("=unresolved")));
		TestFalse(TEXT("no folded module is unavailable"), ReportedBuildId.Contains(TEXT("=unavailable")));

		// Surface the verified aggregate to the run log (greppable) so a relink is witnessable across runs.
		UE_LOG(LogCathodeVerify, Display, TEXT("[CATHODE-VERIFY-COHERENCE] aggregate buildId: %s"), *ReportedBuildId);
	}

	// --- surface ---
	const TSharedPtr<FJsonObject>* Surface = nullptr;
	if (!TestTrue(TEXT("report has surface"), Report->TryGetObjectField(TEXT("surface"), Surface)) || !Surface)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Exposed = nullptr;
	if (TestTrue(TEXT("surface has exposed"), (*Surface)->TryGetObjectField(TEXT("exposed"), Exposed)) && Exposed)
	{
		// Faculty-A finding: the bare production runtime exposes no DOM and none of the contract host-globals.
		bool bDomExposed = true;
		(*Exposed)->TryGetBoolField(TEXT("domExposed"), bDomExposed);
		TestFalse(TEXT("bare runtime: no DOM exposed (faculty-A under-exposure finding)"), bDomExposed);

		const TSharedPtr<FJsonObject>* HostGlobals = nullptr;
		if ((*Exposed)->TryGetObjectField(TEXT("hostGlobals"), HostGlobals) && HostGlobals)
		{
			for (const TCHAR* Name : { TEXT("console"), TEXT("setTimeout"), TEXT("clearTimeout"),
					TEXT("requestAnimationFrame"), TEXT("document"), TEXT("getComputedStyle") })
			{
				bool bPresent = true;
				(*HostGlobals)->TryGetBoolField(Name, bPresent);
				TestFalse(*FString::Printf(TEXT("bare runtime: host-global '%s' not exposed"), Name), bPresent);
			}
		}

		// THE [b] GATE (review's core requirement): the reported production surface excludes every harness
		// injection, AND the faculty's declared exclusion set covers exactly what this driver injects.
		const TSet<FString> GlobalNames = ArrayFieldToSet(*Exposed, TEXT("globalNames"));
		const TSet<FString> HarnessExcluded = ArrayFieldToSet(*Exposed, TEXT("harnessExcluded"));
		for (const FString& Injected : InjectedNames)
		{
			TestFalse(*FString::Printf(TEXT("production surface excludes harness injection '%s'"), *Injected),
				GlobalNames.Contains(Injected));
			TestTrue(*FString::Printf(TEXT("faculty's exclusion set covers harness injection '%s'"), *Injected),
				HarnessExcluded.Contains(Injected));
		}
	}

	// DOM + globals reconcile: DORMANT in 011 (no 012 descriptor) — declared + drift both null, and the
	// declaredSource points at the on-disk descriptor contract (SCHEMA.md) so 012 lands in lockstep.
	for (const TCHAR* Axis : { TEXT("dom"), TEXT("globals") })
	{
		const TSharedPtr<FJsonObject>* AxisObj = nullptr;
		if ((*Surface)->TryGetObjectField(Axis, AxisObj) && AxisObj)
		{
			const TSharedPtr<FJsonValue> Declared = (*AxisObj)->TryGetField(TEXT("declared"));
			TestTrue(*FString::Printf(TEXT("%s reconcile is dormant (declared null)"), Axis),
				Declared.IsValid() && Declared->IsNull());
			const TSharedPtr<FJsonValue> Drift = (*AxisObj)->TryGetField(TEXT("drift"));
			TestTrue(*FString::Printf(TEXT("%s reconcile is dormant (drift null)"), Axis),
				Drift.IsValid() && Drift->IsNull());
			FString DeclaredSource;
			(*AxisObj)->TryGetStringField(TEXT("declaredSource"), DeclaredSource);
			TestTrue(*FString::Printf(TEXT("%s declaredSource points at the on-disk descriptor contract"), Axis),
				DeclaredSource.Contains(TEXT("CathodeVerify/SCHEMA.md")));
		}
	}

	// CSS axis: DORMANT in [b], parallel to DOM/globals — declared null + drift null, declaredSource naming
	// the pending Cathode-OWN litehtml-grounded CSS declaration (authored later on the convention track; never
	// from panels/). Explicitly dormant, not a silent empty.
	const TSharedPtr<FJsonObject>* Css = nullptr;
	if ((*Surface)->TryGetObjectField(TEXT("css"), Css) && Css)
	{
		const TSharedPtr<FJsonValue> Declared = (*Css)->TryGetField(TEXT("declared"));
		TestTrue(TEXT("CSS reconcile is dormant (declared null)"), Declared.IsValid() && Declared->IsNull());
		const TSharedPtr<FJsonValue> Drift = (*Css)->TryGetField(TEXT("drift"));
		TestTrue(TEXT("CSS reconcile is dormant (drift null)"), Drift.IsValid() && Drift->IsNull());
		FString DeclaredSource;
		(*Css)->TryGetStringField(TEXT("declaredSource"), DeclaredSource);
		TestTrue(TEXT("CSS declaredSource names the pending Cathode-own CSS declaration"),
			DeclaredSource.Contains(TEXT("Cathode")) && DeclaredSource.Contains(TEXT("pending")));
	}

	// --- faculty-B: the capability framework ran its self-test green ---
	const TArray<TSharedPtr<FJsonValue>>* Capabilities = nullptr;
	if (TestTrue(TEXT("report has capabilities"), Report->TryGetArrayField(TEXT("capabilities"), Capabilities)) && Capabilities)
	{
		bool bSelfTestOk = false;
		for (const TSharedPtr<FJsonValue>& V : *Capabilities)
		{
			const TSharedPtr<FJsonObject>* Cap = nullptr;
			if (V.IsValid() && V->TryGetObject(Cap) && Cap)
			{
				FString CapName;
				(*Cap)->TryGetStringField(TEXT("name"), CapName);
				if (CapName == TEXT("framework-self-test"))
				{
					bool bOk = false, bExercised = false;
					(*Cap)->TryGetBoolField(TEXT("ok"), bOk);
					(*Cap)->TryGetBoolField(TEXT("exercised"), bExercised);
					bSelfTestOk = bOk && bExercised;
				}
			}
		}
		TestTrue(TEXT("faculty-B framework self-test exercised + ok"), bSelfTestOk);
	}

	GVerifyEmittedReports.Reset();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
