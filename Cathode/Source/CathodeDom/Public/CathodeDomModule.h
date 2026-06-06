#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Cathode DOM module.
 *
 * Hosts the live retained document author JavaScript manipulates and the source of truth fed to layout — the
 * retained node tree (the JS wrapper graph, Model A), the bounded DOM API, the event system with bubbling,
 * observation reads, and dirty-marking. The COMPOSER of three built siblings (007 binding, 005 layout, 006
 * surfaces); it wraps no new vendor and is quickjs-type-coupled by design via 007. A passive service: the
 * document is created on demand over the siblings' APIs.
 */
class FCathodeDomModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
