#include "CathodeScheduler.h"

namespace Cathode
{

int32 FCathodeScheduler::SetTimer(double DueSeconds, FTimerCallback Callback)
{
	const int32 Handle = NextHandle++;
	Timers.Add(FScheduledTimer{ Handle, DueSeconds, MoveTemp(Callback) });
	return Handle;
}

void FCathodeScheduler::ClearTimer(int32 Handle)
{
	Timers.RemoveAll([Handle](const FScheduledTimer& Timer) { return Timer.Handle == Handle; });
}

int32 FCathodeScheduler::RequestAnimationFrame(FAnimationFrameCallback Callback)
{
	const int32 Handle = NextHandle++;
	AnimationFrameCallbacks.Add(FScheduledAnimationFrame{ Handle, MoveTemp(Callback) });
	return Handle;
}

void FCathodeScheduler::CancelAnimationFrame(int32 Handle)
{
	AnimationFrameCallbacks.RemoveAll([Handle](const FScheduledAnimationFrame& Raf) { return Raf.Handle == Handle; });
}

void FCathodeScheduler::FireDueTimers(double NowSeconds)
{
	// Snapshot the due timers (deadline passed), remove them from the pending set, THEN fire them
	// earliest-first. Removing before firing means a callback that re-arms a timer schedules for a later
	// pass, never re-entering this one.
	TArray<FScheduledTimer> Due;
	for (int32 Index = Timers.Num() - 1; Index >= 0; --Index)
	{
		if (Timers[Index].Due <= NowSeconds)
		{
			Due.Add(MoveTemp(Timers[Index]));
			Timers.RemoveAt(Index);
		}
	}

	Due.Sort([](const FScheduledTimer& A, const FScheduledTimer& B) { return A.Due < B.Due; });

	for (FScheduledTimer& Timer : Due)
	{
		if (Timer.Callback)
		{
			Timer.Callback();
		}
	}
}

void FCathodeScheduler::RunAnimationFrameCallbacks(double NowSeconds)
{
	// One-shot: take the current callbacks, clear the pending list, then run them. Any rAF registered
	// during a callback lands in the now-empty list and runs on the NEXT frame — matching rAF semantics.
	TArray<FScheduledAnimationFrame> Current = MoveTemp(AnimationFrameCallbacks);
	AnimationFrameCallbacks.Reset();

	for (FScheduledAnimationFrame& Raf : Current)
	{
		if (Raf.Callback)
		{
			Raf.Callback(NowSeconds);
		}
	}
}

bool FCathodeScheduler::TryGetNextTimerDue(double& OutDueSeconds) const
{
	if (Timers.Num() == 0)
	{
		return false;
	}

	double Earliest = Timers[0].Due;
	for (const FScheduledTimer& Timer : Timers)
	{
		Earliest = FMath::Min(Earliest, Timer.Due);
	}
	OutDueSeconds = Earliest;
	return true;
}

} // namespace Cathode
