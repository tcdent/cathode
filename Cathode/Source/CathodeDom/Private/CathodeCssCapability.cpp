#include "CathodeCssCapability.h"
#include "CathodeCssAllowListGenerated.h" // the BAKED engine-derived allow-list (litehtml string_id.h -7 +2; S2 codegen)

// 025 S3: the hand-authored runtime MIRROR (GetCssCapabilityMirror + the 108+2 literal table + the ECssPropertyStatus/
// FCssCapabilityEntry types) is RETIRED. The supported CSS set is now the BAKED allow-list generated off litehtml by the
// S2 codegen (CathodeCssAllowListGenerated). S2's CathodeCssAllowListTests prove the baked set == litehtml -7 +2
// (committed==regen + the +2/-7 guards) — that engine-coherence SUBSUMES the retired mirror==litehtml clause. This file's
// job shrinks to the validation predicate. NO runtime re-parse of string_id.h (absent at runtime) — the baked array is
// compiled-in (carry, not rediscover). The accepted SET is byte-identical to the retired mirror (proven property-by-property
// in CathodeDomCssCapabilityTests' behavior-preservation test: every litehtml -7 +2 name accepted, the 7 + a never-name rejected).

namespace Cathode
{

bool IsDeclaredCssProperty(const FString& Name)
{
	// CASE-SENSITIVE membership — byte-identical to the retired mirror's `Equals(..., ESearchCase::CaseSensitive)` scan.
	// (A TSet<FString>(...).Contains(Name) would silently FLIP to IgnoreCase — UE's FString TSet/operator== default.)
	// Static-cached: the baked array is materialized once, same O(n) scan the mirror had.
	static const TArray<FString> Allow = GetGeneratedCssAllowListNames();
	for (const FString& Declared : Allow)
	{
		if (Name.Equals(Declared, ESearchCase::CaseSensitive))
		{
			return true; // declared (in the baked engine-derived set) -> element.style accepts the write
		}
	}
	return false; // undeclared -> the accept-and-ignore gate drops the write
}

} // namespace Cathode
