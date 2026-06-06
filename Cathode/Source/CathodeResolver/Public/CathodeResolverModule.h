#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Cathode resource-resolver module.
 *
 * Hosts the one place that maps logical URLs to bundled content — backing 005's litehtml resource callbacks,
 * providing author-JS bytes to 002, and sourcing url->bytes for 004's fonts. Small + pure-bespoke,
 * couples-to-none: it defines neutral seams (IBundleSource / IResourceResolver) and wraps no vendor. A
 * passive service: a resolver is created on demand over a bundle source. No network I/O.
 */
class FCathodeResolverModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
