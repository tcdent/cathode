#include "CathodeGlyphAssembly.h"

namespace Cathode
{

TArray<FShapedGlyph> AssembleGlyphs(const TArray<FRawGlyph>& Raw, float PixelsPerUnit)
{
	TArray<FShapedGlyph> Out;
	Out.Reserve(Raw.Num());

	float PenX = 0.0f;
	float PenY = 0.0f;

	for (const FRawGlyph& R : Raw)
	{
		FShapedGlyph Glyph;
		Glyph.GlyphId = R.GlyphId;
		Glyph.Cluster = R.Cluster;

		// Cumulative draw position = running pen + this glyph's offset (both scaled to px).
		Glyph.X = PenX + R.XOffset * PixelsPerUnit;
		Glyph.Y = PenY + R.YOffset * PixelsPerUnit;

		Glyph.XAdvance = R.XAdvance * PixelsPerUnit;
		Glyph.YAdvance = R.YAdvance * PixelsPerUnit;

		// Advance the pen for the next glyph.
		PenX += Glyph.XAdvance;
		PenY += Glyph.YAdvance;

		Out.Add(Glyph);
	}

	return Out;
}

} // namespace Cathode
