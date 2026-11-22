#include "CathodeBindingModule.h"

#include "Modules/ModuleManager.h"

void FCathodeBindingModule::StartupModule()
{
	// Passive service module — no worker thread, no runtime. The binding registries are created on demand
	// over CathodeRuntime's context by whoever owns the bindings. Nothing to start here.
}

void FCathodeBindingModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeBindingModule, CathodeBinding)
