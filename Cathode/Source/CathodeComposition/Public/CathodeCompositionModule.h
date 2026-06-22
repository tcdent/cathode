#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

// The CathodeComposition module — the integration composition root (sprint 023). Passive: it defines no
// runtime/worker; the composition surfaces are constructed on demand via MakeCompositionSurfaceRegistration.
class FCathodeCompositionModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
