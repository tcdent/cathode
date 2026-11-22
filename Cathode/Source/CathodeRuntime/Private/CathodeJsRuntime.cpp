#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTeardown.h"
#include "CathodeDiagnostics.h" // ICathodeDiagnosticSink::ReportError for the rejection report

#include "Containers/StringConv.h"

#include <atomic>

// quickjs-ng — the embedded JS engine. Every JS_* / JSRuntime / JSContext token is confined to this
// translation unit; swapping engines means reimplementing this file and the factory, nothing else.
THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogCathodeRuntime, Log, All);

namespace Cathode
{

#if WITH_DEV_AUTOMATION_TESTS
TFunction<void(ETeardownStep)> GTeardownObserver;
#endif

namespace
{

// Process-wide single-instance guard. The shared-global, many-surfaces model requires exactly one
// runtime + context, so a second CreateJsRuntime() is REFUSED (returns an IsValid()==false object),
// never a crash. The owning instance releases this on teardown.
std::atomic<bool> GJsRuntimeLive{ false };

/**
 * The quickjs-ng-backed JS runtime. Owns one JSRuntime + one JSContext as a unit. Fully file-local:
 * the only way to obtain one is CreateJsRuntime(), which enforces the single-instance guard.
 */
class FQuickJsRuntime final : public ICathodeJsRuntime
{
public:
	explicit FQuickJsRuntime(bool bClaimedInstance)
		: bOwnsInstanceClaim(bClaimedInstance)
	{
		if (bOwnsInstanceClaim)
		{
			Runtime = JS_NewRuntime();
			if (Runtime != nullptr)
			{
				Context = JS_NewContext(Runtime);
			}
		}
	}

	virtual ~FQuickJsRuntime() override
	{
		// TEARDOWN DISCIPLINE — strict order (see ETeardownStep): release every JS value Runtime Core
		// holds, THEN the context, THEN the runtime. Out-of-order shutdown is exactly where the
		// JS<->native lifetime hazard bites; the order is structural here, not incidental.
		ReleaseHeldValues();
		TapTeardown(ETeardownStep::ReleaseHeldValues);

		if (Context != nullptr)
		{
			JS_FreeContext(Context);
			Context = nullptr;
		}
		TapTeardown(ETeardownStep::FreeContext);

		if (Runtime != nullptr)
		{
			JS_FreeRuntime(Runtime);
			Runtime = nullptr;
		}
		TapTeardown(ETeardownStep::FreeRuntime);

		if (bOwnsInstanceClaim)
		{
			GJsRuntimeLive.store(false);
			bOwnsInstanceClaim = false;
		}
	}

	virtual bool IsValid() const override
	{
		return Runtime != nullptr && Context != nullptr;
	}

	virtual FEvalResult Evaluate(const FString& Source, const FString& SourceName, EEvalKind Kind) override
	{
		// Real JS_Eval (the shape is proven by CathodeContractGlobalsTests). FEvalResult carries
		// no vendor value (the Bridge's seam); we report ONLY whether it threw + the error. Both eval-type flags
		// are wired per Kind (Module is wired but has no caller/proof; the proof is on Global).
		FEvalResult Result;
		if (Context == nullptr)
		{
			return Result; // defensive (IsValid() guards this) — bThrew=false, no engine
		}

		const FTCHARToUTF8 SourceUtf8(*Source);
		const FTCHARToUTF8 NameUtf8(*SourceName);
		const int EvalFlags = (Kind == EEvalKind::Module) ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;

		JSValue EvalResult = JS_Eval(Context, SourceUtf8.Get(), SourceUtf8.Length(), NameUtf8.Get(), EvalFlags);
		if (JS_IsException(EvalResult))
		{
			Result.bThrew = true;
			TakePendingError(Result.Error); // pull the engine's pending exception -> Message/Stack
		}
		JS_FreeValue(Context, EvalResult); // free the result handle on EVERY path (the exception-sentinel free is a safe no-op)
		return Result;
	}

