#include "CathodeDomModule.h"

#include "Modules/ModuleManager.h"

void FCathodeDomModule::StartupModule()
{
	// Passive service module — no worker thread, no runtime. A DOM document is created on demand over the
	// siblings' APIs (CreateDomDocument). Nothing to start here.
}

void FCathodeDomModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeDomModule, CathodeDom)
