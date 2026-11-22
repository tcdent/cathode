#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeText.h"
#include "CathodeTextMetrics.h"

#include "Misc/AutomationTest.h"

// Specs for the BESPOKE metrics + run-extents derivation — hb_*-free. The glyphs are SCRIPTED OPAQUE
// INPUT (standing in for the assembled shaper output); the specs assert OUR computed values (width = our
// sum; bbox = our min/max), like the glyph-assembly cumulative-position specs — never the scripted advances
// echoed back as "the shaping output".

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeTextMetricsTest,
	"Cathode.Text.Metrics.WidthAndExtents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeTextMetricsTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// Two scripted glyphs at cumulative positions with advances.
	TArray<FShapedGlyph> Glyphs;
	{
		FShapedGlyph G0;
		G0.GlyphId = 5; G0.X = 2.0f; G0.Y = 0.0f; G0.XAdvance = 10.0f;
		FShapedGlyph G1;
		G1.GlyphId = 7; G1.X = 10.0f; G1.Y = 0.0f; G1.XAdvance = 20.0f;
		Glyphs.Add(G0);
		Glyphs.Add(G1);
	}

	// Metrics: width = OUR sum of advances (30); vertical extents pass through (already scaled in shaper).
	{
		const FTextMetrics Metrics = DeriveMetrics(Glyphs, /*Ascent*/ 8.0f, /*Descent*/ 2.0f, /*LineGap*/ 1.0f);
		TestTrue(TEXT("width = our sum of advances (10+20)"), FMath::IsNearlyEqual(Metrics.Width, 30.0f));
		TestTrue(TEXT("ascent passed through"), FMath::IsNearlyEqual(Metrics.Ascent, 8.0f));
		TestTrue(TEXT("descent passed through"), FMath::IsNearlyEqual(Metrics.Descent, 2.0f));
		TestTrue(TEXT("line gap passed through"), FMath::IsNearlyEqual(Metrics.LineGap, 1.0f));
	}

	// Extents (CathodeRasterizer COVERAGE.md #2): OUR bbox over cumulative positions + advances + vertical extents.
	{
		const FBox2f Extents = DeriveExtents(Glyphs, /*Ascent*/ 8.0f, /*Descent*/ 2.0f);
		// MinX = first glyph X (2); MaxX = last glyph X + advance (10 + 20 = 30); Y = -ascent .. descent.
		TestTrue(TEXT("min X = leftmost glyph position (2)"), FMath::IsNearlyEqual(Extents.Min.X, 2.0f));
		TestTrue(TEXT("max X = rightmost glyph edge (30)"), FMath::IsNearlyEqual(Extents.Max.X, 30.0f));
		TestTrue(TEXT("top = -ascent (-8)"), FMath::IsNearlyEqual(Extents.Min.Y, -8.0f));
		TestTrue(TEXT("bottom = descent (2)"), FMath::IsNearlyEqual(Extents.Max.Y, 2.0f));
	}

	// Empty run -> zero width, empty bounds.
	{
		const FTextMetrics Metrics = DeriveMetrics(TArray<FShapedGlyph>(), 8.0f, 2.0f, 1.0f);
		TestTrue(TEXT("empty run width = 0"), FMath::IsNearlyEqual(Metrics.Width, 0.0f));
		const FBox2f Extents = DeriveExtents(TArray<FShapedGlyph>(), 8.0f, 2.0f);
		TestTrue(TEXT("empty run extents are empty"), Extents.Min == FVector2f::ZeroVector && Extents.Max == FVector2f::ZeroVector);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
