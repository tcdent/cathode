#include "CathodeCompositionModule.h"

#include "Modules/ModuleManager.h"

void FCathodeCompositionModule::StartupModule()
{
	// Passive composition root — no worker thread, no runtime. Composition surfaces are built on demand by
	// the device-spawn path (integration) via MakeCompositionSurfaceRegistration. Nothing to start here.
}

void FCathodeCompositionModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeCompositionModule, CathodeComposition)
