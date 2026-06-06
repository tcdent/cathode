#include "CathodeTextModule.h"

#include "Modules/ModuleManager.h"

void FCathodeTextModule::StartupModule()
{
	// Passive service module — no worker thread, no runtime to spin up. The shaper + registry are created
	// on demand by whoever shapes text. Nothing to start here.
}

void FCathodeTextModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeTextModule, CathodeText)
