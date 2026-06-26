#pragma once

#include "Modules/ModuleInterface.h"

// 031 — the reference example-integration module (host-agnostic). Passive: it ships the reference host bridge + the wiring
// pattern; it has no worker/runtime of its own (the integrator's host owns those).
class FCathodeExampleModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
