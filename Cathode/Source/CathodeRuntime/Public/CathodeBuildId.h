#pragma once

#include "CoreMinimal.h"

namespace Cathode
{

/**
 * The LINK-TIME build-id of CathodeRuntime itself (sprint 011 coherence-hardening), read from
 * CathodeRuntime.dll's own loaded PE image (PE TimeDateStamp + CodeView PDB GUID) — it advances on ANY relink
 * of CathodeRuntime. Emitted in the [CATHODE-BUILD-SIGNATURE] marker at plugin load as CathodeRuntime's
 * per-load signature. Reads this module's own image regardless of the caller (the FROM_ADDRESS is the function
 * itself, which lives in CathodeRuntime.dll). Returns "unavailable"/non-Windows sentinel when unreadable.
 */
CATHODERUNTIME_API FString CathodeLinkTimeBuildId();

/**
 * The CROSS-MODULE aggregate coherence anchor ([c]-prep hardening): a LIVE read, at call time, of EVERY
 * currently-loaded Cathode* module's PE link-id (same per-module mechanism as CathodeLinkTimeBuildId), folded
 * (sorted by module name) into one string `Name=link:..;...`. A relink of ANY Cathode module changes its
 * segment, so the aggregate advances — closing the cross-module gap the single-module build-id leaves (it
 * cannot witness a sibling module's out-of-band relink). Because it walks the actually-loaded images at call
 * time (not a build-time-baked manifest), it witnesses any sibling relink; and for a fixed set of binaries it
 * is deterministic (identical on re-run). This is the faculty's coherence.buildId (injected + reported).
 * Cathode-internal: per-module PE reads of our own loaded images via the OS; no DbgHelp, no shared tooling.
 */
CATHODERUNTIME_API FString CathodeAggregateBuildId();

} // namespace Cathode
