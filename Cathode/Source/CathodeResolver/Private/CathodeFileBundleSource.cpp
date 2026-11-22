#include "CathodeResolver.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace Cathode
{

namespace
{
// The PRODUCTION UE-asset read backend: a root-relative bundle path -> REAL file bytes via FFileHelper,
// rooted at a SUPPLIED (REQUIRED) RootDir. The FFontRegistry CreateFontRegistry->LoadFileToArray precedent
// (CathodeText/Private/CathodeFontRegistry.cpp:43-50 — the only production "read file bytes" code in Cathode). NO
// baked default-root: the configured root is the ctor input; the production default-root factory + the UFS-staging
// config + the cooked-pak read validation are the COUPLED integration item (COVERAGE.md Deferred #3/#5). Private to this TU;
// only the factory is exposed. Couples-to-none (Core/FFileHelper only — no vendor, no sibling).
class FUEFileBundleSource final : public IBundleSource
{
public:
	explicit FUEFileBundleSource(const FString& InRootDir)
		: RootDir(InRootDir)
	{
	}

	virtual bool Read(const FString& BundlePath, TArray<uint8>& OutBytes) const override
	{
		// RootDir/BundlePath -> the real file. LoadFileToArray returns false on a missing file; on a clean MISS we
		// leave OutBytes EMPTY (no partial-fill/garbage survives a miss — the hit/miss contract).
		const FString FullPath = FPaths::Combine(RootDir, BundlePath);
		if (!FFileHelper::LoadFileToArray(OutBytes, *FullPath))
		{
			OutBytes.Reset();
			return false;
		}
		return true;
	}

private:
	FString RootDir;
};
} // namespace

TUniquePtr<IBundleSource> CreateUEFileBundleSource(const FString& RootDir)
{
	return MakeUnique<FUEFileBundleSource>(RootDir);
}

} // namespace Cathode
