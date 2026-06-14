#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeClock.h"
#include "CathodeJsRuntime.h"
#include "CathodeMessageQueue.h"
#include "CathodeScheduler.h"
#include "CathodeThreadAffinity.h"
#include "CathodeTick.h"

#include "Misc/AutomationTest.h"

// Specs for Runtime Core's highest-value bespoke logic: the ordered TICK. They drive the EXACT
// production FCathodeTick::Tick() (the same unit the worker's Run() loop calls) with only the INPUTS
// isolated — a fake clock, a recording runtime, drivable queues, recording timer/rAF callbacks, and a
// recording relayout seam. There is no forked tick. Phase order is proven by a SINGLE shared ordered
// effect-log fed through those seams, never by a bool or code inspection. Authored, not run this sprint.

namespace
{
using namespace Cathode;

// A controllable clock — the worker timeline is whatever the test sets.
class FFakeClock final : public ICathodeClock
{
public:
	double Now = 0.0;
	virtual double NowSeconds() const override { return Now; }
};

// A recording runtime: ExecutePendingJob returns a SCRIPTED sequence (input), and each call runs a hook
// so the tick's microtask phase leaves an observable mark. Everything else is an inert stub — we test
// OUR drain loop, never vendor job execution.
class FRecordingRuntime final : public ICathodeJsRuntime
{
public:
	TArray<EJobPumpResult> ScriptedJobResults; // consumed front-to-back; NoPending once exhausted
	int32 JobCallCount = 0;
	TFunction<void()> OnExecutePendingJob;

	virtual bool IsValid() const override { return true; }
	virtual FEvalResult Evaluate(const FString&, const FString&, EEvalKind) override { return FEvalResult{}; }

	virtual EJobPumpResult ExecutePendingJob() override
	{
		++JobCallCount;
		if (OnExecutePendingJob)
		{
			OnExecutePendingJob();
		}
		if (ScriptedJobResults.Num() > 0)
		{
			const EJobPumpResult Result = ScriptedJobResults[0];
			ScriptedJobResults.RemoveAt(0);
			return Result;
		}
		return EJobPumpResult::NoPending;
	}

	virtual bool TakePendingError(FJsError&) override { return false; }
	virtual void SetInterruptHandler(FInterruptHandler) override {}
	virtual void SetMemoryLimit(SIZE_T) override {}
	virtual void SetMaxStackSize(SIZE_T) override {}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeTickPhaseOrderTest,
	"Cathode.RuntimeCore.Tick.PhaseOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeTickPhaseOrderTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// One shared ordered effect-log; each phase appends its marker through an injected seam.
	TArray<FString> Order;

	FFakeClock Clock;
	Clock.Now = 100.0;

	Cathode::FThreadAffinity Affinity; // qualified: disambiguates from the engine's ::FThreadAffinity (HAL/PlatformAffinity.h, transitively visible in this test TU)
	Affinity.BindToCurrentThread(); // so the real tick proceeds on this (test) thread

	TCathodeMessageQueue<FHostInboundMessage> Inbound;
	TCathodeMessageQueue<FHostOutboundMessage> Outbound;
	FCathodeScheduler Scheduler;

	FRecordingRuntime Runtime;
	Runtime.OnExecutePendingJob = [&Order]() { Order.Add(TEXT("microtask")); }; // scripted empty -> 1 call

	// Phase 1 input: an inbound message.
	FHostInboundMessage InMsg;
	InMsg.Apply = [&Order]() { Order.Add(TEXT("inbound")); };
	Inbound.Enqueue(MoveTemp(InMsg));

	// Phase 2 input: a timer already due (50 <= now 100).
	Scheduler.SetTimer(50.0, [&Order]() { Order.Add(TEXT("timer")); });

	// Phase 3 input: a requestAnimationFrame callback.
	Scheduler.RequestAnimationFrame([&Order](double) { Order.Add(TEXT("raf")); });

	// Phase 5 input: the relayout seam records + emits one frame-ready.
	FSurfaceRelayoutSeam Relayout = [&Order](TArray<FHostOutboundMessage>& OutFrameReady)
	{
		Order.Add(TEXT("surface"));
		OutFrameReady.Add(FHostOutboundMessage{ 7 });
	};

	FCathodeTick Tick(Runtime, Clock, Affinity, Inbound, Outbound, Scheduler, Relayout);
	Tick.Tick();

