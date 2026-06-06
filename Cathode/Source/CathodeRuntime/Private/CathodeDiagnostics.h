#pragma once

#include "CoreMinimal.h"

#include "CathodeJsRuntime.h" // FJsError

namespace Cathode
{

// The diagnostic sink — where uncaught JS errors are routed out (game log / dev overlay). It is the
// injectable seam for error surfacing: production logs, a dev overlay displays, and a spec records. A
// fault must be CONTAINED — reporting it must never throw or propagate a crash to the parent host.
class ICathodeDiagnosticSink
{
public:
	virtual ~ICathodeDiagnosticSink() = default;

	/** Route an uncaught JS error out. Must not throw or propagate to the parent. */
	virtual void ReportError(const FJsError& Error) = 0;
};

/** Production sink: routes uncaught errors to the game log. */
class FLogDiagnosticSink final : public ICathodeDiagnosticSink
{
public:
	virtual void ReportError(const FJsError& Error) override;
};

} // namespace Cathode