	virtual EJobPumpResult ExecutePendingJob() override
	{
		// The real one-job pump. The tri-state is CONFIRMED against pinned quickjs-ng v0.15.1/fd0a0210:
		// "return < 0 if exception, 0 if no job pending, 1 if a job was executed successfully". Single-context, so the
		// out *pctx == our Context; a faulted job's exception is retrieved by the existing TakePendingError (which the
		// tick's DrainMicrotasks routes to the diagnostic sink). The drain-to-empty LOOP stays in the tick — this is the
		// one-job primitive only.
		JSContext* JobContext = nullptr;
		const int Ret = JS_ExecutePendingJob(Runtime, &JobContext);
		if (Ret < 0) { return EJobPumpResult::Faulted; }
		if (Ret > 0) { return EJobPumpResult::Executed; }
		return EJobPumpResult::NoPending;
	}

	virtual bool TakePendingError(FJsError& OutError) override
	{
		// Vendor extraction: pull the engine's pending exception and map it to our FJsError. The mapping
		// is vendor-shaped (not unit-tested here); the bespoke
		// part is the ROUTING of this FJsError to the diagnostic sink, which lives in the tick.
		if (Context == nullptr)
		{
			return false;
		}

		JSValue Exception = JS_GetException(Context);
		if (JS_IsNull(Exception) || JS_IsUndefined(Exception))
		{
			JS_FreeValue(Context, Exception);
			return false;
		}

		if (const char* MessageUtf8 = JS_ToCString(Context, Exception))
		{
			OutError.Message = UTF8_TO_TCHAR(MessageUtf8);
			JS_FreeCString(Context, MessageUtf8);
		}

		JSValue Stack = JS_GetPropertyStr(Context, Exception, "stack");
		if (!JS_IsUndefined(Stack))
		{
			if (const char* StackUtf8 = JS_ToCString(Context, Stack))
			{
				OutError.Stack = UTF8_TO_TCHAR(StackUtf8);
				JS_FreeCString(Context, StackUtf8);
			}
		}
		JS_FreeValue(Context, Stack);
		JS_FreeValue(Context, Exception);
		return true;
	}

	virtual void SetInterruptHandler(FInterruptHandler Handler) override
	{
		// Store our policy closure (a held value, released in ReleaseHeldValues) and install the C thunk.
		// The thunk -> our policy adaptation is thin vendor wiring; the abort POLICY behind it is bespoke
		// (FInterruptBudget) and tested independently. STABILITY guard, not a security sandbox.
		InterruptHandler = MoveTemp(Handler);
		if (Runtime != nullptr)
		{
			// TODO-PREBUILD(signature): verify JS_SetInterruptHandler + the JSInterruptHandler typedef
			// against quickjs-ng — the thunk shape int(JSRuntime*, void*), the opaque arg passed as `this`,
			// and the return != 0 == abort convention vary between QuickJS forks.
			JS_SetInterruptHandler(Runtime, &FQuickJsRuntime::InterruptThunk, this);
		}
	}

	virtual void SetMemoryLimit(SIZE_T LimitBytes) override
	{
		// Vendor passthrough — the engine enforces it; not our logic, not tested here.
		if (Runtime != nullptr)
		{
			JS_SetMemoryLimit(Runtime, LimitBytes);
		}
	}

	virtual void SetMaxStackSize(SIZE_T StackBytes) override
	{
		// Vendor passthrough — the engine enforces it; not our logic, not tested here.
		if (Runtime != nullptr)
		{
			JS_SetMaxStackSize(Runtime, StackBytes);
		}
	}

	virtual void InstallRejectionTracker(ICathodeDiagnosticSink& Sink) override
	{
		// Install the host promise-rejection tracker. The sink reaches us via this seam (the runtime doesn't
		// hold it); the C thunk's opaque is `this` so the runtime owns the unhandled set + the report logic (like the
		// interrupt thunk). The thunk fires on the is_handled BOOL — NOT JS_ExecutePendingJob's return.
		RejectionSink = &Sink;
		if (Runtime != nullptr)
		{
			JS_SetHostPromiseRejectionTracker(Runtime, &FQuickJsRuntime::RejectionTrackerThunk, this);
		}
	}

