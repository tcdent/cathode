#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTeardown.h"

#include "Containers/StringConv.h"

#include <atomic>

// quickjs-ng — the embedded JS engine. Every JS_* / JSRuntime / JSContext token is confined to this
// translation unit; swapping engines means reimplementing this file and the factory, nothing else.
// The include path is wired by the execution-approval sprint's build (the header is vendored at
// vendor/lib/cathode/quickjs-ng/quickjs.h); no build runs this sprint.
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
		// TODO(eval/startup slice): JS_Eval(Context, ..., JS_EVAL_TYPE_GLOBAL | _MODULE), map a thrown
		// JSValue to FJsError, and JS_FreeValue the result. Not owned by Slice 2 (lifecycle only).
		return FEvalResult{};
	}

	virtual EJobPumpResult ExecutePendingJob() override
	{
		// TODO(Slice 4 — tick): JS_ExecutePendingJob(Runtime, &ctx) tri-state -> EJobPumpResult. The
		// drain-to-empty LOOP stays in the tick, not here, so the loop is independently testable.
		return EJobPumpResult::NoPending;
	}

	virtual bool TakePendingError(FJsError& OutError) override
	{
		// Vendor extraction: pull the engine's pending exception and map it to our FJsError. The mapping
		// is vendor-shaped (not unit-tested — behavioural proof is the execution sprint); the bespoke
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

	virtual void* GetNativeContextHandle() const override
	{
		// The live JSContext as an opaque void* for the JS Binding layer (sprint 007). The binding casts it
		// back to JSContext* and derives the runtime via JS_GetRuntime; 002's public header stays JS_*-free.
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

	void ReleaseHeldValues()
	{
		// First ordered teardown step. Release the JS-touching values Runtime Core holds before the
		// context is destroyed. The interrupt closure is one such held value (it captures our policy);
		// reset it here so nothing dangles. Later slices release DOM tree roots, host-bound callables,
		// and cached exceptions here too.
		InterruptHandler = nullptr;
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
