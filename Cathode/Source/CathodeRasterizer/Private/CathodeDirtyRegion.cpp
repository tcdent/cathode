#include "CathodeDirtyRegion.h"

namespace Cathode
{

namespace
{

// Intersect a rect with the surface bounds. Returns an empty rect (Width/Height <= 0) if disjoint.
FIntRect ClampToBounds(const FIntRect& Rect, const FIntRect& Bounds)
{
	const int32 MinX = FMath::Max(Rect.Min.X, Bounds.Min.X);
	const int32 MinY = FMath::Max(Rect.Min.Y, Bounds.Min.Y);
	const int32 MaxX = FMath::Min(Rect.Max.X, Bounds.Max.X);
	const int32 MaxY = FMath::Min(Rect.Max.Y, Bounds.Max.Y);
	return FIntRect(MinX, MinY, MaxX, MaxY);
}

FIntRect UnionRects(const FIntRect& A, const FIntRect& B)
{
	return FIntRect(
		FMath::Min(A.Min.X, B.Min.X), FMath::Min(A.Min.Y, B.Min.Y),
		FMath::Max(A.Max.X, B.Max.X), FMath::Max(A.Max.Y, B.Max.Y));
}

// Two rects are connected (merge) when they are NOT separated by a gap on either axis — i.e. they
// overlap or share an edge (Max is exclusive, so A.Max.X == B.Min.X means they touch, not gap). Corner
// touching also connects; the resulting slight over-redraw is safe.
bool AreConnected(const FIntRect& A, const FIntRect& B)
{
	const bool bSeparatedX = A.Max.X < B.Min.X || B.Max.X < A.Min.X;
	const bool bSeparatedY = A.Max.Y < B.Min.Y || B.Max.Y < A.Min.Y;
	return !bSeparatedX && !bSeparatedY;
}

} // namespace

FDirtyRegion::FDirtyRegion(int32 SurfaceWidth, int32 SurfaceHeight, EDirtyRegionGranularity InGranularity)
	: SurfaceBounds(0, 0, FMath::Max(0, SurfaceWidth), FMath::Max(0, SurfaceHeight))
	, Granularity(InGranularity)
{
}

void FDirtyRegion::MarkDirty(const FIntRect& InRect)
{
	const FIntRect Rect = ClampToBounds(InRect, SurfaceBounds);
	if (Rect.Width() <= 0 || Rect.Height() <= 0)
	{
		return; // empty or fully outside the surface -> nothing to redraw
	}

	if (Granularity == EDirtyRegionGranularity::BoundingUnion)
	{
		// One rect = the running union of everything marked.
		if (Rects.Num() == 0)
		{
			Rects.Add(Rect);
		}
		else
		{
			Rects[0] = UnionRects(Rects[0], Rect);
		}
		return;
	}

	// PerRect: union the new rect with every connected existing rect, iterating until stable, so the set
	// stays minimal and has no two connected rects.
	FIntRect Merged = Rect;
	bool bMergedAny = true;
	while (bMergedAny)
	{
		bMergedAny = false;
		for (int32 Index = Rects.Num() - 1; Index >= 0; --Index)
		{
			if (AreConnected(Merged, Rects[Index]))
			{
				Merged = UnionRects(Merged, Rects[Index]);
				Rects.RemoveAt(Index);
				bMergedAny = true;
			}
		}
	}
	Rects.Add(Merged);
}

} // namespace Cathode
