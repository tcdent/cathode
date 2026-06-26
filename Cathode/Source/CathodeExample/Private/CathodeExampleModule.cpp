#include "CathodeExampleModule.h"

#include "Modules/ModuleManager.h"

void FCathodeExampleModule::StartupModule()
{
	// Passive reference module — nothing to start. The example bridge + wiring are used by an integrator (and the proof).
}

void FCathodeExampleModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeExampleModule, CathodeExample)
