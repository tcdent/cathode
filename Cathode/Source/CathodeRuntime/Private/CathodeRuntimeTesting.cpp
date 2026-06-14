#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeRuntimeTesting.h"

#include "CathodeRuntimeModule.h"

#include "Modules/ModuleManager.h"

namespace Cathode
{

void Testing::ReleaseAutoStartedRuntime()
{
	// Reach the loaded module and drive its production teardown of the auto-started worker. GetModulePtr is
	// null-safe (no-op if the module isn't loaded); StopRuntimeForTesting is itself idempotent.
	if (FCathodeRuntimeModule* Module = FModuleManager::GetModulePtr<FCathodeRuntimeModule>(TEXT("CathodeRuntime")))
	{
		Module->StopRuntimeForTesting();
	}
}

FScopedTestRuntime::FScopedTestRuntime()
{
	Testing::ReleaseAutoStartedRuntime(); // free the auto-started singleton via production teardown
	Runtime = CreateJsRuntime();          // production factory, on THIS (test) thread -> affinity-correct
}

bool FScopedTestRuntime::IsValid() const
{
	return Runtime.IsValid() && Runtime->IsValid();
}

} // namespace Cathode

#endif // WITH_DEV_AUTOMATION_TESTS
