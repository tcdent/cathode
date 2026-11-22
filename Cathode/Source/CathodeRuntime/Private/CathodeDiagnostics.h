#pragma once

#include "CoreMinimal.h"

#include "CathodeJsRuntime.h" // FJsError

namespace Cathode
{

// The console message level — console.log / warn / error map to these.
enum class EConsoleLevel : uint8
{
	Log,
	Warn,
	Error,
};

// The diagnostic sink — where uncaught JS errors are routed out (game log / dev overlay). It is the
// injectable seam for error surfacing: production logs, a dev overlay displays, and a spec records. A
// fault must be CONTAINED — reporting it must never throw or propagate a crash to the parent host.
class ICathodeDiagnosticSink
{
public:
	virtual ~ICathodeDiagnosticSink() = default;

	/** Route an uncaught JS error out. Must not throw or propagate to the parent. */
	virtual void ReportError(const FJsError& Error) = 0;

	/** Route a console.{log,warn,error} message out (the OnInstallConsole seam target).
	 *  ADDITIVE + default no-op: this COMPLETES the console channel the install seam always implied; it does
	 *  NOT touch ReportError. The default no-op keeps existing sinks compiling, but a sink that leaves it
	 *  defaulted silently DROPS console output — so any sink that surfaces console (the production
	 *  FLogDiagnosticSink, dev overlays, test recorders) MUST override it. */
	virtual void ReportConsole(EConsoleLevel /*Level*/, const FString& /*Message*/) {}
};

/** Production sink: routes uncaught errors + console output to the game log. */
class FLogDiagnosticSink final : public ICathodeDiagnosticSink
{
public:
	virtual void ReportError(const FJsError& Error) override;
	// REAL production override (not the inherited no-op): console output reaches the game log at its level,
	// so console.{log,warn,error} are usable in production, never masked by the additive default.
	virtual void ReportConsole(EConsoleLevel Level, const FString& Message) override;
};

} // namespace Cathode
