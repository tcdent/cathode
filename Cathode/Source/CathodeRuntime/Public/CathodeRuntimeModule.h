#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"

#include "CathodeRuntimeSeam.h" // FSurfaceRelayoutSeam, FHostOutboundMessage (028 S2)

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
 * than lazily. The delegated components (surfaces, host bridge, DOM) are future sprints.
 */
class FCathodeRuntimeModule final : public IModuleInterface
{
public:
	~FCathodeRuntimeModule(); // defined in the .cpp where the worker type is complete

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * 028 S2 — the public seam-install + drain API (the worker is Private; this is the only production-reachable path
	 * to it). The surface-load owner (CathodeComposition, S5) — and the S2 gate test — INSTALL the Phase-5 relayout
	 * seam onto the auto-started worker, so the composition (litehtml + HarfBuzz + Blend2D) runs on the worker thread;
	 * the game thread DRAINS the worker's frame-ready outbound to pump the egress upload half. Both forward to the
	 * worker; no-op / 0 if no worker is live. Reach the module via
	 * FModuleManager::GetModulePtr<FCathodeRuntimeModule>("CathodeRuntime").
	 */
	CATHODERUNTIME_API void InstallSurfaceRelayoutSeam(Cathode::FSurfaceRelayoutSeam Seam);
	CATHODERUNTIME_API int32 DrainOutbound(TArray<Cathode::FHostOutboundMessage>& Out);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * TEST-ONLY (sprint 011, finding-#1 unblock). Release the auto-started worker + its runtime so an
	 * automation test can own the runtime lifecycle on its OWN thread (single-thread affinity). It runs the
	 * EXACT production teardown via the shared TeardownWorker() helper below — no test-specific teardown that
	 * could diverge from production. IDEMPOTENT. Auto-start (StartupModule) is UNCHANGED — this only RELEASES
	 * the running instance for a test; it never alters production startup. Leaves a coherent state (claim
	 * free, no live runtime); a fresh launch re-auto-starts (an automation run has nothing downstream
	 * expecting the auto-started instance).
	 */
	void StopRuntimeForTesting();

	// 028 S2 (#4) — restore the auto-started worker for an IN-SUITE real-worker test (the gate / the 026-via-worker
	// gate-sweep). Earlier FScopedTestRuntime tests RELEASE the auto-started singleton (and do not re-start it — "a
	// fresh launch re-auto-starts", but the suite doesn't fresh-launch between tests), so by an in-suite real-worker
	// test's turn NO worker is running -> the gate's condition-wait times out. RestartWorkerForTesting re-creates +
	// Starts the worker IF released (the single-instance claim is FREE post-release — the SAME path as the auto-start);
	// restore-IF-released (no double-start when the worker is live, e.g. isolated). The PRODUCTION auto-start
	// (StartupModule, tcdent's human-ruled gamma) is UNTOUCHED — this is a test-only RESTORE. IsWorkerRunningForTesting
	// pins the release hypothesis (false in-suite, true isolated). CATHODERUNTIME_API: called cross-module by the device gate test.
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
