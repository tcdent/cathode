#include "CathodeRuntimeWorker.h"

#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"

// Distinct from LogCathodeRuntime (the runtime impl's category): the worker/threading concern logs
// here. JS_* never appears in this file — the worker talks to the engine only through ICathodeJsRuntime.
DEFINE_LOG_CATEGORY_STATIC(LogCathodeWorker, Log, All);

namespace Cathode
{

FCathodeRuntimeWorker::FCathodeRuntimeWorker(TUniquePtr<ICathodeClock> InClock, FRuntimeFactory InRuntimeFactory)
	: Clock(MoveTemp(InClock))
	, RuntimeFactory(MoveTemp(InRuntimeFactory))
	, InterruptBudget(*Clock) // Clock is declared (and thus initialized) first; the budget reads it
{
	// Auto-reset event: PostInbound (and Stop) trigger it to wake an idle worker.
	WakeEvent = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
}

FCathodeRuntimeWorker::~FCathodeRuntimeWorker()
{
	Shutdown();
	if (WakeEvent != nullptr)
	{
		FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
		WakeEvent = nullptr;
	}
}

void FCathodeRuntimeWorker::PostInbound(FHostInboundMessage Message)
{
	Inbound.Enqueue(MoveTemp(Message));
	if (WakeEvent != nullptr)
	{
		WakeEvent->Trigger(); // wake the worker if it is idle-blocked
	}
}

void FCathodeRuntimeWorker::Start()
{
	if (Thread == nullptr)
	{
		bStopRequested.store(false);
		Thread = FRunnableThread::Create(this, TEXT("CathodeWorker"));
	}
}

void FCathodeRuntimeWorker::Shutdown()
{
	if (Thread != nullptr)
	{
		// Kill(bShouldWait=true) calls Stop() then blocks until Run() returns — so the runtime is torn
		// down on the worker thread (its ordered teardown) before we delete the thread.
		Thread->Kill(/*bShouldWait=*/true);
		delete Thread;
		Thread = nullptr;
	}
}

bool FCathodeRuntimeWorker::Init()
{
	return true;
}

uint32 FCathodeRuntimeWorker::Run()
{
	// Single-thread invariant: bind affinity so the runtime is created/used/torn-down only on this thread.
	Affinity.BindToCurrentThread();

	// Ordered startup (the doc lifecycle), as the ONE FRuntimeStartup unit the spec also drives. Each step
	// performs the real work; Startup() fixes the order: create -> budgets -> assemble globals -> load
	// author JS -> begin ticking.
	// Build the ordered startup steps via the SHARED construction — the
	// globals reachability test drives the SAME builder, so its globals proof runs the PRODUCTION install, not a
	// fork. Step 3 (AssembleGlobals) SETS the console/timers install seams + assembles on the live context:
	// the production wiring that makes console / setTimeout / requestAnimationFrame reachable through the real
	// runtime, routing into this worker's DiagnosticSink + Scheduler against its ONE Clock.
	FContractStartupDeps Deps;
	Deps.Runtime = &Runtime;
	Deps.RuntimeFactory = RuntimeFactory;
	Deps.InterruptBudget = &InterruptBudget;
	Deps.Clock = Clock.Get(); // the ONE worker clock — setTimeout's now-source AND the tick's FireDueTimers clock
	Deps.GlobalEnv = &GlobalEnv;
	Deps.GlobalsCtx = &GlobalsCtx;
	Deps.Sink = DiagnosticSink;
	Deps.Scheduler = &Scheduler;

	FRuntimeStartup Startup(BuildContractStartupSteps(MoveTemp(Deps)));
	Startup.Startup();

	if (!Runtime.IsValid() || !Runtime->IsValid())
	{
		UE_LOG(LogCathodeWorker, Error, TEXT("CathodeWorker: JS runtime failed to initialize on the worker thread."));
		return 1;
	}

	// The ONE tick-iteration unit — the same FCathodeTick the specs drive directly. Run() supplies the
	// real seams (including the diagnostic sink for error routing); the loop only sequences ticks and
	// idles between them.
	FCathodeTick Tick(*Runtime, *Clock, Affinity, Inbound, Outbound, Scheduler, RelayoutSeam, DiagnosticSink);

	constexpr double FrameIntervalSeconds = 1.0 / 60.0;
	while (!bStopRequested.load())
	{
		InterruptBudget.BeginExecution(); // open a fresh execution window for this tick's JS
		Tick.Tick();

		// Event-driven wake: idle when nothing is dirty, wake on inbound/timer/raf — not a busy
		// fixed interval.
		double NextTimerDue = 0.0;
		const bool bHasTimer = Scheduler.TryGetNextTimerDue(NextTimerDue);
		const double WaitSeconds = ComputeTickWaitSeconds(
			Scheduler.HasPendingAnimationFrame(), bHasTimer, NextTimerDue, Clock->NowSeconds(), FrameIntervalSeconds);

		if (WaitSeconds < 0.0)
		{
			WakeEvent->Wait(); // block until an inbound message (or Stop) triggers the event
		}
		else
		{
			const uint32 WaitMs = static_cast<uint32>(FMath::CeilToInt(WaitSeconds * 1000.0));
			WakeEvent->Wait(FMath::Max(WaitMs, 1u));
		}
	}

	// Teardown on the worker thread. Release any pending scheduler callbacks FIRST (they may hold dup'd JS
	// callback refs, freed on the context) — BEFORE the runtime/context is torn down, so the held refs release
	// while the context is still alive (no use-after-free / no unfreed-value at JS_FreeRuntime).
	Scheduler.Reset();
	// The runtime's ordered teardown runs on Reset().
	Runtime.Reset();
	return 0;
}

void FCathodeRuntimeWorker::Stop()
{
	bStopRequested.store(true);
	if (WakeEvent != nullptr)
	{
		WakeEvent->Trigger(); // wake an idle worker so it observes the stop request
	}
}

void FCathodeRuntimeWorker::Exit()
{
}

} // namespace Cathode
