#include "CathodeRuntimeModule.h"

#include "CathodeRuntimeWorker.h"

#include "Modules/ModuleManager.h"

// Build-coherence marker (sprint 010): a log category for the build signature below.
DEFINE_LOG_CATEGORY_STATIC(LogCathodeBuild, Log, All);

FCathodeRuntimeModule::~FCathodeRuntimeModule() = default;

void FCathodeRuntimeModule::StartupModule()
{
	// BUILD-COHERENCE MARKER (sprint 010, RQ2): emitted ONLY by this build, carrying the COMPILE timestamp, so
	// any run is provably from the CURRENT binary — a stale binary shows an older timestamp (source-mtime
	// preceding the run is NOT sufficient proof). Logged first (before the worker), on plugin load. Grep a run
	// log for [CATHODE-BUILD-SIGNATURE] to confirm binary freshness before reading the run as evidence.
	UE_LOG(LogCathodeBuild, Display, TEXT("[CATHODE-BUILD-SIGNATURE] Cathode plugin loaded; compiled %s %s"),
		ANSI_TO_TCHAR(__DATE__), ANSI_TO_TCHAR(__TIME__));

	// Auto-start on module load (human-ruled): create the worker AND start its thread, which runs the
	// ordered Startup() sequence (create -> budgets -> assemble globals -> load author JS -> begin
	// ticking). The linked-native game-state object (game.*) attaches AT startup, not lazily — deferring
	// the worker would defer that attach and risk inconsistent state. A plugin included in the engine is
	// assumed to be in use, so there is no idle-spin-up to avoid.
	Worker = MakeUnique<Cathode::FCathodeRuntimeWorker>();
	Worker->Start();
}

void FCathodeRuntimeModule::ShutdownModule()
{
	TeardownWorker();
}

void FCathodeRuntimeModule::TeardownWorker()
{
	// SINGLE-SOURCE teardown (shared by ShutdownModule + the test-only StopRuntimeForTesting): Worker::Shutdown
	// joins the worker thread and tears the runtime down ON that thread (affinity-correct), then
	// ~FQuickJsRuntime releases the single-instance claim. Idempotent: no-op if no worker is live.
	if (Worker.IsValid())
	{
		Worker->Shutdown();
		Worker.Reset();
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void FCathodeRuntimeModule::StopRuntimeForTesting()
{
	// Run the EXACT production teardown (the shared helper) — no test-specific path that could diverge.
	// After this the claim is free + no runtime is live (coherent), so an automation test can
	// CreateJsRuntime() its own instance on the TEST thread.
	TeardownWorker();
}
#endif

IMPLEMENT_MODULE(FCathodeRuntimeModule, CathodeRuntime)
