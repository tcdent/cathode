#include "CathodeContractGlobals.h"
#include "CathodeContractGlobalNames.h" // the dllexport decl of GetContractGlobalNames (so the def below exports)

#include "CathodeClock.h"
#include "CathodeDiagnostics.h"
#include "CathodeGlobalEnvironment.h"
#include "CathodeInterruptBudget.h"
#include "CathodeJsRuntime.h"
#include "CathodeScheduler.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{
namespace
{

// RAII hold of a dup'd JS callback value: JS_FreeValue'd when the last reference drops — i.e. when the
// scheduler's TFunction (which captures a TSharedRef to this) is destroyed, on fire (one-shot removal) or
// clear/cancel. CathodeRuntime-local (FJsValue lives in CathodeBinding, unreachable here by the circular dep).
struct FHeldJsValue
{
	JSContext* Ctx = nullptr;
	JSValue Value = JS_UNDEFINED;
	FHeldJsValue(JSContext* InCtx, JSValue InValue) : Ctx(InCtx), Value(InValue) {}
	~FHeldJsValue() { JS_FreeValue(Ctx, Value); }
};

FCathodeGlobalsContext* GlobalsOf(JSContext* Ctx)
{
	return static_cast<FCathodeGlobalsContext*>(JS_GetContextOpaque(Ctx));
}

// Invoke a held JS callback; a thrown callback is CONTAINED — routed to the sink, never propagated out.
void InvokeCallback(JSContext* Ctx, JSValueConst Fn, int Argc, JSValueConst* Argv)
{
	JSValue Result = JS_Call(Ctx, Fn, JS_UNDEFINED, Argc, const_cast<JSValueConst*>(Argv));
	if (JS_IsException(Result))
	{
		if (FCathodeGlobalsContext* G = GlobalsOf(Ctx))
		{
			FJsError Error;
			if (G->Runtime != nullptr && G->Sink != nullptr && G->Runtime->TakePendingError(Error))
			{
				G->Sink->ReportError(Error);
			}
		}
	}
	JS_FreeValue(Ctx, Result);
}

// --- console.{log,warn,error} -> the diagnostic sink at the level ---
template <EConsoleLevel Level>
JSValue ConsoleThunk(JSContext* Ctx, JSValueConst /*ThisVal*/, int Argc, JSValueConst* Argv)
{
	FCathodeGlobalsContext* G = GlobalsOf(Ctx);
	if (G != nullptr && G->Sink != nullptr)
	{
		FString Message;
		for (int Index = 0; Index < Argc; ++Index)
		{
			const char* Utf8 = JS_ToCString(Ctx, Argv[Index]);
			if (Utf8 != nullptr)
			{
				if (Index > 0) { Message += TEXT(" "); }
				Message += UTF8_TO_TCHAR(Utf8);
				JS_FreeCString(Ctx, Utf8);
			}
		}
		G->Sink->ReportConsole(Level, Message);
	}
	return JS_UNDEFINED;
}

// --- setTimeout(cb, ms) -> Scheduler.SetTimer(now + ms, cb) on the ONE clock; returns the cancel handle ---
JSValue SetTimeoutThunk(JSContext* Ctx, JSValueConst /*ThisVal*/, int Argc, JSValueConst* Argv)
{
	FCathodeGlobalsContext* G = GlobalsOf(Ctx);
	if (G == nullptr || G->Scheduler == nullptr || G->Clock == nullptr || Argc < 1)
	{
		return JS_NewInt32(Ctx, 0);
	}
	double DelayMs = 0.0;
	if (Argc >= 2)
	{
		JS_ToFloat64(Ctx, &DelayMs, Argv[1]);
	}
	if (!(DelayMs >= 0.0)) // NaN / negative -> 0 (browser-like)
	{
		DelayMs = 0.0;
	}
	const double DueSeconds = G->Clock->NowSeconds() + DelayMs / 1000.0; // absolute, the same clock the tick reads
	TSharedRef<FHeldJsValue> Held = MakeShared<FHeldJsValue>(Ctx, JS_DupValue(Ctx, Argv[0]));
	const int32 Handle = G->Scheduler->SetTimer(DueSeconds, [Ctx, Held]()
	{
		InvokeCallback(Ctx, Held->Value, 0, nullptr);
	});
	return JS_NewInt32(Ctx, Handle);
}

JSValue ClearTimeoutThunk(JSContext* Ctx, JSValueConst /*ThisVal*/, int Argc, JSValueConst* Argv)
{
	FCathodeGlobalsContext* G = GlobalsOf(Ctx);
	if (G != nullptr && G->Scheduler != nullptr && Argc >= 1)
	{
		int32 Handle = 0;
		JS_ToInt32(Ctx, &Handle, Argv[0]);
		G->Scheduler->ClearTimer(Handle);
	}
	return JS_UNDEFINED;
}

// --- requestAnimationFrame(cb) -> Scheduler.RequestAnimationFrame (one-shot, cb gets the frame time) ---
JSValue RequestAnimationFrameThunk(JSContext* Ctx, JSValueConst /*ThisVal*/, int Argc, JSValueConst* Argv)
{
	FCathodeGlobalsContext* G = GlobalsOf(Ctx);
	if (G == nullptr || G->Scheduler == nullptr || Argc < 1)
	{
		return JS_NewInt32(Ctx, 0);
	}
	TSharedRef<FHeldJsValue> Held = MakeShared<FHeldJsValue>(Ctx, JS_DupValue(Ctx, Argv[0]));
	const int32 Handle = G->Scheduler->RequestAnimationFrame([Ctx, Held](double NowSeconds)
	{
		JSValue Arg = JS_NewFloat64(Ctx, NowSeconds);
		InvokeCallback(Ctx, Held->Value, 1, &Arg);
		JS_FreeValue(Ctx, Arg);
	});
	return JS_NewInt32(Ctx, Handle);
}

JSValue CancelAnimationFrameThunk(JSContext* Ctx, JSValueConst /*ThisVal*/, int Argc, JSValueConst* Argv)
{
	FCathodeGlobalsContext* G = GlobalsOf(Ctx);
	if (G != nullptr && G->Scheduler != nullptr && Argc >= 1)
	{
		int32 Handle = 0;
		JS_ToInt32(Ctx, &Handle, Argv[0]);
		G->Scheduler->CancelAnimationFrame(Handle);
	}
	return JS_UNDEFINED;
}

// The SINGLE SOURCE for the CathodeContractGlobals global-level names: the installs below reference these
// entries (the JS_SetPropertyStr global name) and GetContractGlobalNames() exposes them, so the authoring .d.ts globals
// section + its guard BIND to the actual install set (an add/remove HERE flows through to the codegen + the guard).
static const char* const GContractGlobalNames[] = { "console", "setTimeout", "clearTimeout", "requestAnimationFrame", "cancelAnimationFrame" };

void InstallConsole(JSContext* Ctx)
{
	JSValue Global = JS_GetGlobalObject(Ctx);
	JSValue Console = JS_NewObject(Ctx);
	JS_SetPropertyStr(Ctx, Console, "log",   JS_NewCFunction(Ctx, &ConsoleThunk<EConsoleLevel::Log>,   "log",   1));
	JS_SetPropertyStr(Ctx, Console, "warn",  JS_NewCFunction(Ctx, &ConsoleThunk<EConsoleLevel::Warn>,  "warn",  1));
	JS_SetPropertyStr(Ctx, Console, "error", JS_NewCFunction(Ctx, &ConsoleThunk<EConsoleLevel::Error>, "error", 1));
	JS_SetPropertyStr(Ctx, Global, GContractGlobalNames[0], Console); // "console"
	JS_FreeValue(Ctx, Global);
}

void InstallTimers(JSContext* Ctx)
{
	JSValue Global = JS_GetGlobalObject(Ctx);
	JS_SetPropertyStr(Ctx, Global, GContractGlobalNames[1], JS_NewCFunction(Ctx, &SetTimeoutThunk,            "setTimeout",            2));
	JS_SetPropertyStr(Ctx, Global, GContractGlobalNames[2], JS_NewCFunction(Ctx, &ClearTimeoutThunk,          "clearTimeout",          1));
	JS_SetPropertyStr(Ctx, Global, GContractGlobalNames[3], JS_NewCFunction(Ctx, &RequestAnimationFrameThunk, "requestAnimationFrame", 1));
	JS_SetPropertyStr(Ctx, Global, GContractGlobalNames[4], JS_NewCFunction(Ctx, &CancelAnimationFrameThunk,  "cancelAnimationFrame",  1));
	JS_FreeValue(Ctx, Global);
}

} // namespace

