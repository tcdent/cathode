#include "CathodeEgressModule.h"

#include "Modules/ModuleManager.h"

void FCathodeEgressModule::StartupModule()
{
	// Passive — no worker thread, no runtime. Egress instances are built on demand by the device-spawn path
	// (integration) via CreateTextureEgress. Nothing to start here.
}

void FCathodeEgressModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeEgressModule, CathodeEgress)
