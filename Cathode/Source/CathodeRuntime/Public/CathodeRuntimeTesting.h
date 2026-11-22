#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

#include "CathodeJsRuntime.h"

namespace Cathode
{

/**
 * TEST-ONLY runtime seam.
 *
 * The plugin auto-starts ONE JS runtime at module load — correct product behaviour, and the single-instance
 * guard gracefully refuses a second. An automation test that needs
 * to own the runtime lifecycle (create/use/destroy on the TEST thread — quickjs is single-thread-affine, the
 * same affinity that ruled out driving the worker-thread-bound auto-started instance) must first RELEASE that
 * auto-started singleton.
 *
 * These helpers do so by ORCHESTRATING PRODUCTION CALLS ONLY — the production worker teardown
 * (FCathodeRuntimeWorker::Shutdown -> ~FQuickJsRuntime releases the claim) + the production factory
 * (CreateJsRuntime) + IsValid. There is NO parallel/fake runtime and NO change to the production auto-start
 * path; the holder is a thin orchestration over existing product levers.
 */
namespace Testing
{
	/**
	 * Release the module's auto-started runtime via the production teardown path, freeing the
	 * single-instance claim. IDEMPOTENT. After this, CreateJsRuntime() on the calling thread becomes the
	 * live instance. Leaves a coherent state (claim free, no live runtime) — a fresh editor launch
	 * re-auto-starts; an automation run has nothing downstream expecting the auto-started instance.
	 */
	CATHODERUNTIME_API void ReleaseAutoStartedRuntime();
}

/**
 * RAII test holder for a runtime the test OWNS.
 *
 * On construction: releases the auto-started singleton (production teardown), then creates a test-owned
 * runtime on the CALLING (test) thread via the production factory. On destruction: the runtime is torn down
 * (TUniquePtr), freeing the claim for the next test — leaving the coherent free-singleton state.
 *
 * IsValid() is the DEFENSIVE GUARD: tests MUST assert IsValid() before using Context(),
 * which returns nullptr when invalid rather than handing back a value to null-deref (the exact crash an
 * unguarded use-test hits). The runtime is a production-factory instance reached through the legitimate reset seam on
 * the test thread — production-path, not a parallel runtime.
 */
class CATHODERUNTIME_API FScopedTestRuntime
{
public:
	FScopedTestRuntime();

	/**
	 * The DEFENSIVE GUARD: true only when the test-owned runtime was created and
	 * reports valid. Each test MUST gate on this before using the runtime (e.g.
	 * `if (!RT.IsValid()) { AddError(...); return false; }`) — abort rather than deref an invalid runtime,
	 * which is the exact crash an unguarded use-test hits (deref of a refused CreateJsRuntime result).
	 */
	bool IsValid() const;

	// Pointer-like access so existing `Runtime->X()` / `*Runtime` test code works unchanged once the
	// declaration is swapped to the holder. Only valid when IsValid(); gate first.
	ICathodeJsRuntime* operator->() const { return Runtime.Get(); }
	ICathodeJsRuntime& operator*() const { return *Runtime; }
	ICathodeJsRuntime& Get() const { return *Runtime; }

private:
	TUniquePtr<ICathodeJsRuntime> Runtime;
};

} // namespace Cathode

#endif // WITH_DEV_AUTOMATION_TESTS
