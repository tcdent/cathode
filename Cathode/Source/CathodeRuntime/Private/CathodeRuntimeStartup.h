#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace Cathode
{

// The runtime startup sequence — the doc's ordered lifecycle:
//   1 create runtime/context -> 2 install safety budgets -> 3 assemble globals / register host fns ->
//   4 load author JS (a seam; no bundle loader is wired) -> 5 begin ticking.
// There is ONE definition of this order, here. The worker's Run() builds the real steps and calls
// Startup() once before its tick loop; the startup spec builds recording steps and calls the SAME
// Startup(). Tests isolate only the step ACTIONS (recording markers) — they never re-implement the
// sequence, so there is no forked startup path (same discipline as the tick's phase order).
class FRuntimeStartup
{
public:
	struct FSteps
	{
		TFunction<void()> CreateRuntimeContext; // create the runtime + its global context
		TFunction<void()> InstallSafetyBudgets; // interrupt policy + memory/stack ceilings
		TFunction<void()> AssembleGlobals;      // assemble the contract globals (console/timers/game.*/document)
		TFunction<void()> LoadAuthorScript;     // load the pre-bundled author JS (seam; a no-op in production)
		TFunction<void()> BeginTicking;         // startup complete -> the tick loop begins (runs after this)
	};

	explicit FRuntimeStartup(FSteps InSteps)
		: Steps(MoveTemp(InSteps))
	{
	}

	/** Run the ordered startup steps. Globals and the host API must exist before author code runs, and
	 *  author code must be loaded before ticking — hence this fixed order. */
	void Startup()
	{
		if (Steps.CreateRuntimeContext)
		{
			Steps.CreateRuntimeContext();
		}
		if (Steps.InstallSafetyBudgets)
		{
			Steps.InstallSafetyBudgets();
		}
		if (Steps.AssembleGlobals)
		{
			Steps.AssembleGlobals();
		}
		if (Steps.LoadAuthorScript)
		{
			Steps.LoadAuthorScript();
		}
		if (Steps.BeginTicking)
		{
			Steps.BeginTicking();
		}
	}

private:
	FSteps Steps;
};

} // namespace Cathode
