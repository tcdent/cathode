#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

#include "CathodeRuntimeStartup.h" // FRuntimeStartup::FSteps

namespace Cathode
{

class ICathodeJsRuntime;
class ICathodeDiagnosticSink;
class FCathodeScheduler;
class ICathodeClock;
class FGlobalEnvironment;
class FInterruptBudget;

// Sprint 012 slice 4 — the standard globals (console + timers/rAF) binding.
//
// The C++ backings the bare global thunks recover AT CALL TIME via JS_GetContextOpaque — globals have no
// 'this' object to carry an opaque, so the context opaque is the channel (the globals analog of the DOM's
// class-id + JS_GetOpaque2(this_val)). Stashed on the JSContext by AssembleContractGlobals; owned by the
// worker (or, in the reachability test, the test) for the runtime's lifetime.
struct FCathodeGlobalsContext
{
	ICathodeJsRuntime* Runtime = nullptr;   // for TakePendingError (contained fault routing from a callback)
	ICathodeDiagnosticSink* Sink = nullptr; // console.* target
	FCathodeScheduler* Scheduler = nullptr; // setTimeout / requestAnimationFrame target
	ICathodeClock* Clock = nullptr;         // setTimeout's now-source — the ONE worker clock the tick also reads
};

// THE production globals install: stash GlobalsCtx on the context, set the console + timers install seams on
// Env (each installing its globals on the LIVE global object — console.{log,warn,error}; setTimeout /
// clearTimeout / requestAnimationFrame / cancelAnimationFrame — with thunks routing via the context opaque),
// then run AssembleGlobals in contract order. CathodeRuntime-internal, RAW quickjs: CathodeBinding's
// installer is class-proto-only (N/A to globals) AND CathodeBinding depends on CathodeRuntime (a circular
// dep the other way), so the globals bind here directly. Context is the JSContext* (the runtime's native
// context handle), taken as void* to keep quickjs out of this header.
void AssembleContractGlobals(
	void* Context,
	FGlobalEnvironment& Env,
	FCathodeGlobalsContext& GlobalsCtx,
	ICathodeJsRuntime& Runtime,
	ICathodeDiagnosticSink& Sink,
	FCathodeScheduler& Scheduler,
	ICathodeClock& Clock);

// The components the shared startup-steps builder needs — refs to the owner's (worker or test) objects.
struct FContractStartupDeps
{
	TUniquePtr<ICathodeJsRuntime>* Runtime = nullptr;          // CreateRuntimeContext writes the runtime here
	TFunction<TUniquePtr<ICathodeJsRuntime>()> RuntimeFactory; // injectable (real CreateJsRuntime by default)
	FInterruptBudget* InterruptBudget = nullptr;
	ICathodeClock* Clock = nullptr;
	FGlobalEnvironment* GlobalEnv = nullptr;
	FCathodeGlobalsContext* GlobalsCtx = nullptr;
	ICathodeDiagnosticSink* Sink = nullptr;
	FCathodeScheduler* Scheduler = nullptr;
};

// THE ONE production startup-steps construction (011 postmortem #3 forward-seam, now due): the worker's Run()
// AND the slice-4 reachability test build their FRuntimeStartup steps via THIS — never a forked
// re-implementation. The AssembleGlobals step runs AssembleContractGlobals on the live context, so a test
// that drives these steps drives the PRODUCTION install (would-it-pass-if-the-install-were-test-only = NO).
FRuntimeStartup::FSteps BuildContractStartupSteps(FContractStartupDeps Deps);

} // namespace Cathode
