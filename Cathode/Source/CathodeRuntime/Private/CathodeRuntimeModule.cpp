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
	// Tear down the worker (joins the thread + tears the runtime down on it, if it was started).
	if (Worker.IsValid())
	{
		Worker->Shutdown();
		Worker.Reset();
	}
}

IMPLEMENT_MODULE(FCathodeRuntimeModule, CathodeRuntime)
