#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeMessageQueue.h"
#include "CathodeThreadAffinity.h"

#include "HAL/PlatformTLS.h"
#include "Misc/AutomationTest.h"

// Specs for Runtime Core's BESPOKE threading logic: the message-boundary FIFO drain and the
// single-thread affinity guard. They drive OUR queue/affinity code and assert observable behaviour;
// they do NOT test UE's threading primitives (FRunnableThread/TQueue/FPlatformTLS) themselves. Expecteds
// are design invariants (FIFO order; on/off-thread outcome) asserted directly.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeQueueOrderingTest,
	"Cathode.RuntimeCore.Threading.QueueOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeQueueOrderingTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// Enqueue inbound messages tagged 0..N-1; each records its tag when applied. Draining must apply
	// them in FIFO order — the contract the tick depends on.
	TCathodeMessageQueue<FHostInboundMessage> Queue;
	TArray<int32> Applied;
	const int32 N = 5;
	for (int32 i = 0; i < N; ++i)
	{
		FHostInboundMessage Msg;
		Msg.Apply = [&Applied, i]() { Applied.Add(i); };
		Queue.Enqueue(MoveTemp(Msg));
	}

	TArray<FHostInboundMessage> Drained;
	const int32 Count = Queue.Drain(Drained);
	for (FHostInboundMessage& Msg : Drained)
	{
		if (Msg.Apply)
		{
			Msg.Apply();
		}
	}

	TestEqual(TEXT("drained the number enqueued"), Count, N);
	TestEqual(TEXT("applied count matches enqueued"), Applied.Num(), N);

	bool bInOrder = (Applied.Num() == N);
	for (int32 i = 0; i < Applied.Num(); ++i)
	{
		if (Applied[i] != i)
		{
			bInOrder = false;
		}
	}
	TestTrue(TEXT("messages applied in FIFO enqueue order"), bInOrder);
	TestTrue(TEXT("queue is empty after drain"), Queue.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeAffinityTest,
	"Cathode.RuntimeCore.Threading.SingleThreadAffinity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeAffinityTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FThreadAffinity Affinity; // qualified: disambiguates from the engine's ::FThreadAffinity (HAL/PlatformAffinity.h, transitively visible in this test TU)
	TestFalse(TEXT("an unbound affinity reports off-thread"), Affinity.IsOnBoundThread());

	// Bound to THIS thread -> on-thread.
	Affinity.BindToCurrentThread();
	TestTrue(TEXT("bound to the current thread reports on-thread"), Affinity.IsOnBoundThread());

	// Bound to a DIFFERENT thread id -> the guard catches the wrong-thread case (returns false), no crash.
	const uint32 OtherThreadId = FPlatformTLS::GetCurrentThreadId() + 1;
	Affinity.BindToThreadId(OtherThreadId);
	TestFalse(TEXT("bound to another thread reports off-thread (violation caught, not crashed)"), Affinity.IsOnBoundThread());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
