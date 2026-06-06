#include "CathodeInterruptBudget.h"

#include "CathodeClock.h"

namespace Cathode
{

void FInterruptBudget::BeginExecution()
{
	ExecutionStartSeconds = Clock.NowSeconds();
	bExecuting = true;
}

bool FInterruptBudget::ShouldInterrupt() const
{
	if (!bExecuting)
	{
		// No active execution window -> never abort.
		return false;
	}

	const double Elapsed = Clock.NowSeconds() - ExecutionStartSeconds;
	return Elapsed > TimeBudgetSeconds;
}

} // namespace Cathode