	const TArray<FString> Expected = {
		TEXT("inbound"), TEXT("timer"), TEXT("raf"), TEXT("microtask"), TEXT("surface")
	};
	TestEqual(TEXT("each phase ran exactly once, in order"), Order.Num(), Expected.Num());
	if (Order.Num() == Expected.Num())
	{
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("phase %d marker"), Index), Order[Index], Expected[Index]);
		}
	}

	// Phase 5 emitted frame-ready outbound.
	TArray<FHostOutboundMessage> Frames;
	const int32 FrameCount = Outbound.Drain(Frames);
	TestEqual(TEXT("one frame-ready emitted"), FrameCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeTickMicrotaskDrainTest,
	"Cathode.RuntimeCore.Tick.MicrotaskDrainToEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeTickMicrotaskDrainTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FFakeClock Clock;
	Cathode::FThreadAffinity Affinity; // qualified: disambiguates from the engine's ::FThreadAffinity (HAL/PlatformAffinity.h, transitively visible in this test TU)
	Affinity.BindToCurrentThread();
	TCathodeMessageQueue<FHostInboundMessage> Inbound;
	TCathodeMessageQueue<FHostOutboundMessage> Outbound;
	FCathodeScheduler Scheduler;
	FSurfaceRelayoutSeam NoRelayout;

	// Two jobs available, then empty: the drain loop must call ExecutePendingJob until NoPending -> three
	// calls (N executed + 1 terminal). The job results are scripted INPUTS; the assertion is on OUR loop.
	FRecordingRuntime Runtime;
	Runtime.ScriptedJobResults = { EJobPumpResult::Executed, EJobPumpResult::Executed, EJobPumpResult::NoPending };

	FCathodeTick Tick(Runtime, Clock, Affinity, Inbound, Outbound, Scheduler, NoRelayout);
	Tick.Tick();

	TestEqual(TEXT("ExecutePendingJob called until NoPending (2 executed + 1 terminal)"), Runtime.JobCallCount, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeTickTimerRafSelectionTest,
	"Cathode.RuntimeCore.Tick.TimerAndRafSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeTickTimerRafSelectionTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FFakeClock Clock;
	Clock.Now = 0.0;
	Cathode::FThreadAffinity Affinity; // qualified: disambiguates from the engine's ::FThreadAffinity (HAL/PlatformAffinity.h, transitively visible in this test TU)
	Affinity.BindToCurrentThread();
	TCathodeMessageQueue<FHostInboundMessage> Inbound;
	TCathodeMessageQueue<FHostOutboundMessage> Outbound;
	FCathodeScheduler Scheduler;
	FSurfaceRelayoutSeam NoRelayout;
	FRecordingRuntime Runtime; // scripted empty -> microtask drain is a no-op here

	TArray<FString> Fired;
	int32 RafCount = 0;
	Scheduler.SetTimer(10.0, [&Fired]() { Fired.Add(TEXT("t10")); });
	Scheduler.SetTimer(20.0, [&Fired]() { Fired.Add(TEXT("t20")); });
	Scheduler.RequestAnimationFrame([&Fired, &RafCount](double) { Fired.Add(TEXT("raf")); ++RafCount; });

	FCathodeTick Tick(Runtime, Clock, Affinity, Inbound, Outbound, Scheduler, NoRelayout);

	// At t=15: timer due at 10 fires; timer due at 20 does not; rAF fires (one-shot).
	Clock.Now = 15.0;
	Tick.Tick();
	TestTrue(TEXT("timer due at 10 fired (deadline <= now)"), Fired.Contains(TEXT("t10")));
	TestFalse(TEXT("timer due at 20 not yet fired (deadline > now)"), Fired.Contains(TEXT("t20")));
	TestTrue(TEXT("rAF fired"), Fired.Contains(TEXT("raf")));
	TestEqual(TEXT("rAF fired once"), RafCount, 1);

	// Advance to t=25: timer due at 20 now fires; the rAF was one-shot (not re-registered) so it does not.
	Clock.Now = 25.0;
	Tick.Tick();
	TestTrue(TEXT("timer due at 20 fired after the clock advanced"), Fired.Contains(TEXT("t20")));
	TestEqual(TEXT("rAF still fired only once (one-shot, not re-registered)"), RafCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeTickIdlePolicyTest,
	"Cathode.RuntimeCore.Tick.IdlePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeTickIdlePolicyTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	const double Frame = 1.0 / 60.0;

	// Animating (pending rAF) -> frame cadence.
	TestTrue(TEXT("pending rAF -> frame interval"),
		FMath::IsNearlyEqual(ComputeTickWaitSeconds(/*bHasPendingRaf=*/true, false, 0.0, 0.0, Frame), Frame));

	// A timer pending -> wait until it is due.
	TestTrue(TEXT("timer pending -> wait until due (10 - 4 = 6)"),
		FMath::IsNearlyEqual(ComputeTickWaitSeconds(false, /*bHasTimer=*/true, 10.0, 4.0, Frame), 6.0));

	// Nothing pending -> negative (block until woken).
	TestTrue(TEXT("idle -> block (negative wait)"),
		ComputeTickWaitSeconds(false, false, 0.0, 0.0, Frame) < 0.0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
