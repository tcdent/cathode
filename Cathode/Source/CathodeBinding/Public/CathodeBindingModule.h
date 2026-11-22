#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Cathode JS Binding module.
 *
 * Hosts the JS binding layer — value RAII over quickjs-ng refcounts, appendable native-class registration
 * (class + opaque + finalizer), the two generic handle mechanisms (wrapped-native-object, held-callable),
 * bounded value marshaling, and the teardown policy. Vendor-confined: quickjs.h + JS_* live in this module
 * (impl TUs and, by design, its public header) and never leak into a consumer — the module is
 * the swap boundary. A passive service: it composes CathodeRuntime's one confined JSContext, never a second runtime.
 */
class FCathodeBindingModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
