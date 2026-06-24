#pragma once

#include "CoreMinimal.h"

// The CSS capability declaration's RUNTIME MIRROR (013 slice 1, ruling B) — a hand-authored C++ supported-CSS set.
// The runtime consumers read THIS (element.style validation now; getComputedStyle's exposed-props later); a COHERENCE
// TEST binds its Present partition to litehtml (mirror Present == string_id.h parse-names MINUS the 7 exclusions) — the
// no-drift guard that codegen would otherwise be. NO CathodeDom->litehtml runtime dependency: a static neutral list.
// 025: capability-manifest.toml is RETIRED (Model A — css-allow-list.toml is now generated off litehtml by the codegen,
// see CathodeCssAllowList). This mirror is TRANSITIONAL: 025 S3 re-points the runtime consumers to the baked
// CathodeCssAllowListGenerated set and retires this hand-authored mirror.

namespace Cathode
{

// Per the manifest [css] status: PRESENT = litehtml parses + cascades it. CathodeComputed is Axis-3 IN but litehtml lacks
// the parse name (declared, never an undeclared drop) — Cathode OWNS the value: getComputedStyle parses the element's OWN
// inline value (own-inline + the CSS initial correct; a cascaded/stylesheet value + rendering are deferred). Migration is
// PROPERTY-DEPENDENT: letter-spacing TRANSITIONAL (-> Present if litehtml ever parses it); opacity TERMINAL (litehtml never
// parses opacity — compositing is the rasterizer's job, not a layout input; the value stays Cathode-owned, only RENDERING
// comes via the rasterizer build-how). Both statuses are DECLARED; only the unsupported (undeclared) are dropped.
// (build_how_pending retired at 014 slice 2 — opacity was its last entry; re-add when a real pending prop appears.)
enum class ECssPropertyStatus : uint8
{
	Present,
	CathodeComputed,
};

struct FCssCapabilityEntry
{
	const TCHAR* Name;
	ECssPropertyStatus Status;
};

// The mirror (defined in CathodeCssCapability.cpp; the coherence test asserts it == manifest [css]).
const TArray<FCssCapabilityEntry>& GetCssCapabilityMirror();

// element.style / setProperty validation predicate (fork-3 accept-and-ignore): a property NAME is DECLARED (and so
// accepted) iff it is in the mirror (Present OR CathodeComputed). An undeclared name is silently dropped.
bool IsDeclaredCssProperty(const FString& Name);

} // namespace Cathode
