#include "CathodeSurfaceRasterizer.h"

#include "CathodePaintDispatch.h"

namespace Cathode
{

namespace
{

// Integer pixel bounds covering a float paint rect (floor min, ceil max) — our-computed dirty bounds.
FIntRect ToIntRect(const FBox2f& Box)
{
	return FIntRect(
		FMath::FloorToInt(Box.Min.X), FMath::FloorToInt(Box.Min.Y),
		FMath::CeilToInt(Box.Max.X), FMath::CeilToInt(Box.Max.Y));
}

FBox2f ToBox2f(const FIntRect& Rect)
{
	return FBox2f(
		FVector2f(static_cast<float>(Rect.Min.X), static_cast<float>(Rect.Min.Y)),
		FVector2f(static_cast<float>(Rect.Max.X), static_cast<float>(Rect.Max.Y)));
}

} // namespace

FSurfaceRasterizer::FSurfaceRasterizer(IRasterBackend& InBackend, int32 Width, int32 Height, const FRasterConfig& InConfig)
	: Backend(InBackend)
	, Config(InConfig)
	, DoubleBuffer(InBackend, Width, Height, DefaultSurfaceFormat)             // decided sRGB BGRA8 surface format
	, DirtyRegion(Width, Height, InConfig.DirtyRegionGranularity)             // config-driven granularity
{
}

bool FSurfaceRasterizer::RasterizeFrame(const TArray<FPaintOp>& Ops)
{
	// Derive the dirty region from the ops' bounds (what is painted = what changed). NOTE: glyph ops carry
	// no bounds yet (Text & Fonts supplies glyph extents) — accountable gap #2 in CathodeRasterizer/COVERAGE.md.
	for (const FPaintOp& Op : Ops)
	{
		DirtyRegion.MarkDirty(ToIntRect(Op.Rect));
	}

	if (DirtyRegion.IsEmpty())
	{
		return false; // nothing dirty -> no re-rasterize, no frame-ready (idle surfaces cost nothing)
	}

	IRasterBitmap* const Back = DoubleBuffer.GetBackBuffer();
	if (Back == nullptr)
	{
		return false;
	}

	// Config applied at context creation (AA/quality/thread_count); see FBlend2DContext::Begin.
	TUniquePtr<IRasterContext> Context = Backend.BeginContext(*Back, Config);
	if (Context.IsValid())
	{
		// RASTERIZE DIRTY REGIONS ONLY: clip to each dirty rect and redraw the ops within it, so only the
		// changed sub-rectangles are re-rasterized — never the whole surface. Routes through the SAME
		// DispatchPaintOps as Slice 4 (no fork).
		for (const FIntRect& Dirty : DirtyRegion.GetRedrawRects())
		{
			Context->ClipToRect(ToBox2f(Dirty));
			DispatchPaintOps(Ops, *Context, Config);
			Context->ResetClip();
		}
		Context->End();
	}

	DoubleBuffer.MarkChanged();
	if (!DoubleBuffer.SwapIfChanged())
	{
		return false; // no real change published -> no frame-ready
	}

	// OUTPUT SEAM: produce the published front-buffer bitmap + dirty rects and hand them off to (future)
	// Surfaces / Texture Egress.
	if (OnFrameReady)
	{
		IRasterBitmap* const Front = DoubleBuffer.GetFrontBuffer();
		FBitmapData Data;
		if (Front != nullptr && Front->GetData(Data))
		{
			OnFrameReady(Data, DirtyRegion.GetRedrawRects());
		}
	}

	DirtyRegion.Flush(); // consumed
	return true;
}

// The public producer factory (sprint 023) — the single out-of-module construction point. The concrete
// FSurfaceRasterizer (+ its FDoubleBuffer/FDirtyRegion) stay Private; the composition root gets only the
// ISurfaceProducer behavior, the IRasterBackend/CreateBlend2DBackend idiom one layer up.
TUniquePtr<ISurfaceProducer> CreateSurfaceRasterizer(IRasterBackend& Backend, int32 Width, int32 Height, const FRasterConfig& Config)
{
	return MakeUnique<FSurfaceRasterizer>(Backend, Width, Height, Config);
}

} // namespace Cathode
