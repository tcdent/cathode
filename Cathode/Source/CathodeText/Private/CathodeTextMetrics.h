#pragma once

#include "CoreMinimal.h"

#include "CathodeText.h" // FShapedGlyph, FTextMetrics

namespace Cathode
{

// Run metrics — OUR derivation. Width is the SUM of the assembled glyph advances (our computation). The
// vertical extents (ascent/descent/line-gap) are the font's horizontal extents READ in the shaper
// (vendor) and SCALED to px (our scale); they pass through here already-scaled. hb_*-free; tested.
FTextMetrics DeriveMetrics(const TArray<FShapedGlyph>& Glyphs, float Ascent, float Descent, float LineGap);

// Run bounding box — OUR bbox computation over the cumulative glyph positions + advances and the
// vertical extents. This produces the run BOUNDS so a glyph-only frame can dirty (closes the deferral #2
// dependency in CathodeRasterizer/COVERAGE.md). Baseline at Y=0: ascent above (negative Y), descent below.
FBox2f DeriveExtents(const TArray<FShapedGlyph>& Glyphs, float Ascent, float Descent);

} // namespace Cathode
