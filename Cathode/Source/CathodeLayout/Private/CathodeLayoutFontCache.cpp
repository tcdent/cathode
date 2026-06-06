#include "CathodeLayoutFontCache.h"

namespace Cathode
{

FLayoutFontCache::FLayoutFontCache(ITextMeasurer& InMeasurer)
	: Measurer(InMeasurer)
{
}

uint64 FLayoutFontCache::ResolveFont(const FString& Face, int32 PixelSize, int32 Weight, bool bItalic)
{
	const FString Key = FString::Printf(TEXT("%s|%d|%d|%d"), *Face, PixelSize, Weight, bItalic ? 1 : 0);

	if (const uint64* Existing = KeyToHandle.Find(Key))
	{
		return *Existing; // dedup -> same handle, no second ResolveFont
	}

	const FLayoutFontRef Ref = Measurer.ResolveFont(Face, PixelSize, Weight, bItalic);
	Refs.Add(Ref);
	const uint64 Handle = static_cast<uint64>(Refs.Num()); // handle = index + 1 (0 = invalid)
	KeyToHandle.Add(Key, Handle);
	return Handle;
}

void FLayoutFontCache::ReleaseFont(uint64 /*Handle*/)
{
	// The cache retains resolved fonts at this altitude; nothing to release.
}

float FLayoutFontCache::MeasureWidth(uint64 Handle, const FString& Text)
{
	return Measurer.MeasureWidth(Text, GetRef(Handle));
}

void FLayoutFontCache::GetFontMetrics(uint64 Handle, float& OutAscent, float& OutDescent, float& OutHeight)
{
	Measurer.GetFontMetrics(GetRef(Handle), OutAscent, OutDescent, OutHeight);
}

FLayoutFontRef FLayoutFontCache::GetRef(uint64 Handle) const
{
	const int32 Index = static_cast<int32>(Handle) - 1;
	return Refs.IsValidIndex(Index) ? Refs[Index] : FLayoutFontRef{};
}

} // namespace Cathode
