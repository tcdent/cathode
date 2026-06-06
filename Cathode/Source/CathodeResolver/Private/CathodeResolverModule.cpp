#include "CathodeResolverModule.h"

#include "Modules/ModuleManager.h"

void FCathodeResolverModule::StartupModule()
{
	// Passive service module — no worker thread, no runtime. A resolver is created on demand over a bundle
	// source (CreateResourceResolver). Nothing to start here.
}

void FCathodeResolverModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeResolverModule, CathodeResolver)
