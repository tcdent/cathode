#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeGlyphAssembly.h"
#include "CathodeShapeFeatures.h"
#include "CathodeText.h"

#include "Misc/AutomationTest.h"

// Specs for the shaper's BESPOKE logic — feature mapping and glyph-assembly math. They are hb_*-FREE:
// the shaper's hb_shape (and the shaped positions/ids/advances it produces) are vendor value, NEVER
// asserted. The raw glyphs here are SCRIPTED OPAQUE INPUT (standing in for the vendor output); the specs
// assert OUR mapping + scaling + cumulative-position math on them, not the input values as "the shaping
// output". Authored, not run this sprint.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeTextFeatureMappingTest,
	"Cathode.Text.Shaping.FeatureMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeTextFeatureMappingTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	const uint32 KernTag = MakeOpenTypeTag('k', 'e', 'r', 'n');
	const uint32 LigaTag = MakeOpenTypeTag('l', 'i', 'g', 'a');
	const uint32 CaltTag = MakeOpenTypeTag('c', 'a', 'l', 't');

	// Default config: kern/liga/calt all on.
	{
		FTextFeatures Features; // defaults: all true
		const TArray<FShapeFeature> Built = BuildShapeFeatures(Features);

		TestEqual(TEXT("three features built"), Built.Num(), 3);
		if (Built.Num() == 3)
		{
			TestTrue(TEXT("kern tag + value on"), Built[0].Tag == KernTag && Built[0].Value == 1);
			TestTrue(TEXT("liga tag + value on"), Built[1].Tag == LigaTag && Built[1].Value == 1);
			TestTrue(TEXT("calt tag + value on"), Built[2].Tag == CaltTag && Built[2].Value == 1);
		}
	}

	// Retro-CRT: kern off, the rest on.
	{
		FTextFeatures Features;
		Features.bKerning = false;
		const TArray<FShapeFeature> Built = BuildShapeFeatures(Features);

		TestEqual(TEXT("three features built"), Built.Num(), 3);
		if (Built.Num() == 3)
		{
			TestTrue(TEXT("kern is explicitly OFF (retro)"), Built[0].Tag == KernTag && Built[0].Value == 0);
			TestTrue(TEXT("liga stays on"), Built[1].Tag == LigaTag && Built[1].Value == 1);
			TestTrue(TEXT("calt stays on"), Built[2].Tag == CaltTag && Built[2].Value == 1);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeTextGlyphAssemblyTest,
	"Cathode.Text.Shaping.GlyphAssembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeTextGlyphAssemblyTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// Scripted raw glyphs (OPAQUE input standing in for vendor shaping output).
	TArray<FRawGlyph> Raw;
	Raw.Add(FRawGlyph{ /*id*/ 5, /*cluster*/ 0, /*xadv*/ 10, /*yadv*/ 0, /*xoff*/ 2, /*yoff*/ 0 });
	Raw.Add(FRawGlyph{ /*id*/ 7, /*cluster*/ 1, /*xadv*/ 20, /*yadv*/ 0, /*xoff*/ 0, /*yoff*/ 3 });

	// PixelsPerUnit = 1.0 -> the cumulative-position + scaling math, isolated.
	{
		const TArray<FShapedGlyph> Glyphs = AssembleGlyphs(Raw, 1.0f);

		TestEqual(TEXT("two glyphs assembled"), Glyphs.Num(), 2);
		if (Glyphs.Num() == 2)
		{
			// glyph 0 at the pen origin + its offset.
			TestTrue(TEXT("g0 id preserved"), Glyphs[0].GlyphId == 5);
			TestTrue(TEXT("g0 X = pen(0) + offset(2)"), FMath::IsNearlyEqual(Glyphs[0].X, 2.0f));
			TestTrue(TEXT("g0 advance scaled"), FMath::IsNearlyEqual(Glyphs[0].XAdvance, 10.0f));

			// glyph 1 at the CUMULATIVE pen (after g0's advance) + its offset.
			TestTrue(TEXT("g1 id preserved"), Glyphs[1].GlyphId == 7);
			TestTrue(TEXT("g1 X = pen(10) + offset(0) — cumulative"), FMath::IsNearlyEqual(Glyphs[1].X, 10.0f));
			TestTrue(TEXT("g1 Y = pen(0) + offset(3)"), FMath::IsNearlyEqual(Glyphs[1].Y, 3.0f));
			TestTrue(TEXT("g1 advance scaled"), FMath::IsNearlyEqual(Glyphs[1].XAdvance, 20.0f));
		}
	}

	// PixelsPerUnit = 0.5 -> the scaling halves positions + advances; accumulation still holds.
	{
		const TArray<FShapedGlyph> Glyphs = AssembleGlyphs(Raw, 0.5f);
		TestEqual(TEXT("two glyphs assembled"), Glyphs.Num(), 2);
		if (Glyphs.Num() == 2)
		{
			TestTrue(TEXT("g0 X scaled = 1.0"), FMath::IsNearlyEqual(Glyphs[0].X, 1.0f));
			TestTrue(TEXT("g1 X cumulative+scaled = 5.0"), FMath::IsNearlyEqual(Glyphs[1].X, 5.0f));
			TestTrue(TEXT("g0 advance scaled = 5.0"), FMath::IsNearlyEqual(Glyphs[0].XAdvance, 5.0f));
		}
	}

	// Empty input -> empty output.
	{
		const TArray<FShapedGlyph> Glyphs = AssembleGlyphs(TArray<FRawGlyph>(), 1.0f);
		TestEqual(TEXT("empty raw -> empty glyphs"), Glyphs.Num(), 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
