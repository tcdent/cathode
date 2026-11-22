#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDirtyRegion.h"
#include "CathodeRaster.h"

#include "Misc/AutomationTest.h"

// Specs for the Rasterizer's dirty-region tracking — entirely OUR logic, no vendor, no pixels. They drive
// the real FDirtyRegion and assert the set/merge/clamp/empty-after-flush invariants directly (design
// invariants).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRasterizerDirtyCoalesceTest,
	"Cathode.Rasterizer.Dirty.CoalesceClampFlush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRasterizerDirtyCoalesceTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// Starts empty.
	{
		FDirtyRegion Region(100, 100, EDirtyRegionGranularity::PerRect);
		TestTrue(TEXT("a fresh region is empty"), Region.IsEmpty());
	}

	// Overlapping rects coalesce to their union.
	{
		FDirtyRegion Region(100, 100, EDirtyRegionGranularity::PerRect);
		Region.MarkDirty(FIntRect(10, 10, 20, 20));
		Region.MarkDirty(FIntRect(15, 15, 25, 25)); // overlaps the first
		TestEqual(TEXT("overlapping rects coalesce to one"), Region.Num(), 1);
		if (Region.Num() == 1)
		{
			const FIntRect R = Region.GetRedrawRects()[0];
			TestTrue(TEXT("the coalesced rect is the union (10,10..25,25)"),
				R == FIntRect(10, 10, 25, 25));
		}
	}

	// Edge-adjacent rects (sharing an edge, no gap) coalesce.
	{
		FDirtyRegion Region(100, 100, EDirtyRegionGranularity::PerRect);
		Region.MarkDirty(FIntRect(0, 0, 10, 10));
		Region.MarkDirty(FIntRect(10, 0, 20, 10)); // shares the x=10 edge
		TestEqual(TEXT("edge-adjacent rects coalesce to one"), Region.Num(), 1);
		if (Region.Num() == 1)
		{
			TestTrue(TEXT("the coalesced rect spans both (0,0..20,10)"),
				Region.GetRedrawRects()[0] == FIntRect(0, 0, 20, 10));
		}
	}

	// Disjoint rects with a gap stay separate.
	{
		FDirtyRegion Region(100, 100, EDirtyRegionGranularity::PerRect);
		Region.MarkDirty(FIntRect(0, 0, 10, 10));
		Region.MarkDirty(FIntRect(50, 50, 60, 60)); // gap between them
		TestEqual(TEXT("disjoint rects stay separate"), Region.Num(), 2);
	}

	// A rect straddling the surface edge is clamped to the surface.
	{
		FDirtyRegion Region(100, 100, EDirtyRegionGranularity::PerRect);
		Region.MarkDirty(FIntRect(90, 90, 120, 120));
		TestEqual(TEXT("a straddling rect is kept (clamped)"), Region.Num(), 1);
		if (Region.Num() == 1)
		{
			TestTrue(TEXT("the rect is clamped to the surface (90,90..100,100)"),
				Region.GetRedrawRects()[0] == FIntRect(90, 90, 100, 100));
		}
	}

	// A rect fully outside the surface is ignored.
	{
		FDirtyRegion Region(100, 100, EDirtyRegionGranularity::PerRect);
		Region.MarkDirty(FIntRect(200, 200, 210, 210));
		TestTrue(TEXT("a fully-outside rect is ignored"), Region.IsEmpty());
	}

	// A zero-area rect is ignored.
	{
		FDirtyRegion Region(100, 100, EDirtyRegionGranularity::PerRect);
		Region.MarkDirty(FIntRect(5, 5, 5, 5));
		TestTrue(TEXT("a zero-area rect is ignored"), Region.IsEmpty());
	}

	// Flush consumes the set -> empty afterwards.
	{
		FDirtyRegion Region(100, 100, EDirtyRegionGranularity::PerRect);
		Region.MarkDirty(FIntRect(10, 10, 20, 20));
		TestFalse(TEXT("non-empty after a mark"), Region.IsEmpty());
		Region.Flush();
		TestTrue(TEXT("empty after flush"), Region.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRasterizerDirtyBoundingUnionTest,
	"Cathode.Rasterizer.Dirty.BoundingUnionGranularity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRasterizerDirtyBoundingUnionTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// BoundingUnion granularity keeps a single rect = the union of everything marked, even when disjoint.
	FDirtyRegion Region(100, 100, EDirtyRegionGranularity::BoundingUnion);
	Region.MarkDirty(FIntRect(0, 0, 10, 10));
	Region.MarkDirty(FIntRect(50, 50, 60, 60)); // disjoint from the first
	Region.MarkDirty(FIntRect(20, 80, 30, 90));

	TestEqual(TEXT("BoundingUnion keeps exactly one rect"), Region.Num(), 1);
	if (Region.Num() == 1)
	{
		TestTrue(TEXT("the single rect is the bounding union of all marks (0,0..60,90)"),
			Region.GetRedrawRects()[0] == FIntRect(0, 0, 60, 90));
	}
	TestTrue(TEXT("granularity reflects the configured value"),
		Region.GetGranularity() == EDirtyRegionGranularity::BoundingUnion);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
