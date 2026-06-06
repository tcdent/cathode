#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Cathode layout module.
 *
 * Hosts Layout & Style — litehtml behind our swap boundary, the document_container routing, dirty
 * tracking + the strategy-agnostic relayout (complete re-feed + the intelligent affected-subtree path),
 * paint routing, border decomposition, and the geometry/style observation read. A passive service: the
 * DOM drives relayout-on-dirty; the Rasterizer + Text consume its neutral seams. A distinct module so
 * the litehtml dependency stays off the other Cathode modules; it never reaches into Blend2D or HarfBuzz.
 */
class FCathodeLayoutModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
