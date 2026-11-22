#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

// The Cathode JS-runtime subsystem: our internal contract over the embedded JavaScript engine
// (quickjs-ng). This header is the SWAP BOUNDARY — the vendor engine's own types
// (JS_*, JSValue, JSContext, JSRuntime, ...) appear ONLY in the implementation, never here, so
// the engine can be replaced without touching Runtime Core.
//
// It sits at a deliberately MODERATELY-PRIMITIVE altitude: it exposes the handful of operations
// Runtime Core's orchestration actually needs (evaluate a script, pump one pending job, set the
// safety budgets, surface an error) — NOT a 1:1 re-export of the engine's C API, and NOT a
// re-creation of a JS engine. The loops, phase ordering, and budget *policy* that sit on top are
// Runtime Core's bespoke logic, authored against this seam — not part of it.

namespace Cathode
{

/** How a script is evaluated (maps to the engine's eval-type, expressed in our terms). */
enum class EEvalKind : uint8
{
	Global, // top-level global script
	Module, // ES module
};

/** Result of pumping a single pending job (microtask). Maps the engine's tri-state job return. */
enum class EJobPumpResult : uint8
{
	NoPending, // queue empty — nothing ran
	Executed,  // one job ran to completion
	Faulted,   // a job threw; the exception is retrievable via TakePendingError()
};

/** An error surfaced from the engine (an uncaught exception), in our types — no vendor handle. */
struct FJsError
{
	FString Message;
	FString Stack;
};

/** Result of evaluating a script. Carries no vendor value: value handling belongs to the
 *  JS<->Native Bridge; Runtime Core only needs to know whether evaluation threw and the error. */
struct FEvalResult
{
	bool bThrew = false;
	FJsError Error;
};

/** Our interrupt hook: the engine calls it periodically during long-running execution; returning
 *  true aborts the running script. The *budget* this encodes (instruction/time) is Runtime Core's
 *  bespoke policy — this is only the typed callback the engine invokes. */
using FInterruptHandler = TFunction<bool()>;

// The host promise-rejection tracker's report target. Full type in CathodeDiagnostics.h (Private); the public
// interface only forward-declares it (in-module callers — the contract-globals seam, the tick — include the full type).
class ICathodeDiagnosticSink;

/**
 * One JavaScript runtime plus its global context, owned as a single logical unit. Exactly one of
 * these exists per Cathode runtime — the single-instance guarantee is Runtime Core's to enforce,
 * not this object's. Construction stands the engine up; destruction tears it down in the
 * engine-required order (context before runtime). All methods must be called on the owning worker
 * thread; thread affinity is Runtime Core's invariant.
 */
class ICathodeJsRuntime
{
public:
	virtual ~ICathodeJsRuntime() = default;

	/** True once the runtime + context are stood up and usable. */
	virtual bool IsValid() const = 0;

	/** Evaluate a script in the global context. Returns whether it threw and the error; the
	 *  produced value is intentionally not surfaced here (the Bridge's concern, not Runtime Core's). */
	virtual FEvalResult Evaluate(const FString& Source, const FString& SourceName, EEvalKind Kind) = 0;

	/** Execute exactly one pending job (microtask). Runtime Core's tick loops this to drain the
	 *  queue to empty — that drain LOOP is bespoke tick logic, deliberately not folded into this
	 *  primitive so the loop stays independently testable. */
	virtual EJobPumpResult ExecutePendingJob() = 0;

	/** Retrieve and clear the engine's current pending exception, if any. */
	virtual bool TakePendingError(FJsError& OutError) = 0;

	// --- Safety budgets (typed hooks; the values and policy behind them are Runtime Core's) ---

	/** Install the interrupt hook the engine calls during long execution. */
	virtual void SetInterruptHandler(FInterruptHandler Handler) = 0;

	/** Per-runtime memory ceiling, in bytes. */
	virtual void SetMemoryLimit(SIZE_T LimitBytes) = 0;

	/** Per-runtime stack ceiling, in bytes. */
	virtual void SetMaxStackSize(SIZE_T StackBytes) = 0;

	/** Install the host promise-rejection tracker, routing GENUINELY-unhandled author-JS rejections to the
	 *  diagnostic sink. Set via the contract-globals seam (where the sink is available). Non-pure default no-op so
	 *  mock/non-engine runtimes stay unchanged; the engine-backed runtime overrides it. */
	virtual void InstallRejectionTracker(ICathodeDiagnosticSink& /*Sink*/) {}

	/** Report the unhandled rejections that survived the microtask drain (the SETTLED boundary) to the sink +
	 *  clear. The tick calls this AFTER its drain-to-empty. Non-pure default no-op (mocks unchanged). */
	virtual void ReportUnhandledRejections() {}

	/** The live JS context as an OPAQUE handle, for the JS Binding layer. Returns the engine's
	 *  context cast to void* so this header stays JS_*-free; CathodeBinding casts it back internally and
	 *  derives the runtime from it. Worker-thread-only (the runtime's thread-affinity invariant). Defaults to
	 *  null — a mock/non-engine runtime has no context; the engine-backed runtime overrides this to return
	 *  its real context. (Non-pure so existing mock implementers stay unchanged.) */
	virtual void* GetNativeContextHandle() const { return nullptr; }
};

/**
 * Construct the concrete JS runtime (quickjs-ng-backed). This factory is the single place
 * the vendor choice is bound — swapping JS engines swaps only its implementation. Never returns
 * null; on engine-init failure the returned object reports IsValid() == false.
 */
CATHODERUNTIME_API TUniquePtr<ICathodeJsRuntime> CreateJsRuntime();

} // namespace Cathode
