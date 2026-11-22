#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace Cathode
{

// Timer + requestAnimationFrame scheduling — OUR selection logic over the worker's injected clock.
// This is the INTERNAL C++ seam; the JS setTimeout / requestAnimationFrame globals are wired onto it in
// CathodeContractGlobals, not here. Bespoke + testable: due-timer selection (deadline <= now, earliest first) and rAF
// one-shot semantics (a callback re-registered during the run fires on the NEXT frame, not this one).
class FCathodeScheduler
{
public:
	using FTimerCallback = TFunction<void()>;
	using FAnimationFrameCallback = TFunction<void(double /*NowSeconds*/)>;

	/** Register a timer due at an absolute time (seconds, worker clock). Returns a cancel handle. */
	int32 SetTimer(double DueSeconds, FTimerCallback Callback);

	/** Cancel a pending timer by handle (no-op if already fired/cleared). */
	void ClearTimer(int32 Handle);

	/** Register a one-shot animation-frame callback for the next frame. Returns a cancel handle. */
	int32 RequestAnimationFrame(FAnimationFrameCallback Callback);

	/** Cancel a pending rAF callback by handle. */
	void CancelAnimationFrame(int32 Handle);

	/** Fire every timer whose deadline has passed (Due <= Now), earliest first; removes those fired. */
	void FireDueTimers(double NowSeconds);

	/** Run every currently-registered rAF callback (passing Now), then clear them; callbacks registered
	 *  during the run are deferred to the next frame (one-shot semantics). */
	void RunAnimationFrameCallbacks(double NowSeconds);

	bool HasPendingAnimationFrame() const { return AnimationFrameCallbacks.Num() > 0; }
	bool HasPendingTimer() const { return Timers.Num() > 0; }

	/** Earliest pending timer deadline; false if none pending. */
	bool TryGetNextTimerDue(double& OutDueSeconds) const;

	/** Drop all pending timers + rAF callbacks. Teardown release: a callback may hold a resource
	 *  (the JS-callback ref); call this BEFORE the JS context is freed so the callbacks release while it is
	 *  still alive (the worker calls it just before tearing down the runtime). Empties the selection state only;
	 *  the selection LOGIC is unchanged. */
	void Reset()
	{
		Timers.Reset();
		AnimationFrameCallbacks.Reset();
	}

private:
	struct FScheduledTimer
	{
		int32 Handle = 0;
		double Due = 0.0;
		FTimerCallback Callback;
	};

	struct FScheduledAnimationFrame
	{
		int32 Handle = 0;
		FAnimationFrameCallback Callback;
	};

	TArray<FScheduledTimer> Timers;
	TArray<FScheduledAnimationFrame> AnimationFrameCallbacks;
	int32 NextHandle = 1;
};

} // namespace Cathode
