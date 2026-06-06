#include "CathodeFontRegistry.h"

#include "Misc/FileHelper.h"

namespace Cathode
{

FFontRegistry::FFontRegistry(FFontFileLoader InLoader)
	: Loader(MoveTemp(InLoader))
{
}

FFontHandle FFontRegistry::RegisterFontFile(const FString& Path)
{
	// Same path -> same handle, with NO reload (single-load).
	if (const int32* Existing = PathToHandle.Find(Path))
	{
		return FFontHandle{ *Existing };
	}

	TArray<uint8> Bytes;
	if (!Loader || !Loader(Path, Bytes))
	{
		return FFontHandle{}; // invalid (INDEX_NONE) on load failure
	}

	const int32 NewId = FontBytes.Num();
	FontBytes.Add(MakeUnique<TArray<uint8>>(MoveTemp(Bytes)));
	PathToHandle.Add(Path, NewId);
	return FFontHandle{ NewId };
}

bool FFontRegistry::GetFontBytes(FFontHandle Handle, TArrayView<const uint8>& OutBytes) const
{
	if (!Handle.IsValid() || !FontBytes.IsValidIndex(Handle.Id) || !FontBytes[Handle.Id].IsValid())
	{
		return false;
	}
	OutBytes = TArrayView<const uint8>(*FontBytes[Handle.Id]);
	return true;
}

TUniquePtr<IFontRegistry> CreateFontRegistry()
{
	// Production loader: read the font file once from the local file system / UE file APIs.
	return MakeUnique<FFontRegistry>([](const FString& Path, TArray<uint8>& OutBytes)
	{
		return FFileHelper::LoadFileToArray(OutBytes, *Path);
	});
}

} // namespace Cathode
