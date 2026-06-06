#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Templates/Function.h"
#endif

// The teardown-order contract for the JS runtime, shared between the runtime implementation and its
// lifecycle specs. Releasing every held JS value BEFORE freeing the context, and the context BEFORE
// the runtime, is the discipline that avoids the JS<->native lifetime hazard (a value outliving its
// context, or a context outliving its runtime). The enum fixes that order as a named contract.

namespace Cathode
{

/** The ordered steps the runtime's destructor performs, in required order. */
enum class ETeardownStep : uint8
{
	ReleaseHeldValues, // release every JS value Runtime Core holds
	FreeContext,       // then destroy the JS context
	FreeRuntime,       // then destroy the JS runtime
};

#if WITH_DEV_AUTOMATION_TESTS
// Test-injection seam (RQ1-sanctioned): when set, the production destructor taps this observer at
// each ordered teardown step, so a lifecycle spec records and asserts the REAL teardown order
// without forking the teardown code. Unset in production; defined in CathodeJsRuntime.cpp.
extern TFunction<void(ETeardownStep)> GTeardownObserver;
#endif

} // namespace Cathode
