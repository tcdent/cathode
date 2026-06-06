#include "CathodeSurfacesModule.h"

#include "Modules/ModuleManager.h"

void FCathodeSurfacesModule::StartupModule()
{
	// Passive service module — no worker thread, no runtime. The surface registry is created on demand by
	// whoever owns the many-surfaces set. Nothing to start here.
}

void FCathodeSurfacesModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeSurfacesModule, CathodeSurfaces)
