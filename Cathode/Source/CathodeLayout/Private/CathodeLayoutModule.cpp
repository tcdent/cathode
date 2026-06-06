#include "CathodeLayoutModule.h"

#include "Modules/ModuleManager.h"

void FCathodeLayoutModule::StartupModule()
{
	// Passive service module — no worker thread, no runtime. The layout engine is created on demand by
	// whoever lays out a document. Nothing to start here.
}

void FCathodeLayoutModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeLayoutModule, CathodeLayout)
