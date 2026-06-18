#pragma once

#include "CoreMinimal.h"

// The CSS capability declaration's RUNTIME MIRROR (013 slice 1, ruling B) — a hand-authored C++ form of
// capability-manifest.toml [css] (the single source). The runtime consumers read THIS (element.style validation now;
// getComputedStyle's exposed-props later); the build-time linter reads the manifest TOML; a COHERENCE TEST binds
// them (mirror == manifest [css] AND every PRESENT name ⊆ litehtml's string_id.h parse names) — the no-drift guard
// that codegen would otherwise be. NO CathodeDom->litehtml runtime dependency: the mirror is a static neutral list.

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
