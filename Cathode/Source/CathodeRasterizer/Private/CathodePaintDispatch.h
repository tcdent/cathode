#pragma once

#include "CoreMinimal.h"

#include "CathodeRaster.h" // FPaintOp, IRasterContext, FRasterConfig

namespace Cathode
{

// Draw-dispatch — OUR logic, BL*-free. THE single production entry that routes a stream of input paint
// ops (the Layout/Text input seam) to a backend draw session (IRasterContext), one op -> one backend
// call, with our-computed params. There is exactly ONE definition: the real paint path calls it with the
// Blend2D-backed context; the specs call the SAME function with a recording context. No forked dispatch.
//
// Our-computed param of note: glyph origins are passed UNSNAPPED when bFractionalGlyphPositioning is set
// (the decided default) — never rounded to the pixel grid. Vendor rasterization (the fill_* calls behind
// IRasterContext) is not our concern here; this routes, the backend rasterizes.
void DispatchPaintOps(const TArray<FPaintOp>& Ops, IRasterContext& Context, const FRasterConfig& Config);

} // namespace Cathode
