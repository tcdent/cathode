#pragma once

#include "CoreMinimal.h"

namespace Cathode
{

// 025 S1 — the install-set NAMES of the CathodeContractGlobals global-level globals (console + timers + rAF/cancelrAF),
// the SINGLE SOURCE the install references. The authoring .d.ts codegen (GenerateAuthoringDts) emits one global per name
// (signatures hand-authored per name) + its guard asserts the .d.ts globals == this set, so a CathodeContractGlobals
// add/remove flows through (the no-drift binding for the globals section). NB `document` is installed separately via
// FDomDocument::InstallAsGlobal (per-surface) — the codegen adds it as a documented known beside this set.
// (A dedicated PUBLIC header — distinct from the Private CathodeContractGlobals.h install-binding decls — so the
// cross-module codegen consumer can include it without name-colliding with the Private one.)
CATHODERUNTIME_API TArray<FString> GetContractGlobalNames();

} // namespace Cathode
