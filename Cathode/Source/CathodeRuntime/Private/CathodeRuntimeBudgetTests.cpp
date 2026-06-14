#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeClock.h"
#include "CathodeDiagnostics.h"
#include "CathodeInterruptBudget.h"
#include "CathodeJsRuntime.h"
#include "CathodeMessageQueue.h"
#include "CathodeScheduler.h"
#include "CathodeThreadAffinity.h"
#include "CathodeTick.h"

#include "Misc/AutomationTest.h"

// Specs for Runtime Core's BESPOKE safety logic: the interrupt-budget ABORT POLICY and the error-
// routing path. These are STABILITY / parent-isolation guards for trusted code (no attack surface) —
// the tests assert the abort DECISION and the routing OUTCOME, not vendor enforcement (the engine
// actually interrupting / enforcing memory/stack is the execution sprint's behavioural proof).
// Authored, not run this sprint.

namespace
{
using namespace Cathode;

// Controllable clock — the budget reads time from here.
class FFakeClock final : public ICathodeClock
{
public:
	double Now = 0.0;
	virtual double NowSeconds() const override { return Now; }
};

// A runtime that scripts a faulting job and a known pending error — to drive the tick's error ROUTING.
class FFaultingRuntime final : public ICathodeJsRuntime
{
public:
	TArray<EJobPumpResult> ScriptedJobResults;
	FJsError ScriptedError;
	bool bHasPendingError = false;

	virtual bool IsValid() const override { return true; }
	virtual FEvalResult Evaluate(const FString&, const FString&, EEvalKind) override { return FEvalResult{}; }

	virtual EJobPumpResult ExecutePendingJob() override
	{
		if (ScriptedJobResults.Num() > 0)
		{
			const EJobPumpResult Result = ScriptedJobResults[0];
			ScriptedJobResults.RemoveAt(0);
			return Result;
		}
		return EJobPumpResult::NoPending;
	}

	virtual bool TakePendingError(FJsError& OutError) override
	{
		if (bHasPendingError)
		{
			OutError = ScriptedError;
			bHasPendingError = false;
			return true;
		}
		return false;
	}

	virtual void SetInterruptHandler(FInterruptHandler) override {}
	virtual void SetMemoryLimit(SIZE_T) override {}
	virtual void SetMaxStackSize(SIZE_T) override {}
};

// Records every error routed to it — the injected diagnostic sink.
class FRecordingSink final : public ICathodeDiagnosticSink
{
public:
	TArray<FJsError> Reported;
	virtual void ReportError(const FJsError& Error) override { Reported.Add(Error); }
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeInterruptBudgetTest,
	"Cathode.RuntimeCore.Safety.InterruptBudgetPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeInterruptBudgetTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FFakeClock Clock;
	Clock.Now = 0.0;
	FInterruptBudget Budget(Clock, /*TimeBudgetSeconds=*/1.0);

	// No execution window open yet -> never abort.
	TestFalse(TEXT("no active window -> do not abort"), Budget.ShouldInterrupt());

	// Open the window at t=0; the abort decision is elapsed > budget, measured on the injected clock.
	Budget.BeginExecution();

	Clock.Now = 0.5;
	TestFalse(TEXT("under budget -> continue (return 0)"), Budget.ShouldInterrupt());

	Clock.Now = 1.0;
	TestFalse(TEXT("exactly at budget -> continue"), Budget.ShouldInterrupt());

	Clock.Now = 1.5;
	TestTrue(TEXT("over budget -> abort (return nonzero)"), Budget.ShouldInterrupt());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeErrorRoutingTest,
	"Cathode.RuntimeCore.Safety.ErrorRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeErrorRoutingTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FFakeClock Clock;
	Cathode::FThreadAffinity Affinity; // qualified: disambiguates from the engine's ::FThreadAffinity (HAL/PlatformAffinity.h, transitively visible in this test TU)
	Affinity.BindToCurrentThread();
	TCathodeMessageQueue<FHostInboundMessage> Inbound;
	TCathodeMessageQueue<FHostOutboundMessage> Outbound;
	FCathodeScheduler Scheduler;
	FSurfaceRelayoutSeam NoRelayout;

	// A job faults, then the queue is empty; the runtime hands back a known pending error.
	FFaultingRuntime Runtime;
	Runtime.ScriptedJobResults = { EJobPumpResult::Faulted, EJobPumpResult::NoPending };
	Runtime.bHasPendingError = true;
	Runtime.ScriptedError.Message = TEXT("scripted screen error");

	FRecordingSink Sink;

	// Drive the REAL tick (same FCathodeTick the worker runs), injecting the faulting runtime + the
	// recording sink.
	FCathodeTick Tick(Runtime, Clock, Affinity, Inbound, Outbound, Scheduler, NoRelayout, &Sink);
	Tick.Tick();

	// The uncaught fault routed to the injected sink, carrying its info.
	TestEqual(TEXT("one uncaught error routed to the sink"), Sink.Reported.Num(), 1);
	if (Sink.Reported.Num() == 1)
	{
		TestEqual(TEXT("the routed error carries its message"), Sink.Reported[0].Message, FString(TEXT("scripted screen error")));
	}

	// Reaching here at all is the containment assertion: routing the fault did NOT propagate a crash to
	// the parent — the tick completed normally.
	TestTrue(TEXT("fault contained: the tick completed without propagating a crash"), true);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
