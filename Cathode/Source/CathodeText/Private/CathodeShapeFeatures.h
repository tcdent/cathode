#pragma once

#include "CoreMinimal.h"

#include "CathodeText.h" // FTextFeatures

namespace Cathode
{

// Our neutral OpenType feature: a 4-char tag packed into a uint32 (same bit layout the shaping engine
// uses) + an on/off value. Engine-neutral — no hb_*. The shaper converts this to the vendor feature
// type at the thin edge.
struct FShapeFeature
{
	uint32 Tag = 0;
	uint32 Value = 0;
};

// Pack a 4-char OpenType tag into a uint32 (big-endian char order) — our own bit math, no hb_*.
inline uint32 MakeOpenTypeTag(char A, char B, char C, char D)
{
	return (static_cast<uint32>(static_cast<uint8>(A)) << 24)
		| (static_cast<uint32>(static_cast<uint8>(B)) << 16)
		| (static_cast<uint32>(static_cast<uint8>(C)) << 8)
		| static_cast<uint32>(static_cast<uint8>(D));
}

// Map per-run feature config -> the explicit feature set the shaper applies. Each feature is set
// explicitly on/off so the result is deterministic and testable: kern=0 (bKerning=false) is the
// retro-CRT raw look. OUR mapping, hb_*-free; tested.
TArray<FShapeFeature> BuildShapeFeatures(const FTextFeatures& Features);

} // namespace Cathode
