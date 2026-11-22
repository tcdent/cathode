#include "CathodeCssCapability.h"
#include "CathodeCssAllowListGenerated.h" // the BAKED engine-derived allow-list (litehtml string_id.h -7 +2; the allow-list codegen)

// The supported CSS set is the BAKED allow-list generated off litehtml by the allow-list codegen
// (CathodeCssAllowListGenerated). CathodeCssAllowListTests prove the baked set == litehtml -7 +2 (committed==regen + the
// +2/-7 guards). This file is the validation predicate only. NO runtime re-parse of string_id.h (absent at runtime) —
// the baked array is compiled-in. CathodeDomCssCapabilityTests check the accepted SET property-by-property: every
// litehtml -7 +2 name accepted, the 7 + a never-name rejected.

namespace Cathode
{

bool IsDeclaredCssProperty(const FString& Name)
{
	// CASE-SENSITIVE membership — an `Equals(..., ESearchCase::CaseSensitive)` scan.
	// (A TSet<FString>(...).Contains(Name) would silently FLIP to IgnoreCase — UE's FString TSet/operator== default.)
	// Static-cached: the baked array is materialized once, then an O(n) scan.
	static const TArray<FString> Allow = GetGeneratedCssAllowListNames();
	for (const FString& Declared : Allow)
	{
		if (Name.Equals(Declared, ESearchCase::CaseSensitive))
		{
			return true; // declared (in the baked engine-derived set) -> element.style accepts the write
		}
	}
	return false; // undeclared -> accept-and-ignore drops the write
}

} // namespace Cathode