	virtual void ReportUnhandledRejections() override
	{
		// Called by the tick AFTER the microtask drain-to-empty (the SETTLED boundary). The survivors — false-fired and
		// never-later-handled — are the genuinely-unhandled rejections: report each to the sink, free the dup, clear.
		if (RejectionSink != nullptr)
		{
			for (const TPair<void*, FUnhandledRejection>& Pair : UnhandledRejections)
			{
				RejectionSink->ReportError(Pair.Value.Error);
			}
		}
		if (Context != nullptr)
		{
			for (const TPair<void*, FUnhandledRejection>& Pair : UnhandledRejections)
			{
				JS_FreeValue(Context, Pair.Value.Promise);
			}
		}
		UnhandledRejections.Empty();
	}

	virtual void* GetNativeContextHandle() const override
	{
		// The live JSContext as an opaque void* for the JS Binding layer. The binding casts it
		// back to JSContext* and derives the runtime via JS_GetRuntime; CathodeRuntime's public header stays JS_*-free.
		return Context;
	}

private:
	// C thunk for JS_SetInterruptHandler (signature: int(JSRuntime*, void* opaque)); opaque is `this`.
	// Adapts the engine's call to our stored policy closure — thin vendor glue, returns nonzero to abort.
	static int InterruptThunk(JSRuntime* /*Rt*/, void* Opaque)
	{
		FQuickJsRuntime* Self = static_cast<FQuickJsRuntime*>(Opaque);
		if (Self != nullptr && Self->InterruptHandler)
		{
			return Self->InterruptHandler() ? 1 : 0;
		}
		return 0;
	}

	// C thunk for JS_SetHostPromiseRejectionTracker. Signature MUST match the vendored typedef EXACTLY
	// (quickjs.h:1134): void(JSContext*, JSValueConst promise, JSValueConst reason, bool is_handled, void* opaque) —
	// quickjs-ng spells is_handled as C++ `bool` (NOT JS_BOOL). opaque is `this`. Adapts to the deferred-report below.
	static void RejectionTrackerThunk(JSContext* Ctx, JSValueConst Promise, JSValueConst Reason, bool bIsHandled, void* Opaque)
	{
		FQuickJsRuntime* Self = static_cast<FQuickJsRuntime*>(Opaque);
		if (Self != nullptr)
		{
			Self->OnPromiseRejection(Ctx, Promise, Reason, bIsHandled);
		}
	}

	void OnPromiseRejection(JSContext* Ctx, JSValueConst Promise, JSValueConst Reason, bool bIsHandled)
	{
		void* const Key = JS_VALUE_GET_PTR(Promise); // the promise object ptr — a stable per-promise key
		if (bIsHandled)
		{
			// A handler was added LATER -> this rejection is NOT unhandled after all. Remove it + free the dup we held.
			if (FUnhandledRejection* Existing = UnhandledRejections.Find(Key))
			{
				JS_FreeValue(Ctx, Existing->Promise);
				UnhandledRejections.Remove(Key);
			}
			return;
		}
		// is_handled == FALSE: rejected with no handler (yet). Track it — DUP the promise (keep it alive as the held key)
		// + capture the reason NOW (the reason value is only valid for this call). The REPORT is deferred to the drain.
		if (UnhandledRejections.Contains(Key)) { return; } // defensive — quickjs fires the false edge once per promise
		FUnhandledRejection Entry;
		Entry.Promise = JS_DupValue(Ctx, Promise);
		ExtractError(Ctx, Reason, Entry.Error);
		UnhandledRejections.Add(Key, MoveTemp(Entry));
	}

