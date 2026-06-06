#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTLS.h"

namespace Cathode
{

// The single-thread invariant mechanism. The worker binds this to its own thread at startup; all JS
// access must happen on that thread. The guard compares the calling thread to the bound one and
// REPORTS a violation by returning false — it never crashes the host (the policy on a violation, e.g.
// log-and-skip, is the caller's; this matches the stability mentality, not a security sandbox). The
// bound thread id is injectable so the invariant is testable deterministically without spinning a
// second thread.
class FThreadAffinity
{
public:
	/** Bind to the calling thread — the worker calls this from inside its Run(). */
	void BindToCurrentThread()
	{
		BoundThreadId = FPlatformTLS::GetCurrentThreadId();
		bBound = true;
	}

	/** Bind to an explicit thread id — for tests, to exercise the on/off-thread cases deterministically. */
	void BindToThreadId(uint32 ThreadId)
	{
		BoundThreadId = ThreadId;
		bBound = true;
	}

	/** True only when called from the bound thread; false if unbound or on any other thread. */
	bool IsOnBoundThread() const
	{
		return bBound && FPlatformTLS::GetCurrentThreadId() == BoundThreadId;
	}

	bool IsBound() const { return bBound; }
	uint32 GetBoundThreadId() const { return BoundThreadId; }

private:
	uint32 BoundThreadId = 0;
	bool bBound = false;
};

} // namespace Cathode
