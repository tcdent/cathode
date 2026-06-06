#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

namespace Cathode
{

// The worker's clock seam. Timers and requestAnimationFrame (Slice 4) read time through this — never
// FPlatformTime directly — so the tick can be driven deterministically against an injected fake clock
// in tests, while production uses the real platform clock.
class ICathodeClock
{
public:
	virtual ~ICathodeClock() = default;

	/** Seconds on the worker's own timeline. */
	virtual double NowSeconds() const = 0;
};

/** Production clock: the platform seconds clock. */
class FPlatformClock final : public ICathodeClock
{
public:
	virtual double NowSeconds() const override { return FPlatformTime::Seconds(); }
};

} // namespace Cathode
