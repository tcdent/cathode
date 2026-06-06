#pragma once

#include "CoreMinimal.h"

#include "CathodeDirtyRegion.h"
#include "CathodeDoubleBuffer.h"
#include "CathodeRaster.h" // IRasterBackend, FPaintOp, FRasterConfig, FFrameReadySeam

namespace Cathode
{

// The live producer path for one surface — the Rasterizer's rasterize-a-frame entry. It owns the
// double-buffer and the dirty-region tracker for the surface, and threads the FRasterConfig through the
// whole path. RasterizeFrame: begin a draw session on the BACK buffer (config applied at begin) -> route
// the input ops through the SAME DispatchPaintOps entry (no fork) -> mark the dirty region from the ops
// -> swap-if-changed -> on a real change, PRODUCE the front-buffer bitmap + dirty rects and hand them off
// through the output seam. Frame-ready fires only on change; idle frames produce nothing.
//
// Bespoke + testable via a recording backend (records the dispatch + supplies bitmap data) and a recording
// output seam — never pixels.
class FSurfaceRasterizer
{
public:
	FSurfaceRasterizer(IRasterBackend& InBackend, int32 Width, int32 Height, const FRasterConfig& InConfig);

	bool IsValid() const { return DoubleBuffer.IsValid(); }

	/** Wire the output seam future Surfaces / Texture Egress consume (the produced bitmap + dirty rects). */
	void SetFrameReadySeam(FFrameReadySeam Seam) { OnFrameReady = MoveTemp(Seam); }

	/** Rasterize one frame from the input ops. Returns true iff a frame was produced + handed off (a real
	 *  change occurred). Routes through the SAME DispatchPaintOps as Slice 4 — no re-implementation. */
	bool RasterizeFrame(const TArray<FPaintOp>& Ops);

	const FRasterConfig& GetConfig() const { return Config; }
	int32 GetWidth() const { return DoubleBuffer.GetWidth(); }
	int32 GetHeight() const { return DoubleBuffer.GetHeight(); }

private:
	IRasterBackend& Backend;
	FRasterConfig Config;
	FDoubleBuffer DoubleBuffer;
	FDirtyRegion DirtyRegion;
	FFrameReadySeam OnFrameReady;
};

} // namespace Cathode
