#include "CathodeDiagnostics.h"

DEFINE_LOG_CATEGORY_STATIC(LogCathodeScreen, Log, All);

namespace Cathode
{

void FLogDiagnosticSink::ReportError(const FJsError& Error)
{
	// Contained: log and return. An uncaught error in one surface's JS must never crash or corrupt the
	// parent host — it is a screen-level diagnostic, not a process fault.
	if (Error.Stack.IsEmpty())
	{
		UE_LOG(LogCathodeScreen, Error, TEXT("Uncaught screen error: %s"), *Error.Message);
	}
	else
	{
		UE_LOG(LogCathodeScreen, Error, TEXT("Uncaught screen error: %s\n%s"), *Error.Message, *Error.Stack);
	}
}

} // namespace Cathode
