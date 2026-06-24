#pragma once

#include "CoreMinimal.h"

namespace Cathode
{

// 025 S1 — generate the Cathode authoring type surface (lib.cathode.d.ts) FROM the engine's own interfaces (Model A,
// tcdent FORK-1): the DOM/JS member surface is PROJECTED from the production FBoundMember binding tables (the same
// tables InstallBoundMembers registers), the signatures read from each member's own FBoundMember.Signature field — no
// hand-maintained member list. Three named hand-sections (referenced-but-table-less surfaces) are emitted around the
// generated member core, each guarded so it can't silently drift: the standard globals (console/timers/rAF — installed
// outside FBoundMember, name-guarded), the interface extends graph (the standard DOM inheritance, Iface-coverage
// guarded), and the auxiliary referenced types (NodeList/CSSStyleDeclaration/... — resolve-guarded). game.* is omitted
// (a named residual, pending the sim).
//
// The coherence test (CathodeAuthoringDtsTests) re-runs THIS function and asserts the committed lib.cathode.d.ts equals
// the regenerated artifact (regen-vs-committed), so a binding add/remove flows through and a stale commit REDs.
CATHODEDOM_API FString GenerateAuthoringDts();

} // namespace Cathode
