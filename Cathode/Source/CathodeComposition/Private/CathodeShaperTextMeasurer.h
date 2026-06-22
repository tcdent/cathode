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
// SLICE 1: WIRED, not text-exercised. ResolveFont/GetFontMetrics let Layout construct + set up the default
// font; MeasureWidth shapes real text but is not exercised by the non-text proof (its discriminating proof is
// slice 2). A SINGLE injected default font backs every ResolveFont — the face-name -> font CATALOG is deferred
// (no text is rendered yet); the PixelSize IS respected so metrics scale.
class FShaperTextMeasurer final : public ITextMeasurer
{
public:
	explicit FShaperTextMeasurer(const FString& FontFilePath);

	bool IsValid() const { return DefaultFont.IsValid() && Shaper.IsValid(); }

	virtual FLayoutFontRef ResolveFont(const FString& Face, int32 PixelSize, int32 Weight, bool bItalic) override;
	virtual float MeasureWidth(const FString& Text, FLayoutFontRef Font) override;
	virtual void GetFontMetrics(FLayoutFontRef Font, float& OutAscent, float& OutDescent, float& OutHeight) override;

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
