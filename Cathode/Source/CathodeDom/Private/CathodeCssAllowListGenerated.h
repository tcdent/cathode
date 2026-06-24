#pragma once

#include "CoreMinimal.h"

namespace Cathode
{

// 025 S2 — the baked-in CSS allow-list accessor. The DEFINITION (+ the GCssAllowList[] array) is GENERATED into
// CathodeCssAllowListGenerated.cpp by the regenerate-mode test (litehtml string_id.h -7 audited exclusions +2
// cathode_computed; coherence-guarded). string_id.h is NOT on disk at runtime, so the runtime CSS-property validation
// (IsDeclaredCssProperty — wired to this in S3) reads THIS baked set instead of re-enumerating the engine header. This
// .h is HAND-authored + stable; only the .cpp is regenerated.
TArray<FString> GetGeneratedCssAllowListNames();

} // namespace Cathode
