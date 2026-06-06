#include "CathodeShapeCache.h"

namespace Cathode
{

FShapeKey MakeShapeKey(const FShapeRequest& Request)
{
	FShapeKey Key;
	Key.Text = Request.Text;
	Key.FontId = Request.Font.Id;
	Key.Size = Request.FontSizePixels;
	Key.FeatureBits =
		(Request.Features.bKerning ? 0x1 : 0)
		| (Request.Features.bLigatures ? 0x2 : 0)
		| (Request.Features.bContextualAlternates ? 0x4 : 0);
	return Key;
}

FCachingShaper::FCachingShaper(TUniquePtr<ITextShaper> InInner, int32 InMaxEntries)
	: Inner(MoveTemp(InInner))
	, MaxEntries(InMaxEntries)
{
}

FShapedRun FCachingShaper::Shape(const FShapeRequest& Request)
{
	const FShapeKey Key = MakeShapeKey(Request);

	if (const FShapedRun* Found = Cache.Find(Key))
	{
		return *Found; // cache hit -> the same shaped result, NO re-shape
	}

	FShapedRun Result = Inner.IsValid() ? Inner->Shape(Request) : FShapedRun{};

	Cache.Add(Key, Result);
	InsertionOrder.Add(Key);
	if (MaxEntries > 0 && InsertionOrder.Num() > MaxEntries)
	{
		const FShapeKey Evicted = InsertionOrder[0];
		InsertionOrder.RemoveAt(0);
		Cache.Remove(Evicted);
	}

	return Result;
}

} // namespace Cathode
