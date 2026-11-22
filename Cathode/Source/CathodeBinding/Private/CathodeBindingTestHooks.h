#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace Cathode
{

// Test-injection seam (the CathodeRuntime GTeardownObserver pattern): FJsValue taps these AFTER the real JS_DupValue /
// JS_FreeValue / JS_MarkValue / JS_FreeValueRT run, so a test can count OUR dup/free/mark/rt-free decisions
// without forking the production path and without asserting quickjs-ng GC internals. Unset in normal use
// (taps are no-ops). GJsValueOnFree counts the ctx-based destructor path; GJsValueOnFreeRT counts the
// finalizer-safe rt-based path; GJsValueOnMark counts gc_mark visibility calls.
extern TFunction<void()> GJsValueOnDup;
extern TFunction<void()> GJsValueOnFree;
extern TFunction<void()> GJsValueOnFreeRT;
extern TFunction<void()> GJsValueOnMark;

} // namespace Cathode

#endif // WITH_DEV_AUTOMATION_TESTS
