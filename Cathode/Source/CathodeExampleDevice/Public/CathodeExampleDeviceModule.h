#pragma once

#include "Modules/ModuleInterface.h"

// 031 S3 — the reference targetable-device-actor module (host-agnostic, Engine-linking). Passive: it ships the reference
// example actor; the integrator spawns + wires it.
class FCathodeExampleDeviceModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
