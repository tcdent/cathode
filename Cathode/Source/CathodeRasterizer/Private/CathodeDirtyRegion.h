#pragma once

#include "CoreMinimal.h"

#include "CathodeRaster.h" // EDirtyRegionGranularity

namespace Cathode
{

// Dirty-region tracking — entirely OUR logic, no vendor. Decides which sub-rectangles of a surface get
// redrawn so per-change cost stays small and idle surfaces are near-free. Marked rects are clamped to the
// surface bounds and coalesced according to the configured granularity:
//   - PerRect:       keep a minimal set, merging rects that overlap or touch (no gap) into their union;
//                    disjoint rects stay separate (least overdraw).
//   - BoundingUnion: keep a single rect = the union of everything marked (coarser, fewer uploads).
// The set is consumed via Flush() (after redraw + upload) and is empty afterwards.
//
// Pure + unit-tested directly (set/merge/clamp/empty-after-flush invariants), no pixels, no bitmap.
class FDirtyRegion
{
public:
	FDirtyRegion(int32 SurfaceWidth, int32 SurfaceHeight, EDirtyRegionGranularity InGranularity);

	/** Mark a rect dirty. Clamped to the surface; empty/out-of-bounds after clamp -> ignored. Coalesced
	 *  per the configured granularity. */
	void MarkDirty(const FIntRect& Rect);

	bool IsEmpty() const { return Rects.Num() == 0; }
	int32 Num() const { return Rects.Num(); }

	/** The current redraw set — the rects to repaint this frame. */
	const TArray<FIntRect>& GetRedrawRects() const { return Rects; }

	EDirtyRegionGranularity GetGranularity() const { return Granularity; }

	/** Consume the dirty set (after the redraw + upload). Empty afterwards. */
	void Flush() { Rects.Reset(); }

private:
	FIntRect SurfaceBounds; // [0,0 .. W,H), Max exclusive
	EDirtyRegionGranularity Granularity;
	TArray<FIntRect> Rects;
};

} // namespace Cathode
