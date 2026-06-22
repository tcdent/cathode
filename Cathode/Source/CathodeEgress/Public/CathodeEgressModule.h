#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

// The CathodeEgress module — Texture Egress (sprint 024), the sole RHI touchpoint. Passive: it defines no
// runtime/worker. The egress is constructed on demand by the device-spawn path (integration) via
// CreateTextureEgress and wired to the surface registry's readiness relay.
class FCathodeEgressModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
