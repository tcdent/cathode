#include "CathodeRasterizerModule.h"

#include "Modules/ModuleManager.h"

void FCathodeRasterizerModule::StartupModule()
{
	// Passive service module — no worker thread, no runtime to spin up. The backend is created on demand
	// (CreateBlend2DBackend) by whoever services the relayout seam. Nothing to start here.
}

void FCathodeRasterizerModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeRasterizerModule, CathodeRasterizer)
