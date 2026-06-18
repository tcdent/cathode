#include "CathodeCssCapability.h"

// THE MATERIALIZED MIRROR of capability-manifest.toml [css] (013 slice 1). SETTLED METHOD (browse-design re-ratified):
// the supported set is litehtml's OWN parse-name table string_id.h:163-295 MINUS 7 source-cited exclusions = 108 present
// (NOT a member-filter — that silently drops shorthands like flex-flow whose decomposition is in litehtml's non-vendored
// .cpp). The file-derived completeness test (CathodeDomCssCapabilityTests) asserts this == string_id.h:163-295 minus the
// exclusions, and cross-checks the 3 parse-but-not-computed exclusions stay member-less. Keep this lockstep with the
// manifest [css]. The ~14 nonstandard axis longhands are present at the keep-literal default pending tcdent's tail.

namespace Cathode
{

const TArray<FCssCapabilityEntry>& GetCssCapabilityMirror()
{
	static const TArray<FCssCapabilityEntry> Mirror =
	{
		// === 108 PRESENT — string_id.h:163-295 parse-names minus the 7 exclusions ===
		{ TEXT("background"),             ECssPropertyStatus::Present },
		{ TEXT("background-color"),       ECssPropertyStatus::Present },
		{ TEXT("background-image"),       ECssPropertyStatus::Present },
		{ TEXT("background-repeat"),      ECssPropertyStatus::Present },
		{ TEXT("background-origin"),      ECssPropertyStatus::Present },
		{ TEXT("background-clip"),        ECssPropertyStatus::Present },
		{ TEXT("background-attachment"),  ECssPropertyStatus::Present },
		{ TEXT("background-size"),        ECssPropertyStatus::Present },
		{ TEXT("background-position"),    ECssPropertyStatus::Present },
		{ TEXT("background-position-x"),  ECssPropertyStatus::Present },
		{ TEXT("background-position-y"),  ECssPropertyStatus::Present },
		{ TEXT("border"),                 ECssPropertyStatus::Present },
		{ TEXT("border-width"),           ECssPropertyStatus::Present },
		{ TEXT("border-style"),           ECssPropertyStatus::Present },
		{ TEXT("border-color"),           ECssPropertyStatus::Present },
		{ TEXT("border-spacing"),         ECssPropertyStatus::Present },
		{ TEXT("border-left"),            ECssPropertyStatus::Present },
		{ TEXT("border-right"),           ECssPropertyStatus::Present },
		{ TEXT("border-top"),             ECssPropertyStatus::Present },
		{ TEXT("border-bottom"),          ECssPropertyStatus::Present },
		{ TEXT("border-left-style"),      ECssPropertyStatus::Present },
		{ TEXT("border-right-style"),     ECssPropertyStatus::Present },
		{ TEXT("border-top-style"),       ECssPropertyStatus::Present },
		{ TEXT("border-bottom-style"),    ECssPropertyStatus::Present },
		{ TEXT("border-left-width"),      ECssPropertyStatus::Present },
		{ TEXT("border-right-width"),     ECssPropertyStatus::Present },
		{ TEXT("border-top-width"),       ECssPropertyStatus::Present },
		{ TEXT("border-bottom-width"),    ECssPropertyStatus::Present },
		{ TEXT("border-left-color"),      ECssPropertyStatus::Present },
		{ TEXT("border-right-color"),     ECssPropertyStatus::Present },
		{ TEXT("border-top-color"),       ECssPropertyStatus::Present },
		{ TEXT("border-bottom-color"),    ECssPropertyStatus::Present },
		{ TEXT("border-radius"),          ECssPropertyStatus::Present },
		{ TEXT("border-radius-x"),        ECssPropertyStatus::Present },
		{ TEXT("border-radius-y"),        ECssPropertyStatus::Present },
		{ TEXT("border-bottom-left-radius"),  ECssPropertyStatus::Present },
		{ TEXT("border-bottom-left-radius-x"),  ECssPropertyStatus::Present },
		{ TEXT("border-bottom-left-radius-y"),  ECssPropertyStatus::Present },
		{ TEXT("border-bottom-right-radius"),  ECssPropertyStatus::Present },
		{ TEXT("border-bottom-right-radius-x"),  ECssPropertyStatus::Present },
		{ TEXT("border-bottom-right-radius-y"),  ECssPropertyStatus::Present },
		{ TEXT("border-top-left-radius"),  ECssPropertyStatus::Present },
		{ TEXT("border-top-left-radius-x"),  ECssPropertyStatus::Present },
		{ TEXT("border-top-left-radius-y"),  ECssPropertyStatus::Present },
		{ TEXT("border-top-right-radius"),  ECssPropertyStatus::Present },
		{ TEXT("border-top-right-radius-x"),  ECssPropertyStatus::Present },
		{ TEXT("border-top-right-radius-y"),  ECssPropertyStatus::Present },
		{ TEXT("list-style"),             ECssPropertyStatus::Present },
		{ TEXT("list-style-type"),        ECssPropertyStatus::Present },
		{ TEXT("list-style-position"),    ECssPropertyStatus::Present },
		{ TEXT("list-style-image"),       ECssPropertyStatus::Present },
		{ TEXT("margin"),                 ECssPropertyStatus::Present },
		{ TEXT("margin-left"),            ECssPropertyStatus::Present },
		{ TEXT("margin-right"),           ECssPropertyStatus::Present },
		{ TEXT("margin-top"),             ECssPropertyStatus::Present },
		{ TEXT("margin-bottom"),          ECssPropertyStatus::Present },
		{ TEXT("padding"),                ECssPropertyStatus::Present },
		{ TEXT("padding-left"),           ECssPropertyStatus::Present },
		{ TEXT("padding-right"),          ECssPropertyStatus::Present },
		{ TEXT("padding-top"),            ECssPropertyStatus::Present },
		{ TEXT("padding-bottom"),         ECssPropertyStatus::Present },
		{ TEXT("font"),                   ECssPropertyStatus::Present },
		{ TEXT("font-family"),            ECssPropertyStatus::Present },
		{ TEXT("font-style"),             ECssPropertyStatus::Present },
		{ TEXT("font-weight"),            ECssPropertyStatus::Present },
		{ TEXT("font-size"),              ECssPropertyStatus::Present },
		{ TEXT("line-height"),            ECssPropertyStatus::Present },
		{ TEXT("text-decoration"),        ECssPropertyStatus::Present },
		{ TEXT("white-space"),            ECssPropertyStatus::Present },
		{ TEXT("text-align"),             ECssPropertyStatus::Present },
		{ TEXT("vertical-align"),         ECssPropertyStatus::Present },
		{ TEXT("color"),                  ECssPropertyStatus::Present },
		{ TEXT("width"),                  ECssPropertyStatus::Present },
		{ TEXT("height"),                 ECssPropertyStatus::Present },
		{ TEXT("min-width"),              ECssPropertyStatus::Present },
		{ TEXT("min-height"),             ECssPropertyStatus::Present },
		{ TEXT("max-width"),              ECssPropertyStatus::Present },
		{ TEXT("max-height"),             ECssPropertyStatus::Present },
		{ TEXT("position"),               ECssPropertyStatus::Present },
		{ TEXT("overflow"),               ECssPropertyStatus::Present },
		{ TEXT("display"),                ECssPropertyStatus::Present },
		{ TEXT("visibility"),             ECssPropertyStatus::Present },
		{ TEXT("box-sizing"),             ECssPropertyStatus::Present },
		{ TEXT("z-index"),                ECssPropertyStatus::Present },
		{ TEXT("float"),                  ECssPropertyStatus::Present },
		{ TEXT("clear"),                  ECssPropertyStatus::Present },
		{ TEXT("text-indent"),            ECssPropertyStatus::Present },
		{ TEXT("left"),                   ECssPropertyStatus::Present },
		{ TEXT("right"),                  ECssPropertyStatus::Present },
		{ TEXT("top"),                    ECssPropertyStatus::Present },
		{ TEXT("bottom"),                 ECssPropertyStatus::Present },
		{ TEXT("cursor"),                 ECssPropertyStatus::Present },
		{ TEXT("content"),                ECssPropertyStatus::Present },
		{ TEXT("border-collapse"),        ECssPropertyStatus::Present },
		{ TEXT("text-transform"),         ECssPropertyStatus::Present },
		{ TEXT("flex"),                   ECssPropertyStatus::Present },
		{ TEXT("flex-flow"),              ECssPropertyStatus::Present },
		{ TEXT("flex-direction"),         ECssPropertyStatus::Present },
		{ TEXT("flex-wrap"),              ECssPropertyStatus::Present },
		{ TEXT("justify-content"),        ECssPropertyStatus::Present },
		{ TEXT("align-items"),            ECssPropertyStatus::Present },
		{ TEXT("align-content"),          ECssPropertyStatus::Present },
		{ TEXT("align-self"),             ECssPropertyStatus::Present },
		{ TEXT("flex-grow"),              ECssPropertyStatus::Present },
		{ TEXT("flex-shrink"),            ECssPropertyStatus::Present },
		{ TEXT("flex-basis"),             ECssPropertyStatus::Present },
		{ TEXT("caption-side"),           ECssPropertyStatus::Present },
		{ TEXT("order"),                  ECssPropertyStatus::Present },
		// === build-how (Axis-3 IN, not litehtml parse names; cascade-EMPTY until the build-how lands) ===
		{ TEXT("opacity"),                ECssPropertyStatus::BuildHowPending },
		{ TEXT("letter-spacing"),         ECssPropertyStatus::BuildHowPending },
	};
	return Mirror;
}

bool IsDeclaredCssProperty(const FString& Name)
{
	for (const FCssCapabilityEntry& Entry : GetCssCapabilityMirror())
	{
		if (Name.Equals(Entry.Name, ESearchCase::CaseSensitive)) // CSS property names are lowercase + case-sensitive
		{
			return true; // Present OR either BuildHow -> DECLARED (element.style accepts the write)
		}
	}
	return false; // undeclared -> the accept-and-ignore gate drops the write
}

} // namespace Cathode
