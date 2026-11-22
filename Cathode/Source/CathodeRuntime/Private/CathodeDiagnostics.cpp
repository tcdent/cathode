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

void FLogDiagnosticSink::ReportConsole(EConsoleLevel Level, const FString& Message)
{
	// REAL override: route console output to the game log at its level, so production
	// console.{log,warn,error} are surfaced — never silently dropped into the additive default no-op.
	switch (Level)
	{
	case EConsoleLevel::Error:
		UE_LOG(LogCathodeScreen, Error, TEXT("console.error: %s"), *Message);
		break;
	case EConsoleLevel::Warn:
		UE_LOG(LogCathodeScreen, Warning, TEXT("console.warn: %s"), *Message);
		break;
	case EConsoleLevel::Log:
	default:
		UE_LOG(LogCathodeScreen, Log, TEXT("console.log: %s"), *Message);
		break;
	}
}

} // namespace Cathode
