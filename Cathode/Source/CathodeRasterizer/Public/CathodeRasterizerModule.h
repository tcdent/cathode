#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Cathode rasterizer module.
 *
 * Hosts the Rasterizer — the CPU paint backend (Blend2D, JIT via AsmJit) behind our swap boundary, the
 * CPU bitmap + double-buffering, dirty-region tracking, and the draw-dispatch. It is a passive service
 * (no worker thread of its own): Runtime Core drives it through the phase-5 relayout seam, on the worker
 * thread. A distinct module so the Blend2D/AsmJit dependency stays off CathodeRuntime.
 */
class FCathodeRasterizerModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
