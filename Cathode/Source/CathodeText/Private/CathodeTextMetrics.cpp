#include "CathodeTextMetrics.h"

namespace Cathode
{

FTextMetrics DeriveMetrics(const TArray<FShapedGlyph>& Glyphs, float Ascent, float Descent, float LineGap)
{
	FTextMetrics Metrics;

	// Width = OUR sum of the glyph advances.
	float Width = 0.0f;
	for (const FShapedGlyph& Glyph : Glyphs)
	{
		Width += Glyph.XAdvance;
	}
	Metrics.Width = Width;

	// Vertical extents come from the font (read + scaled in the shaper); passed through here.
	Metrics.Ascent = Ascent;
	Metrics.Descent = Descent;
	Metrics.LineGap = LineGap;

	return Metrics;
}

FBox2f DeriveExtents(const TArray<FShapedGlyph>& Glyphs, float Ascent, float Descent)
{
	if (Glyphs.Num() == 0)
	{
		return FBox2f(ForceInit); // empty run -> empty bounds
	}

	float MinX = TNumericLimits<float>::Max();
	float MaxX = TNumericLimits<float>::Lowest();
	for (const FShapedGlyph& Glyph : Glyphs)
	{
		MinX = FMath::Min(MinX, Glyph.X);
		MaxX = FMath::Max(MaxX, Glyph.X + Glyph.XAdvance);
	}

	// Baseline at Y=0: ascent is above the baseline (negative Y, Y grows down), descent below.
	return FBox2f(FVector2f(MinX, -Ascent), FVector2f(MaxX, Descent));
}

} // namespace Cathode
