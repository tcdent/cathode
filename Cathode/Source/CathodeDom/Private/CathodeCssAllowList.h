#pragma once

#include "CoreMinimal.h"

namespace Cathode
{

// 025 S2 — the CSS allow-list codegen (Model A, tcdent FORK-1): the supported CSS-property set is PROJECTED from the
// ENGINE source — litehtml's string_id.h parse-names (ParseLitehtmlPropertyNames) MINUS the 7 audited exclusions PLUS
// the 2 cathode_computed (opacity, letter-spacing). NOT from capability-manifest.toml (the retired Model-B copy). A
// litehtml string_id.h add/remove flows through. The codegen emits BOTH css-allow-list.toml (the build/linter artifact)
// AND the runtime-set .cpp (a baked-in array — string_id.h is not on disk at runtime; S3 wires the runtime consumer to it).
//
// Build-time tooling (run by the regenerate-mode test): it reads string_id.h from the plugin ThirdParty dir by PATH
// (IPluginManager), so it lives behind WITH_DEV_AUTOMATION_TESTS (the shipped runtime never re-enumerates — it reads the
// committed runtime-set .cpp). The coherence + guard tests re-run these to assert the committed artifacts == the projection.
#if WITH_DEV_AUTOMATION_TESTS

// Load litehtml's string_id.h text from the plugin ThirdParty dir (Source/ThirdParty/litehtml/include/litehtml/string_id.h).
// Returns false if the plugin dir / file can't be resolved.
bool LoadLitehtmlStringIdH(FString& OutContents);

// litehtml's CSS property NAMES from string_id.h's `// CSS property names` block (`_flex_direction_,` -> "flex-direction").
void ParseLitehtmlPropertyNames(const FString& StringIdH, TSet<FString>& Out);

// The 7 audited exclusions (hand-curated; each MUST be a real litehtml parse-name — guarded). Migrated from the test TU.
const TArray<FString>& CssAuditedExclusions();

// The 2 cathode_computed props (opacity, letter-spacing) — Axis-3-IN but NOT litehtml parse-names; the named +2 exception.
const TArray<FString>& CssCathodeComputed();

// THE ALLOW-LIST SET = ParseLitehtmlPropertyNames(string_id.h) - CssAuditedExclusions + CssCathodeComputed. Sorted.
void BuildCssAllowListSet(const FString& StringIdH, TArray<FString>& OutSorted);

// The two generated artifacts (Model A): the .toml (build/linter) + the runtime-set .cpp (baked-in array for S3).
FString GenerateCssAllowListToml();   // reads string_id.h, projects the set -> css-allow-list.toml text
FString GenerateCssAllowListCpp();    // reads string_id.h, projects the set -> the runtime-set .cpp text (GCssAllowList[] + accessor)

#endif // WITH_DEV_AUTOMATION_TESTS

} // namespace Cathode
