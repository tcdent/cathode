#include "CathodeRuntimeModule.h"

#include "CathodeRuntimeWorker.h"

#include "Modules/ModuleManager.h"

FCathodeRuntimeModule::~FCathodeRuntimeModule() = default;

void FCathodeRuntimeModule::StartupModule()
{
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
