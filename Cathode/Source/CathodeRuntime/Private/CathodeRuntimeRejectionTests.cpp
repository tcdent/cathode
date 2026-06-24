#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeClock.h"
#include "CathodeDiagnostics.h"
#include "CathodeJsRuntime.h"
#include "CathodeMessageQueue.h"
#include "CathodeRuntimeTesting.h"
#include "CathodeScheduler.h"
#include "CathodeThreadAffinity.h"
#include "CathodeTick.h"

#include "Misc/AutomationTest.h"

// 029 #18 — author-JS unhandled-rejection -> host Sink: the discriminating pair. The host promise-rejection tracker
// (JS_SetHostPromiseRejectionTracker, fire-decision on the is_handled BOOL — NOT JS_ExecutePendingJob's tri-state return,
// the wrong channel: a throwing-but-CAUGHT .then returns 1/Executed) routes a GENUINELY-unhandled rejection to the
// diagnostic sink, DEFERRED to the tick's drain-to-empty (the settled boundary). The pair: a genuinely-unhandled rejection
// REACHES a recording sink; a SYNC-caught AND a LATER-caught do NOT. The unhandled half bites a no-tracker; the caught
// half bites a fire-on-any-rejection naive impl AND a read-ExecutePendingJob-return impl; the LATER-caught is the sharp
// bite — the tracker's is_handled=true double-fire must REMOVE it from the deferred set before the drain-report.

namespace
{
using namespace Cathode;

class FFixedClock final : public ICathodeClock
{
public:
	virtual double NowSeconds() const override { return 0.0; }
};

class FRecordingSink final : public ICathodeDiagnosticSink
{
public:
	virtual void ReportError(const FJsError& Error) override { Errors.Add(Error); }
	TArray<FJsError> Errors;
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeUnhandledRejectionTest,
	"Cathode.RuntimeCore.Rejection.UnhandledReachesSink",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeUnhandledRejectionTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FScopedTestRuntime Runtime;
	if (!Runtime.IsValid() || !Runtime->IsValid()) { AddError(TEXT("runtime failed to start")); return false; }

	FFixedClock Clock;
	Cathode::FThreadAffinity Affinity; // qualified: disambiguates from the engine's ::FThreadAffinity
	Affinity.BindToCurrentThread();    // the real tick proceeds on this (test) thread
	TCathodeMessageQueue<FHostInboundMessage> Inbound;
	TCathodeMessageQueue<FHostOutboundMessage> Outbound;
	FCathodeScheduler Scheduler;
	FSurfaceRelayoutSeam NoRelayout;
	FRecordingSink Sink;

	// Install the tracker with the recording sink — the production effect of the OnInstallRejectionTracker seam
	// (CathodeContractGlobals wires it; here the test installs it directly with the recording sink).
	Runtime->InstallRejectionTracker(Sink);

	// Three author-JS rejections: (1) genuinely-unhandled, (2) sync-caught (a .catch attached in the same tick),
	// (3) later-caught (the .catch added in a LATER microtask -> the tracker's is_handled=false-then-true double-fire).
	// Only (1) is genuinely unhandled at the settled boundary.
	const FEvalResult R = Runtime->Evaluate(
		TEXT("Promise.reject(new Error('unhandled-boom'));\n")
		TEXT("Promise.reject(new Error('sync-caught')).catch(function(){});\n")
		TEXT("globalThis.__lp = Promise.reject(new Error('later-caught'));\n")
		TEXT("Promise.resolve().then(function(){ globalThis.__lp.catch(function(){}); });\n"),
		TEXT("<rejection-test>"), EEvalKind::Global);
	TestFalse(TEXT("[029-#18] the setup script itself does not throw"), R.bThrew);

	// Drive the PRODUCTION drain (FCathodeTick::Tick -> DrainMicrotasks, which now calls ReportUnhandledRejections at the
	// settled boundary). The drain settles every continuation (the later .catch RUNS) BEFORE the report -> the later-caught
	// is removed (is_handled=true) before its rejection could surface.
	FCathodeTick Tick(Runtime.Get(), Clock, Affinity, Inbound, Outbound, Scheduler, NoRelayout, &Sink);
	Tick.Tick();

	// THE DISCRIMINATING PAIR.
	bool bHasUnhandled = false, bHasSyncCaught = false, bHasLaterCaught = false;
	for (const FJsError& E : Sink.Errors)
	{
		if (E.Message.Contains(TEXT("unhandled-boom"))) { bHasUnhandled = true; }
		if (E.Message.Contains(TEXT("sync-caught")))    { bHasSyncCaught = true; }
		if (E.Message.Contains(TEXT("later-caught")))   { bHasLaterCaught = true; }
	}
	AddInfo(FString::Printf(TEXT("[029-#18] sink recorded %d error(s); unhandled=%d sync-caught=%d later-caught=%d"),
		Sink.Errors.Num(), bHasUnhandled ? 1 : 0, bHasSyncCaught ? 1 : 0, bHasLaterCaught ? 1 : 0));

	TestTrue(TEXT("[029-#18] a GENUINELY-unhandled rejection REACHES the sink (bites a no-tracker)"), bHasUnhandled);
	TestFalse(TEXT("[029-#18] a SYNC-caught rejection does NOT reach the sink (bites a fire-on-any-rejection / read-ExecutePendingJob-return impl)"), bHasSyncCaught);
	TestFalse(TEXT("[029-#18] a LATER-caught rejection does NOT reach the sink (the sharp bite: the is_handled=true double-fire removed it before the drain-report)"), bHasLaterCaught);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
