#include "CathodeVerifyModule.h"

#include "Modules/ModuleManager.h"

void FCathodeVerifyModule::StartupModule()
{
	// Passive capability module — no worker thread, no auto-run. The faculty is driven on demand (in a
	// Cathode runtime) and emits its self-report; the automation gate drives it via the production runtime.
	// Nothing to start here.
}

void FCathodeVerifyModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FCathodeVerifyModule, CathodeVerify)
