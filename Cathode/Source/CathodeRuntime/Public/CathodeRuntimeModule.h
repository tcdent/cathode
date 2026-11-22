#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"

#include "CathodeRuntimeSeam.h" // FSurfaceRelayoutSeam, FHostOutboundMessage

namespace Cathode
{
class FCathodeRuntimeWorker;
}

/**
 * Cathode runtime module.
 *
 * Owns the lifetime of the Cathode runtime worker (the single quickjs-ng runtime + its worker thread).
 * The module creates AND starts the worker on load (auto-start) and tears it down on unload, so the
 * ordered startup sequence — including the linked-native game-state attach — runs at module load rather
 * than lazily. The delegated components (surfaces, host bridge, DOM) live in their own modules.
 */
class FCathodeRuntimeModule final : public IModuleInterface
{
public:
	~FCathodeRuntimeModule(); // defined in the .cpp where the worker type is complete

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * The public seam-install + drain API (the worker is Private; this is the only production-reachable path
	 * to it). The surface-load owner (CathodeComposition) — and the real-worker tests — INSTALL the Phase-5 relayout
	 * seam onto the auto-started worker, so the composition (litehtml + HarfBuzz + Blend2D) runs on the worker thread;
	 * the game thread DRAINS the worker's frame-ready outbound to pump the egress upload half. Both forward to the
	 * worker; no-op / 0 if no worker is live. Reach the module via
	 * FModuleManager::GetModulePtr<FCathodeRuntimeModule>("CathodeRuntime").
	 */
	CATHODERUNTIME_API void InstallSurfaceRelayoutSeam(Cathode::FSurfaceRelayoutSeam Seam);
	CATHODERUNTIME_API int32 DrainOutbound(TArray<Cathode::FHostOutboundMessage>& Out);

	/**
	 * Post an arbitrary job onto the worker's inbound FIFO (run ON the worker thread, between ticks). The
	 * surface-load owner posts the per-surface FDomDocument::Dispose here at teardown — Dispose makes quickjs C-API
	 * calls so it MUST run on the worker (the same FIFO mechanism InstallSurfaceRelayoutSeam uses for the seam-set).
	 * No-op if no worker is live.
	 */
	CATHODERUNTIME_API void PostWorkerJob(TFunction<void()> Job);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * TEST-ONLY. Release the auto-started worker + its runtime so an
	 * automation test can own the runtime lifecycle on its OWN thread (single-thread affinity). It runs the
	 * EXACT production teardown via the shared TeardownWorker() helper below — no test-specific teardown that
	 * could diverge from production. IDEMPOTENT. Auto-start (StartupModule) is UNCHANGED — this only RELEASES
	 * the running instance for a test; it never alters production startup. Leaves a coherent state (claim
	 * free, no live runtime); a fresh launch re-auto-starts (an automation run has nothing downstream
	 * expecting the auto-started instance).
	 */
	void StopRuntimeForTesting();

	// Restore the auto-started worker for an IN-SUITE real-worker test. Earlier FScopedTestRuntime tests RELEASE the auto-started singleton (and do not re-start it — "a
	// fresh launch re-auto-starts", but the suite doesn't fresh-launch between tests), so by an in-suite real-worker
	// test's turn NO worker is running -> the test's condition-wait times out. RestartWorkerForTesting re-creates +
	// Starts the worker IF released (the single-instance claim is FREE post-release — the SAME path as the auto-start);
	// restore-IF-released (no double-start when the worker is live, e.g. isolated). The PRODUCTION auto-start
	// (StartupModule) is UNTOUCHED — this is a test-only RESTORE. IsWorkerRunningForTesting
	// reports whether a worker is live (false in-suite after a release, true isolated). CATHODERUNTIME_API: called cross-module by the real-worker tests.
	CATHODERUNTIME_API bool IsWorkerRunningForTesting() const;
	CATHODERUNTIME_API void RestartWorkerForTesting();
#endif

private:
	/**
	 * The SINGLE-SOURCE worker teardown — both ShutdownModule() and (test-only) StopRuntimeForTesting() call
	 * it, so the test-release path can never silently diverge from the production shutdown path.
	 * FCathodeRuntimeWorker::Shutdown joins the worker thread and tears the runtime down ON that thread
	 * (~FQuickJsRuntime releases the single-instance claim there); then the (now-empty) worker is reset.
	 * Idempotent: a no-op when no worker is live.
	 */
	void TeardownWorker();

	TUniquePtr<Cathode::FCathodeRuntimeWorker> Worker;
};
