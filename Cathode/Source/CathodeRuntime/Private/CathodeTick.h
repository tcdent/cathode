#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

#include "CathodeJsRuntime.h"
#include "CathodeMessageQueue.h"

namespace Cathode
{

class ICathodeClock;
class ICathodeDiagnosticSink;
class FCathodeScheduler;
class FThreadAffinity;

// Phase 5 delegation seam: the web-engine / Surfaces components relayout + raster dirtied surfaces and
// report which produced a frame; Runtime Core only SEQUENCES the call and EMITS frame-ready. The
// callback appends one outbound notification per surface that produced a frame. Default unset (no
// surfaces wired yet); the real implementation belongs to those components' sprints.
using FSurfaceRelayoutSeam = TFunction<void(TArray<FHostOutboundMessage>& /*OutFrameReady*/)>;

// The ordered tick — the heart of Runtime Core. There is ONE definition of the phase sequence, here.
// The worker's Run() loop calls Tick() each iteration; the tick specs call the SAME Tick() directly
// with injected seams (fake clock, recording runtime, drivable queues, recording timer/rAF callbacks,
// a recording relayout seam). Tests isolate only the INPUTS — they never re-implement the phases, so
// there is no forked tick path.
class FCathodeTick
{
public:
	FCathodeTick(
		ICathodeJsRuntime& InRuntime,
		ICathodeClock& InClock,
		FThreadAffinity& InAffinity,
		TCathodeMessageQueue<FHostInboundMessage>& InInbound,
		TCathodeMessageQueue<FHostOutboundMessage>& InOutbound,
		FCathodeScheduler& InScheduler,
		const FSurfaceRelayoutSeam& InRelayout,
		ICathodeDiagnosticSink* InSink = nullptr);

	/** Run one ordered tick:
	 *  1 drain inbound -> 2 fire due timers -> 3 run requestAnimationFrame -> 4 drain microtasks to
	 *  empty -> 5 relayout/raster (delegated) + emit frame-ready. */
	void Tick();

private:
	void DrainInbound();
	void DrainMicrotasks();
	void RelayoutAndEmitFrames(double NowSeconds);

	ICathodeJsRuntime& Runtime;
	ICathodeClock& Clock;
	FThreadAffinity& Affinity;
	TCathodeMessageQueue<FHostInboundMessage>& Inbound;
	TCathodeMessageQueue<FHostOutboundMessage>& Outbound;
	FCathodeScheduler& Scheduler;
	const FSurfaceRelayoutSeam& Relayout;
	ICathodeDiagnosticSink* Sink; // error-routing target; nullable (no sink wired -> faults not routed)
};

// Idle policy (RQ3, event-driven wake): how long the worker should wait before the next tick.
//  - animating (pending rAF) -> the frame interval (continuous cadence)
//  - else a timer pending     -> until the earliest timer is due (clamped >= 0)
//  - else                     -> negative, meaning "block until an inbound message wakes us"
double ComputeTickWaitSeconds(
	bool bHasPendingRaf,
	bool bHasTimer,
	double NextTimerDue,
	double NowSeconds,
	double FrameIntervalSeconds);

} // namespace Cathode