	// Map a JS value (a rejection reason) to our FJsError — message + stack (mirrors TakePendingError, but from a given
	// value rather than the pending exception). Vendor-shaped extraction; JS_GetPropertyStr on a non-object is undefined-safe.
	static void ExtractError(JSContext* Ctx, JSValueConst Value, FJsError& Out)
	{
		if (const char* MessageUtf8 = JS_ToCString(Ctx, Value))
		{
			Out.Message = UTF8_TO_TCHAR(MessageUtf8);
			JS_FreeCString(Ctx, MessageUtf8);
		}
		JSValue Stack = JS_GetPropertyStr(Ctx, Value, "stack");
		if (!JS_IsUndefined(Stack))
		{
			if (const char* StackUtf8 = JS_ToCString(Ctx, Stack))
			{
				Out.Stack = UTF8_TO_TCHAR(StackUtf8);
				JS_FreeCString(Ctx, StackUtf8);
			}
		}
		JS_FreeValue(Ctx, Stack);
	}

	void ReleaseHeldValues()
	{
		// First ordered teardown step. Release the JS-touching values Runtime Core holds before the
		// context is destroyed. The interrupt closure is one such held value (it captures our policy);
		// reset it here so nothing dangles.
		InterruptHandler = nullptr;

		// Free the dup'd promises held for unhandled-rejection tracking BEFORE the context is destroyed
		// (the JS<->native lifetime hazard: a dup outliving its context is exactly the out-of-order teardown bite).
		if (Context != nullptr)
		{
			for (const TPair<void*, FUnhandledRejection>& Pair : UnhandledRejections)
			{
				JS_FreeValue(Context, Pair.Value.Promise);
			}
		}
		UnhandledRejections.Empty();
		RejectionSink = nullptr;
	}

	void TapTeardown(ETeardownStep Step)
	{
#if WITH_DEV_AUTOMATION_TESTS
		if (GTeardownObserver)
		{
			GTeardownObserver(Step);
		}
#else
		(void)Step;
#endif
	}

	JSRuntime* Runtime = nullptr;
	JSContext* Context = nullptr;
	bool bOwnsInstanceClaim = false;
	FInterruptHandler InterruptHandler; // our policy closure; released first in teardown

	// Author-JS unhandled-rejection tracking. JS_SetHostPromiseRejectionTracker fires (is_handled=FALSE) when a
	// promise rejects with no handler, and fires AGAIN (is_handled=TRUE) if a handler is added LATER (the double-fire). We
	// DEFER the report: this map holds the dup'd promise + its captured reason; a later handle REMOVES it (free the dup);
	// ReportUnhandledRejections (called by the tick AFTER the microtask drain-to-empty — the SETTLED boundary, once no jobs
	// pend a still-unhandled rejection is genuinely unhandled) reports the SURVIVORS to the sink + clears. The is_handled
	// BOOL is the fire-decision — NOT JS_ExecutePendingJob's tri-state return (a throwing-but-CAUGHT .then returns 1/Executed).
	struct FUnhandledRejection { JSValue Promise; FJsError Error; };
	TMap<void*, FUnhandledRejection> UnhandledRejections; // key: the promise object ptr (JS_VALUE_GET_PTR), stable per promise
	ICathodeDiagnosticSink* RejectionSink = nullptr;      // set at install (the seam, where the sink is available); not owned
};

} // namespace

TUniquePtr<ICathodeJsRuntime> CreateJsRuntime()
{
	bool bExpected = false;
	if (!GJsRuntimeLive.compare_exchange_strong(bExpected, true))
	{
		// One runtime is already live — refuse the second. Stability, not a crash: the returned object
		// reports IsValid()==false and the caller decides what to do.
		UE_LOG(LogCathodeRuntime, Error,
			TEXT("CreateJsRuntime: a Cathode JS runtime already exists; refusing a second instance ")
			TEXT("(the shared-global, many-surfaces model requires exactly one)."));
		return MakeUnique<FQuickJsRuntime>(/*bClaimedInstance=*/false);
	}

	TUniquePtr<FQuickJsRuntime> Instance = MakeUnique<FQuickJsRuntime>(/*bClaimedInstance=*/true);
	if (!Instance->IsValid())
	{
		// Engine init failed after we claimed the slot; the instance owns the claim and releases it on
		// destruction, so the slot is not stranded.
		UE_LOG(LogCathodeRuntime, Error, TEXT("CreateJsRuntime: JS engine initialization failed."));
	}
	return Instance;
}

} // namespace Cathode
