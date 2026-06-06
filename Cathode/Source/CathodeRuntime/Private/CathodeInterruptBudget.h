#pragma once

#include "CoreMinimal.h"

namespace Cathode
{

class ICathodeClock;

// The interrupt-budget POLICY — OUR logic, independent of quickjs. The engine calls our interrupt
// handler periodically during long execution; this decides whether to ABORT. It is a STABILITY /
// parent-isolation guard for trusted code: a buggy author while(true) must not hang every surface or
// take down the game. It is NOT a security sandbox — there is no attack surface — so there is no
// threat-model machinery here, just a wall-clock bound on a single execution window, measured on the
// injected clock so it is testable deterministically without the engine.
class FInterruptBudget
{
public:
	static constexpr double DefaultTimeBudgetSeconds = 1.0;

	explicit FInterruptBudget(const ICathodeClock& InClock, double InTimeBudgetSeconds = DefaultTimeBudgetSeconds)
		: Clock(InClock)
		, TimeBudgetSeconds(InTimeBudgetSeconds)
	{
	}

	/** Open a fresh execution window — called immediately before a JS execution begins. */
	void BeginExecution();

	/** The abort decision the interrupt handler asks for: true => abort (the window is over budget). */
	bool ShouldInterrupt() const;

	void SetTimeBudgetSeconds(double Seconds) { TimeBudgetSeconds = Seconds; }
	double GetTimeBudgetSeconds() const { return TimeBudgetSeconds; }

private:
	const ICathodeClock& Clock;
	double TimeBudgetSeconds;
	double ExecutionStartSeconds = 0.0;
	bool bExecuting = false;
};

} // namespace Cathode
