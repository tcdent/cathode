#include "CathodeRuntimeModule.h"

#include "Modules/ModuleManager.h"

void FCathodeRuntimeModule::StartupModule()
{
	// Scaffold (sprint 001): runtime startup — worker thread, quickjs-ng runtime, and surface
	// registry — is authored in a later sprint. The module exists so the plugin shape is complete.
}

void FCathodeRuntimeModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeRuntimeModule, CathodeRuntime)
