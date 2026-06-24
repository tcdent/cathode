#include "CathodeTick.h"

#include "CathodeClock.h"
#include "CathodeDiagnostics.h"
#include "CathodeScheduler.h"
#include "CathodeThreadAffinity.h"

// JS_* never appears here — the tick touches the engine only through ICathodeJsRuntime::ExecutePendingJob.
DEFINE_LOG_CATEGORY_STATIC(LogCathodeTick, Log, All);

namespace Cathode
{

FCathodeTick::FCathodeTick(
	ICathodeJsRuntime& InRuntime,
	ICathodeClock& InClock,
	FThreadAffinity& InAffinity,
	TCathodeMessageQueue<FHostInboundMessage>& InInbound,
	TCathodeMessageQueue<FHostOutboundMessage>& InOutbound,
	FCathodeScheduler& InScheduler,
	const FSurfaceRelayoutSeam& InRelayout,
	ICathodeDiagnosticSink* InSink)
	: Runtime(InRuntime)
	, Clock(InClock)
	, Affinity(InAffinity)
	, Inbound(InInbound)
	, Outbound(InOutbound)
	, Scheduler(InScheduler)
	, Relayout(InRelayout)
	, Sink(InSink)
{
}

void FCathodeTick::Tick()
{
	// Stability guard: the tick must run on the worker thread (the JS heap is single-thread). Off-thread
	// -> log + skip, never crash. Run() always calls this on the worker; tests bind affinity to their own
	// thread so the real orchestration proceeds.
	if (!Affinity.IsOnBoundThread())
	{
		UE_LOG(LogCathodeTick, Error, TEXT("Tick invoked off the worker thread; skipping (stability guard)."));
		return;
	}

	// Phase 1 — drain inbound host events/state (which may run JS handlers).
	DrainInbound();

	const double Now = Clock.NowSeconds();

	// Phase 2 — fire timers whose deadline has passed.
	Scheduler.FireDueTimers(Now);

	// Phase 3 — run requestAnimationFrame callbacks (the "about to produce a frame" hook).
	Scheduler.RunAnimationFrameCallbacks(Now);

	// Phase 4 — drain microtasks: pump the job queue to empty so Promise continuations settle.
	DrainMicrotasks();

	// Phase 5 — let dirtied surfaces relayout/raster (delegated) and emit frame-ready outbound.
	RelayoutAndEmitFrames(Now);
}

void FCathodeTick::DrainInbound()
{
	TArray<FHostInboundMessage> Batch;
	Inbound.Drain(Batch);
	for (FHostInboundMessage& Message : Batch)
	{
		if (Message.Apply)
		{
			Message.Apply();
		}
	}
}

void FCathodeTick::DrainMicrotasks()
{
	// OUR loop; the one-job primitive (ExecutePendingJob) is vendor. Drain to empty so every Promise
	// continuation settles within the tick — N executed jobs cost N+1 calls (the final call reports
	// NoPending and ends the loop). A faulted job is still consumed and draining continues; the fault is
	// ROUTED to the diagnostic sink (contained to this surface, never propagated to the parent).
	for (;;)
	{
		const EJobPumpResult Result = Runtime.ExecutePendingJob();
		if (Result == EJobPumpResult::NoPending)
		{
			break;
		}
		if (Result == EJobPumpResult::Faulted)
		{
			FJsError Error;
			if (Runtime.TakePendingError(Error) && Sink != nullptr)
			{
				Sink->ReportError(Error);
			}
		}
	}

	// 029 #18 — the drain is empty: the SETTLED boundary. Report the genuinely-unhandled author-JS rejections (the
	// tracker's deferred survivors — is_handled=false, never-later-handled) to the diagnostic sink + clear. A later-caught
	// rejection was already removed by the tracker's is_handled=true edge before now, so it does NOT surface here.
	Runtime.ReportUnhandledRejections();
}

void FCathodeTick::RelayoutAndEmitFrames(double NowSeconds)
{
	(void)NowSeconds;

	// Delegated: the web-engine / Surfaces components do the actual relayout/raster. Runtime Core only
	// sequences the call and emits whatever frame-ready notifications come back. Unset until those
	// components are wired (Slice/sprint of their own) -> nothing to emit.
	if (!Relayout)
	{
		return;
	}

	TArray<FHostOutboundMessage> FrameReady;
	Relayout(Runtime, FrameReady); // 028 S3 — pass the worker's runtime so the seam can build a real FDomDocument on the worker
	for (FHostOutboundMessage& Message : FrameReady)
	{
		Outbound.Enqueue(MoveTemp(Message)); // Runtime Core emits frame-ready outbound.
	}
}

double ComputeTickWaitSeconds(
	bool bHasPendingRaf,
	bool bHasTimer,
	double NextTimerDue,
	double NowSeconds,
	double FrameIntervalSeconds)
{
	if (bHasPendingRaf)
	{
		return FrameIntervalSeconds; // animating -> continuous frame cadence
	}
	if (bHasTimer)
	{
		return FMath::Max(0.0, NextTimerDue - NowSeconds); // sleep until the next timer is due
	}
	return -1.0; // nothing pending -> block until an inbound message wakes the worker
}

} // namespace Cathode