// The install-set NAME accessor (the single source above). The authoring .d.ts codegen emits one global per
// name (signatures hand-authored per name); its guard asserts the .d.ts globals == this set — so a CathodeContractGlobals
// add/remove flows through. (document is installed separately via FDomDocument::InstallAsGlobal — a documented known
// the codegen adds beside this set, per-surface.)
TArray<FString> GetContractGlobalNames()
{
	TArray<FString> Names;
	for (const char* Name : GContractGlobalNames) { Names.Add(ANSI_TO_TCHAR(Name)); }
	return Names;
}

void AssembleContractGlobals(void* Context, FGlobalEnvironment& Env, FCathodeGlobalsContext& GlobalsCtx,
	ICathodeJsRuntime& Runtime, ICathodeDiagnosticSink& Sink, FCathodeScheduler& Scheduler, ICathodeClock& Clock)
{
	JSContext* Ctx = static_cast<JSContext*>(Context);
	if (Ctx == nullptr)
	{
		return;
	}
	GlobalsCtx.Runtime = &Runtime;
	GlobalsCtx.Sink = &Sink;
	GlobalsCtx.Scheduler = &Scheduler;
	GlobalsCtx.Clock = &Clock;
	JS_SetContextOpaque(Ctx, &GlobalsCtx); // the bare thunks recover the backings from here at call time

	// Set the two install seams (the owning console/timers wiring), then assemble in contract order. The seam
	// signatures hand the sink/scheduler; the backings are already on the context opaque, so the closures only
	// install the JS globals on the live context.
	Env.OnInstallConsole = [Ctx](ICathodeDiagnosticSink&) { InstallConsole(Ctx); };
	Env.OnInstallRejectionTracker = [&Runtime](ICathodeDiagnosticSink& RejectSink) { Runtime.InstallRejectionTracker(RejectSink); };
	Env.OnInstallTimers = [Ctx](FCathodeScheduler&) { InstallTimers(Ctx); };
	Env.AssembleGlobals(Sink, Scheduler);
}

