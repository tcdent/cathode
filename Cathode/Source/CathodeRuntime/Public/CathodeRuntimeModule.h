#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Cathode runtime module.
 *
 * Owns the lifetime of the Cathode runtime: the single quickjs-ng runtime, the worker thread,
 * the surfaces, and the texture-upload path. This sprint (001) declares the module as scaffold
 * only — the worker, surfaces, and host bridge are authored in later sprints.
 */
class FCathodeRuntimeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
