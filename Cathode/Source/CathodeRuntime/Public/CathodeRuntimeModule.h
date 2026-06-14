#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"

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
