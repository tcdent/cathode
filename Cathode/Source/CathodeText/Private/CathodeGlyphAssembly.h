#pragma once

#include "CoreMinimal.h"

#include "CathodeText.h" // FShapedGlyph

namespace Cathode
{

// The raw shaper output, in our neutral types (plain ints) — read out of the engine in the shaper .cpp,
// then handed to AssembleGlyphs. No hb_*; the engine-reading is the vendor edge, this is just numbers.
struct FRawGlyph
{
	uint32 GlyphId = 0;
	int32 Cluster = 0;
	int32 XAdvance = 0; // engine fixed-point advance/offset
	int32 YAdvance = 0;
	int32 XOffset = 0;
	int32 YOffset = 0;
};

// OUR glyph-assembly math (hb_*-free, testable on plain numbers): scale the raw fixed-point advances/
// offsets to pixels (PixelsPerUnit) and compute the CUMULATIVE draw position per glyph (running pen +
// this glyph's offset). The raw values are opaque INPUT (the shaper's vendor output); we test the
// scaling + accumulation, not the raw values.
TArray<FShapedGlyph> AssembleGlyphs(const TArray<FRawGlyph>& Raw, float PixelsPerUnit);

} // namespace Cathode
