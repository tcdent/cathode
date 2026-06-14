#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

#include "CathodeText.h" // ITextShaper, FShapeRequest, FShapedRun

namespace Cathode
{

// The shape-plan/result cache key: text + font + size + the feature toggles. Two requests with the same
// key produce the same shaped result, so the second is a cache hit (no re-shape).
struct FShapeKey
{
	FString Text;
	int32 FontId = INDEX_NONE;
	float Size = 0.0f;
	uint8 FeatureBits = 0; // bit0 kern, bit1 liga, bit2 calt

	bool operator==(const FShapeKey& Other) const
	{
		return FontId == Other.FontId && Size == Other.Size && FeatureBits == Other.FeatureBits && Text == Other.Text;
	}
};

inline uint32 GetTypeHash(const FShapeKey& Key)
{
	// Unqualified so ADL finds FString's GetTypeHash (a hidden friend in modern UE — invisible to
	// qualified ::GetTypeHash lookup). The FShapeKey overload can't match an FString arg, so no recursion.
	uint32 Hash = GetTypeHash(Key.Text);
	Hash = HashCombine(Hash, ::GetTypeHash(Key.FontId));
	Hash = HashCombine(Hash, ::GetTypeHash(Key.Size));
	Hash = HashCombine(Hash, ::GetTypeHash(Key.FeatureBits));
	return Hash;
}

FShapeKey MakeShapeKey(const FShapeRequest& Request);

// A shaper that caches results by key — the single shaped result both consumers reuse. A repeat
// same-key request returns the cached result WITHOUT calling the inner shaper (no re-shape). Cache
// policy is a sensible fixed default (bounded, FIFO eviction) — no tuning surface (human-ruled).
class FCachingShaper final : public ITextShaper
{
public:
	static constexpr int32 DefaultMaxEntries = 256;

	explicit FCachingShaper(TUniquePtr<ITextShaper> InInner, int32 InMaxEntries = DefaultMaxEntries);

	virtual FShapedRun Shape(const FShapeRequest& Request) override;

	int32 NumCached() const { return Cache.Num(); }

private:
	TUniquePtr<ITextShaper> Inner;
	int32 MaxEntries;
	TMap<FShapeKey, FShapedRun> Cache;
	TArray<FShapeKey> InsertionOrder; // FIFO eviction order
};

} // namespace Cathode
