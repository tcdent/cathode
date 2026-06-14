#pragma once

#include "CoreMinimal.h"

namespace Cathode
{

/**
 * The LINK-TIME build-id (sprint 011 coherence-hardening), read from CathodeRuntime.dll's own loaded PE image
 * (PE TimeDateStamp + CodeView PDB GUID) — it advances on ANY relink of CathodeRuntime, so it is the
 * per-result-run coherence anchor emitted in the [CATHODE-BUILD-SIGNATURE] marker at plugin load.
 *
 * Exported so the runtime-verification faculty (CathodeVerify) injects the SAME id it captures in its
 * self-report — single-source, no duplicate PE read, so the report's coherence anchor matches the load marker.
 * Reads this module's own image regardless of the caller (the FROM_ADDRESS is the function itself, which lives
 * in CathodeRuntime.dll). Returns "unavailable"/non-Windows sentinel when it cannot be read.
 */
CATHODERUNTIME_API FString CathodeLinkTimeBuildId();

} // namespace Cathode
