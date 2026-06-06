#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Cathode surfaces module.
 *
 * Hosts Surfaces — the per-surface world-object layer and the many-surfaces model: one shared runtime
 * driving many independent displays. The FIRST pure-bespoke Cathode component (it wraps no vendor). A
 * passive service: it composes 003's per-surface producer (the render target), 005's document, and relays
 * surface-addressed readiness to Texture Egress — all through engine-neutral seams. A distinct module so
 * "couples to none" holds; it imports no sibling Cathode module and no vendor.
 */
class FCathodeSurfacesModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
