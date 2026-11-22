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

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Real ExecutePendingJob (the confirmed tri-state) + the INTEGRATED drain, against the REAL engine
// (FScopedTestRuntime). The PRODUCTION DrainMicrotasks loop (FCathodeTick::Tick) drives the REAL one-job
// primitive. Throw-on-success via Contains (a stub's ExecutePendingJob is NoPending-always -> the drain never runs ->
// the check's bThrew==false -> the assert FAILS). The drain "drained:ab" + the faulted "42" are RED on a stubbed runtime.
//
// THE FAULTED PROOF: a plain author-JS `Promise.then(throw)` is CAUGHT by the reaction -> Executed(1)
// + an unhandled rejection (host-tracker channel), NOT Faulted(-1). The REACHABLE <0 path is a HOST-enqueued raw job
// whose JSJobFunc returns JS_EXCEPTION (JS_EnqueueJob) -> JS_ExecutePendingJob returns -1 -> Faulted -> the tick's
// Faulted->Sink routing surfaces the message. This proves the <0->Faulted MAPPING
// with NO test-only production seam. (The author-JS unhandled-rejection->sink channel is covered by CathodeRuntimeRejectionTests.)

namespace
{
using namespace Cathode;

class FFixedClock final : public ICathodeClock
{
public:
	virtual double NowSeconds() const override { return 0.0; }
};

// Records faults the tick routes here (DrainMicrotasks: a Faulted job -> TakePendingError -> Sink->ReportError).
class FRecordingSink final : public ICathodeDiagnosticSink
{
public:
	virtual void ReportError(const FJsError& Error) override { Errors.Add(Error); }
	TArray<FJsError> Errors;
};

// A HOST raw job whose function RETURNS JS_EXCEPTION -> the reachable <0 (Faulted) path. Sets the exception to an Error
// carrying the computed 6*7 => "42" (author-JS reactions CATCH a handler-throw -> Executed; only a host job-function
// exception yields -1). JS_Throw steals the Error ref + returns the JS_EXCEPTION sentinel.
JSValue ThrowingHostJob(JSContext* Ctx, int /*argc*/, JSValueConst* /*argv*/)
{
	JSValue Err = JS_NewError(Ctx);
	JS_SetPropertyStr(Ctx, Err, "message", JS_NewString(Ctx, TCHAR_TO_UTF8(*FString::FromInt(6 * 7))));
	return JS_Throw(Ctx, Err);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeDrainTest,
	"Cathode.RuntimeCore.Drain.RealJobPump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeDrainTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FScopedTestRuntime Runtime;
	if (!Runtime.IsValid() || !Runtime->IsValid()) { AddError(TEXT("runtime failed to start")); return false; }

	JSContext* Ctx = static_cast<JSContext*>(Runtime->GetNativeContextHandle());
	if (Ctx == nullptr) { AddError(TEXT("runtime exposes no native context")); return false; }
	JSRuntime* Rt = JS_GetRuntime(Ctx);

	// The tick's other inputs (empty — only the microtask-drain phase is exercised).
	FFixedClock Clock;
	Cathode::FThreadAffinity Affinity; // qualified: disambiguates from the engine's ::FThreadAffinity
	Affinity.BindToCurrentThread();    // so the real tick proceeds on this (test) thread
	TCathodeMessageQueue<FHostInboundMessage> Inbound;
	TCathodeMessageQueue<FHostOutboundMessage> Outbound;
	FCathodeScheduler Scheduler;
	FSurfaceRelayoutSeam NoRelayout;
	FRecordingSink Sink;

	auto Eval = [&](const TCHAR* Source) -> FEvalResult
	{
		return Runtime->Evaluate(Source, TEXT("<drain-test>"), EEvalKind::Global);
	};
	// Drive the PRODUCTION drain (FCathodeTick::Tick -> DrainMicrotasks, UNCHANGED) over the REAL ExecutePendingJob.
	auto Drain = [&]()
	{
		FCathodeTick Tick(Runtime.Get(), Clock, Affinity, Inbound, Outbound, Scheduler, NoRelayout, &Sink);
		Tick.Tick();
	};

	// (EMPTY) ExecutePendingJob on an empty queue -> NoPending (a NoPending<->Executed swap would infinite-loop the
	// drain -> caught by the DRAIN below terminating). The fresh runtime's startup is synchronous (no pending jobs).
	TestTrue(TEXT("(EMPTY) ExecutePendingJob on an empty queue -> NoPending"),
		Runtime->ExecutePendingJob() == EJobPumpResult::NoPending);

	// (NORMAL/DRAIN) a RE-QUEUING Promise chain: the 2nd .then is CHAINED off the first, so 'b' is enqueued ONLY when
	// 'a' runs -> the loop must RE-CHECK pending after each job. The production drain runs the REAL ExecutePendingJob to
	// empty; the check throws IFF the log drained in order+complete -> bThrew==true + Contains("drained:ab"). A stub
	// (NoPending-always) drains nothing -> __log empty -> bThrew==false -> FAILS (bites ran+ordered+complete + re-queuing).
	{
		Eval(TEXT("globalThis.__log = []; Promise.resolve().then(() => globalThis.__log.push('a')).then(() => globalThis.__log.push('b'));"));
		Drain();

		// drain-to-empty proven BOTH ways: complete log (not a cap-terminated-short) AND the queue is truly empty.
		TestFalse(TEXT("(DRAIN) drain-to-empty: JS_IsJobPending==false after the drain (queue truly empty, test-side; NOT wired into the loop)"),
			JS_IsJobPending(Rt));
		const FEvalResult Check = Eval(TEXT("if (globalThis.__log.join('') === 'ab') throw new Error('drained:' + globalThis.__log.join(''))"));
		TestTrue(TEXT("(DRAIN) the re-queuing chain ran IN ORDER to completion -> bThrew==true"), Check.bThrew);
		TestTrue(TEXT("(DRAIN) the ordered log -> message carries 'drained:ab'"), Check.Error.Message.Contains(TEXT("drained:ab")));
	}

	// (FAULTED) the reachable <0 path: a HOST-enqueued raw job whose JSJobFunc returns JS_EXCEPTION -> ExecutePendingJob
	// returns -1 -> Faulted -> the tick routes it to the Sink. Assert the Sink received the fault with Contains("42") --
	// DISTINCT from NoPending (a Faulted<->NoPending swap drops the fault -> "42" never surfaces -> bites the swap AND the stub).
	{
		Sink.Errors.Empty();
		JS_EnqueueJob(Ctx, &ThrowingHostJob, 0, nullptr);
		Drain();

		TestEqual(TEXT("(FAULTED) the tick routed exactly one fault to the Sink (the <0 tri-state fired)"), Sink.Errors.Num(), 1);
		if (Sink.Errors.Num() == 1)
		{
			TestTrue(TEXT("(FAULTED) the routed fault carries the computed '42' (not a dropped NoPending)"),
				Sink.Errors[0].Message.Contains(TEXT("42")));
		}
	}

	// (LEAK — empirical bounded-growth guard) N cycles of eval+drain INCL the faulted host-job must NOT grow memory
	// UNBOUNDED (bounded, NOT byte-exact — quickjs caches/GC make malloc_size non-byte-stable). A leak in ExecutePendingJob
	// / TakePendingError / the eval-result frees would grow malloc_size ~linearly with N.
	auto MallocSizeAfterGC = [&]() -> uint64
	{
		JS_RunGC(Rt);
		JSMemoryUsage Usage;
		JS_ComputeMemoryUsage(Rt, &Usage);
		return static_cast<uint64>(Usage.malloc_size);
	};
	auto OneCycle = [&]()
	{
		Eval(TEXT("Promise.resolve().then(() => {}).then(() => {});")); // a small re-queuing chain
		Drain();
		JS_EnqueueJob(Ctx, &ThrowingHostJob, 0, nullptr);               // a faulted host-job (exercises the <0 + TakePendingError path)
		Drain();
	};
	for (int32 i = 0; i < 5; ++i) { OneCycle(); }      // warm up (caches stabilize)
	const uint64 Baseline = MallocSizeAfterGC();
	for (int32 i = 0; i < 50; ++i) { OneCycle(); }     // 10x the warm-up
	const uint64 After = MallocSizeAfterGC();
	// generous bound (caches/GC slack) yet tight enough that 50 leaked errors would blow it: <= baseline + 128 KiB.
	TestTrue(FString::Printf(TEXT("(LEAK) memory BOUNDED after 50 eval+drain+faulted cycles (baseline=%llu after=%llu; no unbounded leak)"), Baseline, After),
		After <= Baseline + (128 * 1024));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
