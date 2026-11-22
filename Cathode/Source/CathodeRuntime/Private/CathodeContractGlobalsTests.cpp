#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeClock.h"
#include "CathodeContractGlobals.h"
#include "CathodeDiagnostics.h"
#include "CathodeGlobalEnvironment.h"
#include "CathodeInterruptBudget.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeStartup.h"
#include "CathodeScheduler.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// The standard globals (console + timers/rAF). Each global is exercised on the LIVE
// runtime context, installed through the SHARED production assembly (BuildContractStartupSteps + FRuntimeStartup
// — the SAME builder the worker's Run() uses), NOT a test-only hand-install: would-it-pass-if-test-only? NO.

namespace
{
using namespace Cathode;

// A settable fake clock — the ONE clock for BOTH setTimeout's now-source and the FireDueTimers / rAF driving
// (the same-clock discriminator: advancing this one clock past the deadline is what fires the timer).
class FFakeClock final : public ICathodeClock
{
public:
	double Now = 0.0;
	virtual double NowSeconds() const override { return Now; }
};

// Records console.* routed through the sink (the OnInstallConsole target) so the oracle asserts the sink
// actually received the leveled message via the PRODUCTION install. (The production FLogDiagnosticSink is a
// REAL override too — see CathodeDiagnostics.cpp — routing to the game log at level; this recorder captures.)
class FRecordingConsoleSink final : public ICathodeDiagnosticSink
{
public:
	struct FEntry { EConsoleLevel Level; FString Message; };
	TArray<FEntry> Console;
	TArray<FString> Errors;
	virtual void ReportError(const FJsError& Error) override { Errors.Add(Error.Message); }
	virtual void ReportConsole(EConsoleLevel Level, const FString& Message) override { Console.Add({ Level, Message }); }
};

void EvalGlobal(JSContext* Ctx, const char* Src)
{
	JSValue Result = JS_Eval(Ctx, Src, FCStringAnsi::Strlen(Src), "<globals-test>", JS_EVAL_TYPE_GLOBAL);
	JS_FreeValue(Ctx, Result);
}

int32 EvalInt(JSContext* Ctx, const char* Src)
{
	JSValue Result = JS_Eval(Ctx, Src, FCStringAnsi::Strlen(Src), "<globals-test>", JS_EVAL_TYPE_GLOBAL);
	int32 Out = -1;
	JS_ToInt32(Ctx, &Out, Result);
	JS_FreeValue(Ctx, Result);
	return Out;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeContractGlobalsTest,
	"Cathode.Runtime.ContractGlobals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeContractGlobalsTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// Runtime declared FIRST so it destructs LAST: the Scheduler (declared after) destructs first, freeing the
	// held JS callbacks (JS_FreeValue) while the context is still alive.
	TUniquePtr<ICathodeJsRuntime> Runtime;
	FFakeClock Clock;            // the ONE clock
	Clock.Now = 1000.0;
	FInterruptBudget Budget(Clock);
	FGlobalEnvironment Env;
	FCathodeGlobalsContext GlobalsCtx;
	FRecordingConsoleSink Sink;
	FCathodeScheduler Scheduler; // the SAME scheduler the thunks target AND the test drives

	// PRODUCTION REACHABILITY: build the steps via the SHARED builder the worker uses, on a REAL runtime.
	FContractStartupDeps Deps;
	Deps.Runtime = &Runtime;
	Deps.RuntimeFactory = TFunction<TUniquePtr<ICathodeJsRuntime>()>(&CreateJsRuntime);
	Deps.InterruptBudget = &Budget;
	Deps.Clock = &Clock;
	Deps.GlobalEnv = &Env;
	Deps.GlobalsCtx = &GlobalsCtx;
	Deps.Sink = &Sink;
	Deps.Scheduler = &Scheduler;

	FRuntimeStartup Startup(BuildContractStartupSteps(Deps));
	Startup.Startup();

	if (!Runtime.IsValid() || !Runtime->IsValid()) { AddError(TEXT("runtime failed to start")); return false; }
	JSContext* Ctx = static_cast<JSContext*>(Runtime->GetNativeContextHandle());
	if (Ctx == nullptr) { AddError(TEXT("runtime exposes no native context")); return false; }

	// The globals are reachable on the LIVE context (installed by the production assembly, not a hand-install).
	TestEqual(TEXT("console installed (object) via the production assembly"), EvalInt(Ctx, "typeof console === 'object' ? 1 : 0"), 1);
	TestEqual(TEXT("setTimeout installed (function)"), EvalInt(Ctx, "typeof setTimeout === 'function' ? 1 : 0"), 1);
	TestEqual(TEXT("clearTimeout installed (function)"), EvalInt(Ctx, "typeof clearTimeout === 'function' ? 1 : 0"), 1);
	TestEqual(TEXT("requestAnimationFrame installed (function)"), EvalInt(Ctx, "typeof requestAnimationFrame === 'function' ? 1 : 0"), 1);
	TestEqual(TEXT("cancelAnimationFrame installed (function)"), EvalInt(Ctx, "typeof cancelAnimationFrame === 'function' ? 1 : 0"), 1);

	// --- console.{log,warn,error} -> the sink at the level ---
	EvalGlobal(Ctx, "console.log('hello'); console.warn('careful'); console.error('boom');");
	if (TestEqual(TEXT("3 console messages reached the sink"), Sink.Console.Num(), 3))
	{
		TestEqual(TEXT("console.log -> Log level"), static_cast<int32>(Sink.Console[0].Level), static_cast<int32>(EConsoleLevel::Log));
		TestEqual(TEXT("console.log message routed"), Sink.Console[0].Message, FString(TEXT("hello")));
		TestEqual(TEXT("console.warn -> Warn level"), static_cast<int32>(Sink.Console[1].Level), static_cast<int32>(EConsoleLevel::Warn));
		TestEqual(TEXT("console.warn message routed"), Sink.Console[1].Message, FString(TEXT("careful")));
		TestEqual(TEXT("console.error -> Error level"), static_cast<int32>(Sink.Console[2].Level), static_cast<int32>(EConsoleLevel::Error));
		TestEqual(TEXT("console.error message routed"), Sink.Console[2].Message, FString(TEXT("boom")));
	}

	// --- setTimeout: NEGATIVE-then-POSITIVE (anti-vacuous) + one-shot, on the ONE clock (due = 1000.0 + 0.1) ---
	EvalGlobal(Ctx, "globalThis.fired = 0; setTimeout(function(){ globalThis.fired++; }, 100);");
	Clock.Now = 1000.05; Scheduler.FireDueTimers(Clock.Now); // SHORT of the deadline
	TestEqual(TEXT("setTimeout: cb does NOT fire before the deadline"), EvalInt(Ctx, "globalThis.fired"), 0);
	Clock.Now = 1000.15; Scheduler.FireDueTimers(Clock.Now); // PAST the deadline
	TestEqual(TEXT("setTimeout: cb fires after the deadline"), EvalInt(Ctx, "globalThis.fired"), 1);
	Clock.Now = 1000.25; Scheduler.FireDueTimers(Clock.Now); // one-shot: not again
	TestEqual(TEXT("setTimeout: cb fires EXACTLY once (one-shot)"), EvalInt(Ctx, "globalThis.fired"), 1);

	// --- clearTimeout before the deadline -> never fires ---
	EvalGlobal(Ctx, "globalThis.cleared = 0; var ch = setTimeout(function(){ globalThis.cleared++; }, 100); clearTimeout(ch);");
	Clock.Now = 1000.50; Scheduler.FireDueTimers(Clock.Now);
	TestEqual(TEXT("clearTimeout: cb never fires"), EvalInt(Ctx, "globalThis.cleared"), 0);

	// --- requestAnimationFrame: ONE-SHOT re-register over >= 2 frames (a re-request fires the NEXT frame, once
	//     per frame — not 0, not twice in one tick) ---
	EvalGlobal(Ctx, "globalThis.raf = 0; function frame(){ globalThis.raf++; requestAnimationFrame(frame); } requestAnimationFrame(frame);");
	TestEqual(TEXT("rAF: not fired before any frame"), EvalInt(Ctx, "globalThis.raf"), 0);
	Scheduler.RunAnimationFrameCallbacks(Clock.Now);
	TestEqual(TEXT("rAF: fires once on the first frame (re-request deferred, not same-frame)"), EvalInt(Ctx, "globalThis.raf"), 1);
	Scheduler.RunAnimationFrameCallbacks(Clock.Now);
	TestEqual(TEXT("rAF: the re-requested cb fires on the NEXT frame (one per frame)"), EvalInt(Ctx, "globalThis.raf"), 2);

	// --- cancelAnimationFrame -> never fires ---
	EvalGlobal(Ctx, "globalThis.rafc = 0; var rh = requestAnimationFrame(function(){ globalThis.rafc++; }); cancelAnimationFrame(rh);");
	Scheduler.RunAnimationFrameCallbacks(Clock.Now);
	TestEqual(TEXT("cancelAnimationFrame: cb never fires"), EvalInt(Ctx, "globalThis.rafc"), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
