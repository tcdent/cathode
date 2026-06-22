#include "CathodeShaperTextMeasurer.h"

namespace Cathode
{

FShaperTextMeasurer::FShaperTextMeasurer(const FString& FontFilePath)
	: Registry(CreateFontRegistry())
{
	if (Registry.IsValid())
	{
		DefaultFont = Registry->RegisterFontFile(FontFilePath);
		Shaper = CreateHarfBuzzShaper(*Registry);
	}
}

FLayoutFontRef FShaperTextMeasurer::ResolveFont(const FString& /*Face*/, int32 PixelSize, int32 /*Weight*/, bool /*bItalic*/)
{
	// Slice 1: a single injected default font for any face (the catalog is deferred — no text rendered yet).
	// The PixelSize is respected so the measured metrics scale with the requested size.
	FResolvedFont Entry;
	Entry.Handle = DefaultFont;
	Entry.PixelSize = static_cast<float>(PixelSize);
	const int32 Index = Resolved.Add(Entry);
	return FLayoutFontRef{ Index };
}

float FShaperTextMeasurer::MeasureWidth(const FString& Text, FLayoutFontRef Font)
{
	const FResolvedFont* R = Lookup(Font);
	if (R == nullptr || !R->Handle.IsValid() || !Shaper.IsValid())
	{
		return 0.0f;
	}
	FShapeRequest Request;
	Request.Text = Text;
	Request.Font = R->Handle;
	Request.FontSizePixels = R->PixelSize;
	const FShapedRun Shaped = Shaper->Shape(Request);
	return GetMetrics(Shaped).Width;
}

void FShaperTextMeasurer::GetFontMetrics(FLayoutFontRef Font, float& OutAscent, float& OutDescent, float& OutHeight)
{
	OutAscent = 0.0f;
	OutDescent = 0.0f;
	OutHeight = 0.0f;
	const FResolvedFont* R = Lookup(Font);
	if (R == nullptr || !R->Handle.IsValid() || !Shaper.IsValid())
	{
		return;
	}
	// The font's horizontal extents (ascent/descent/line-gap) come off any shaped run — probe with a space.
	FShapeRequest Request;
	Request.Text = TEXT(" ");
	Request.Font = R->Handle;
	Request.FontSizePixels = R->PixelSize;
	const FShapedRun Shaped = Shaper->Shape(Request);
	const FTextMetrics& M = GetMetrics(Shaped);
	OutAscent = M.Ascent;
	OutDescent = M.Descent;
	OutHeight = M.Ascent + M.Descent + M.LineGap;
}

bool FShaperTextMeasurer::ShapeText(FLayoutFontRef Font, const FString& Text, FShapedRun& OutShaped, float& OutFontSizePx)
{
	const FResolvedFont* R = Lookup(Font);
	if (R == nullptr || !R->Handle.IsValid() || !Shaper.IsValid())
	{
		return false;
	}
	FShapeRequest Request;
	Request.Text = Text;
	Request.Font = R->Handle;          // the SAME handle Layout measured with (the same-bytes anchor)
	Request.FontSizePixels = R->PixelSize;
	OutShaped = Shaper->Shape(Request);
	OutFontSizePx = R->PixelSize;
	return true;
}

bool FShaperTextMeasurer::GetFontBytes(FFontHandle Handle, TArrayView<const uint8>& OutBytes) const
{
	return Registry.IsValid() && Registry->GetFontBytes(Handle, OutBytes);
}

const FShaperTextMeasurer::FResolvedFont* FShaperTextMeasurer::Lookup(FLayoutFontRef Font) const
{
	return Resolved.IsValidIndex(Font.Id) ? &Resolved[Font.Id] : nullptr;
}

} // namespace Cathode
