#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

#include "CathodeClock.h"
#include "CathodeContractGlobals.h"
#include "CathodeDiagnostics.h"
#include "CathodeGlobalEnvironment.h"
#include "CathodeInterruptBudget.h"
#include "CathodeJsRuntime.h"
#include "CathodeMessageQueue.h"
#include "CathodeRuntimeStartup.h"
#include "CathodeScheduler.h"
#include "CathodeThreadAffinity.h"
#include "CathodeTick.h"

#include <atomic>

class FEvent;
class FRunnableThread;

namespace Cathode
{

// The dedicated worker thread that OWNS the JS runtime. The runtime is created, used, and torn down
// ONLY on this thread (the single-thread invariant); the JS heap is never touched from another thread.
// The host communicates with it solely through the thread-safe message boundary — inbound events/state
// in, outbound "frame ready" out. The clock and the runtime factory are injectable so Slice 4's tick
// can be exercised deterministically against a fake clock and a recording runtime, running the real
// orchestration rather than a forked test path.
class FCathodeRuntimeWorker final : public FRunnable
{
public:
	using FRuntimeFactory = TFunction<TUniquePtr<ICathodeJsRuntime>()>;

	explicit FCathodeRuntimeWorker(
		TUniquePtr<ICathodeClock> InClock = MakeUnique<FPlatformClock>(),
		FRuntimeFactory InRuntimeFactory = FRuntimeFactory(&CreateJsRuntime));

	virtual ~FCathodeRuntimeWorker() override;

	/** Start the worker thread. */
	void Start();

	/** Request stop and block until the worker thread has fully exited (the runtime is torn down on it). */
	void Shutdown();

	/** Post an inbound message from any thread (host -> worker); wakes the worker if it is idle. */
	void PostInbound(FHostInboundMessage Message);

	/** Drain outbound notifications (worker -> host); call on the consuming (game) thread. */
	int32 DrainOutbound(TArray<FHostOutboundMessage>& Out) { return Outbound.Drain(Out); }

	/** True only on the worker thread — the single-thread invariant guard. */
	bool IsOnWorkerThread() const { return Affinity.IsOnBoundThread(); }

	/** The timer/rAF scheduler the JS setTimeout / requestAnimationFrame globals wire onto (Slice 6). */
	FCathodeScheduler& GetScheduler() { return Scheduler; }

	/** Install the phase-5 relayout seam (the web-engine / Surfaces components wire this; default unset). */
	void SetSurfaceRelayoutSeam(FSurfaceRelayoutSeam Seam) { RelayoutSeam = MoveTemp(Seam); }

	/** The interrupt-budget policy (configure the time budget; default FInterruptBudget::DefaultTimeBudgetSeconds). */
	FInterruptBudget& GetInterruptBudget() { return InterruptBudget; }

	/** Override the diagnostic sink uncaught errors route to (default: the game log). Null restores the default. */
	void SetDiagnosticSink(ICathodeDiagnosticSink* InSink) { DiagnosticSink = InSink != nullptr ? InSink : &DefaultSink; }

	/** The global-environment assembler the contract-global components (console/DOM/Host Bridge) wire their
	 *  install seams onto (future). Runtime Core sequences the assembly; it does not implement the globals. */
	FGlobalEnvironment& GetGlobalEnvironment() { return GlobalEnv; }

	// FRunnable
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override; // signal the loop to exit
	virtual void Exit() override;

private:
	TUniquePtr<ICathodeClock> Clock;
	FRuntimeFactory RuntimeFactory;
	FThreadAffinity Affinity;

	TCathodeMessageQueue<FHostInboundMessage> Inbound;
	TCathodeMessageQueue<FHostOutboundMessage> Outbound;
	FCathodeScheduler Scheduler;
	FSurfaceRelayoutSeam RelayoutSeam;
	FInterruptBudget InterruptBudget; // bespoke abort policy; references *Clock (declared before it)
	FLogDiagnosticSink DefaultSink;
	ICathodeDiagnosticSink* DiagnosticSink = &DefaultSink; // injectable error-routing target
	FGlobalEnvironment GlobalEnv; // contract-global assembly orchestration (console/timers seams set at assembly)
	FCathodeGlobalsContext GlobalsCtx; // S4: backings the global thunks recover via the context opaque (lives as
	                                   // long as the worker -> outlives the runtime/context the thunks run on)

	TUniquePtr<ICathodeJsRuntime> Runtime; // worker-thread-only
	FRunnableThread* Thread = nullptr;
	FEvent* WakeEvent = nullptr; // event-driven wake: idle until inbound/timer/raf
	std::atomic<bool> bStopRequested{ false };
};

} // namespace Cathode
