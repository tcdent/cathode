#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

#include "CathodeLayout.h" // ITextMeasurer, FLayoutFontRef
#include "CathodeText.h"   // IFontRegistry, ITextShaper, FFontHandle

namespace Cathode
{

// The real ITextMeasurer (CathodeLayout's text-measurement seam) backed by CathodeText's HarfBuzz shaper +
// font registry. This is the composition root's text-measurement glue — it deps BOTH Layout and Text, so it
// lives HERE (not in either leaf engine). It is the Layout-CONSTRUCTION prerequisite: CreateLiteHtmlLayoutEngine
// requires an ITextMeasurer&, even to lay out a no-text document.
//
// ResolveFont/GetFontMetrics let Layout construct + set up the default font; MeasureWidth shapes real text.
// A SINGLE injected default font backs every ResolveFont — there is NO face-name -> font CATALOG (the Face,
// Weight and italic arguments are ignored); the PixelSize IS respected so metrics scale.
class FShaperTextMeasurer final : public ITextMeasurer
{
public:
	explicit FShaperTextMeasurer(const FString& FontFilePath);

	bool IsValid() const { return DefaultFont.IsValid() && Shaper.IsValid(); }

	virtual FLayoutFontRef ResolveFont(const FString& Face, int32 PixelSize, int32 Weight, bool bItalic) override;
	virtual float MeasureWidth(const FString& Text, FLayoutFontRef Font) override;
	virtual void GetFontMetrics(FLayoutFontRef Font, float& OutAscent, float& OutDescent, float& OutHeight) override;

	// The raster-side shaping service — the SAME-BYTES ANCHOR. This adapter is SHARED between the
	// document backer (Layout measures via ITextMeasurer) and the render-target backer (the mapper shapes for the
	// raster), so the FLayoutFontRef Layout resolved maps to the SAME FFontHandle + bytes the shaper shapes and
	// the Rasterizer draws — one font end-to-end. ShapeText resolves the ref + shapes via the same HarfBuzz shaper
	// Layout measures with; GetFontBytes hands the registry's bytes for the FPositionedGlyphRun. (ShapeText is
	// non-const: the shaper mutates its plan cache.)
	bool ShapeText(FLayoutFontRef Font, const FString& Text, FShapedRun& OutShaped, float& OutFontSizePx);
	bool GetFontBytes(FFontHandle Handle, TArrayView<const uint8>& OutBytes) const;

private:
	struct FResolvedFont
	{
		FFontHandle Handle;
		float PixelSize = 0.0f;
	};
	const FResolvedFont* Lookup(FLayoutFontRef Font) const;

	TUniquePtr<IFontRegistry> Registry;
	TUniquePtr<ITextShaper> Shaper;
	FFontHandle DefaultFont;
	TArray<FResolvedFont> Resolved; // FLayoutFontRef.Id indexes this
};

} // namespace Cathode
