#pragma once

#include "CoreMinimal.h"

// The CSS capability declaration's RUNTIME MIRROR (013 slice 1, ruling B) — a hand-authored C++ form of
// capability-manifest.toml [css] (the single source). The runtime consumers read THIS (element.style validation now;
// getComputedStyle's exposed-props later); the build-time linter reads the manifest TOML; a COHERENCE TEST binds
// them (mirror == manifest [css] AND every PRESENT name ⊆ litehtml's string_id.h parse names) — the no-drift guard
// that codegen would otherwise be. NO CathodeDom->litehtml runtime dependency: the mirror is a static neutral list.

namespace Cathode
{

// Per the manifest [css] status: PRESENT = litehtml parses + cascades it. The other two are Axis-3 IN but litehtml
// lacks the parse name (declared, never an undeclared drop):
//   BuildHowPending = resolves cascade-EMPTY until its build-how lands (opacity).
//   CathodeComputed = delivered Cathode-side TODAY -- getComputedStyle parses the element's OWN inline value
//                     (own-element-only; inheritance + rendering deferred to the litehtml build-how). A TRANSITIONAL
//                     partial that migrates to Present when litehtml gains the parse name (letter-spacing, 014 slice 1).
// All three are DECLARED (valid element.style names); only the unsupported (undeclared) are dropped.
enum class ECssPropertyStatus : uint8
{
	Present,
	BuildHowPending,
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
// accepted) iff it is in the mirror (Present OR either BuildHow). An undeclared name is silently dropped.
bool IsDeclaredCssProperty(const FString& Name);

} // namespace Cathode
