#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"

namespace Cathode
{
class FCathodeRuntimeWorker;
}

/**
 * Cathode runtime module.
 *
 * Owns the lifetime of the Cathode runtime worker (the single quickjs-ng runtime + its worker thread).
 * The module creates AND starts the worker on load (auto-start) and tears it down on unload, so the
 * ordered startup sequence — including the linked-native game-state attach — runs at module load rather
 * than lazily. The delegated components (surfaces, host bridge, DOM) are future sprints.
 */
class FCathodeRuntimeModule final : public IModuleInterface
{
public:
	~FCathodeRuntimeModule(); // defined in the .cpp where the worker type is complete

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TUniquePtr<Cathode::FCathodeRuntimeWorker> Worker;
};
