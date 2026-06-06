#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Cathode text module.
 *
 * Hosts Text & Fonts — the HarfBuzz shaper behind our swap boundary, the single shaped result, metrics
 * derivation, the shape-plan/result cache, per-run feature control, and the font registry + single-load
 * byte store. A passive service (no worker thread of its own): Layout & Style and the Rasterizer consume
 * its shaped result on the worker thread. A distinct module so the HarfBuzz dependency stays off the
 * other Cathode modules; it never reaches into Blend2D.
 */
class FCathodeTextModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
