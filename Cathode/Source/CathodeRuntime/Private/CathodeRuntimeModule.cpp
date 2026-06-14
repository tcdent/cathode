#include "CathodeRuntimeModule.h"

#include "CathodeBuildId.h"
#include "CathodeRuntimeWorker.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Build-coherence marker (sprint 010): a log category for the build signature below. The link-id mechanism
// (CathodeLinkTimeBuildId + the cross-module CathodeAggregateBuildId) lives in CathodeBuildId.cpp.
DEFINE_LOG_CATEGORY_STATIC(LogCathodeBuild, Log, All);

FCathodeRuntimeModule::~FCathodeRuntimeModule() = default;

void FCathodeRuntimeModule::StartupModule()
{
	// BUILD-COHERENCE MARKER: the [CATHODE-BUILD-SIGNATURE] line carries CathodeRuntime's OWN per-load link-id
	// (advances on ANY CathodeRuntime relink). NOTE: this is CathodeRuntime's per-load signature — emitted at
	// startup, before sibling Cathode modules are guaranteed loaded, so it canNOT aggregate them here. The
	// CROSS-MODULE coherence anchor is the faculty's coherence.buildId (Cathode::CathodeAggregateBuildId(),
	// read at faculty time when all Cathode modules are loaded). The "(module-TU compiled ...)" suffix is THIS
	// TU's compile time only — supplementary context, not the anchor.
	UE_LOG(LogCathodeBuild, Display, TEXT("[CATHODE-BUILD-SIGNATURE] Cathode plugin loaded; build-id %s (module-TU compiled %s %s)"),
		*Cathode::CathodeLinkTimeBuildId(), ANSI_TO_TCHAR(__DATE__), ANSI_TO_TCHAR(__TIME__));

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