FRuntimeStartup::FSteps BuildContractStartupSteps(FContractStartupDeps Deps)
{
	FRuntimeStartup::FSteps Steps;
	Steps.CreateRuntimeContext = [Deps]()
	{
		*Deps.Runtime = Deps.RuntimeFactory ? Deps.RuntimeFactory() : CreateJsRuntime();
	};
	Steps.InstallSafetyBudgets = [Deps]()
	{
		if (Deps.Runtime->IsValid())
		{
			constexpr SIZE_T DefaultMemoryLimitBytes = static_cast<SIZE_T>(64) * 1024 * 1024; // 64 MB
			constexpr SIZE_T DefaultMaxStackBytes = static_cast<SIZE_T>(256) * 1024;          // 256 KB
			ICathodeJsRuntime& RT = **Deps.Runtime;
			FInterruptBudget* Budget = Deps.InterruptBudget;
			RT.SetInterruptHandler([Budget]() { return Budget->ShouldInterrupt(); });
			RT.SetMemoryLimit(DefaultMemoryLimitBytes);
			RT.SetMaxStackSize(DefaultMaxStackBytes);
		}
	};
	Steps.AssembleGlobals = [Deps]()
	{
		// Set the console/timers seams + assemble on the LIVE context — the production reachability path.
		if (Deps.Runtime->IsValid())
		{
			void* NativeCtx = (*Deps.Runtime)->GetNativeContextHandle();
			AssembleContractGlobals(NativeCtx, *Deps.GlobalEnv, *Deps.GlobalsCtx, **Deps.Runtime,
				*Deps.Sink, *Deps.Scheduler, *Deps.Clock);
		}
	};
	Steps.LoadAuthorScript = []() {}; // SEAM: no pre-bundled author JS loader is wired (surfaces load their own script)
	Steps.BeginTicking = []() {};     // startup complete — the worker's tick loop begins after Startup()
	return Steps;
}

} // namespace Cathode
