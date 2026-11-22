#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeShapeCache.h"
#include "CathodeText.h"

#include "Misc/AutomationTest.h"

// Specs for the measurement + glyph-run seams — both genuinely PRODUCED + populated off the ONE shaped
// result (no stub/empty, no double-shape). A recording shaper supplies a scripted FShapedRun (opaque
// input); the specs assert the seams extract its fields, and that ONE shape feeds BOTH seams. Never the
// shaped positions as shaping output.

namespace
{
using namespace Cathode;

class FRecordingShaper final : public ITextShaper
{
public:
	int32 ShapeCallCount = 0;
	FShapedRun Scripted;
	virtual FShapedRun Shape(const FShapeRequest&) override { ++ShapeCallCount; return Scripted; }
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeTextSeamsTest,
	"Cathode.Text.Seams.MeasurementAndGlyphRunProduced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeTextSeamsTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// A scripted shaped result with distinguishable, opaque fields.
	FRecordingShaper* Inner = new FRecordingShaper();
	Inner->Scripted.Metrics.Width = 50.0f;
	Inner->Scripted.Metrics.Ascent = 8.0f;
	Inner->Scripted.Glyphs.Add(FShapedGlyph{});
	Inner->Scripted.Glyphs.Add(FShapedGlyph{});
	Inner->Scripted.Glyphs.Add(FShapedGlyph{});
	Inner->Scripted.Extents = FBox2f(FVector2f(0.f, -8.f), FVector2f(50.f, 2.f));
	Inner->Scripted.Font = FFontHandle{ 7 };

	FCachingShaper Cache{ TUniquePtr<ITextShaper>(Inner) }; // brace-init: parens here are the most-vexing-parse (reads as a function decl)

	FShapeRequest Request;
	Request.Text = TEXT("text");
	Request.Font = FFontHandle{ 7 };
	Request.FontSizePixels = 16.0f;

	// ONE shape -> one FShapedRun; both seams extract off the SAME result.
	const FShapedRun Shaped = Cache.Shape(Request);
	TestEqual(TEXT("one shape produced the result"), Inner->ShapeCallCount, 1);

	// Measurement seam (to Layout): yields the metrics off the shaped result.
	const FTextMetrics& Measured = GetMetrics(Shaped);
	TestTrue(TEXT("measurement seam yields the run's metrics"), FMath::IsNearlyEqual(Measured.Width, Shaped.Metrics.Width));
	TestTrue(TEXT("measurement seam yields the run's ascent"), FMath::IsNearlyEqual(Measured.Ascent, Shaped.Metrics.Ascent));

	// Glyph-run seam (to the Rasterizer): ids + positions + EXTENTS + the font handle, populated.
	const FGlyphRun Run = GetGlyphRun(Shaped);
	TestEqual(TEXT("glyph-run seam carries all the glyphs"), Run.Glyphs.Num(), Shaped.Glyphs.Num());
	TestTrue(TEXT("glyph-run seam is non-empty (populated, not a stub)"), Run.Glyphs.Num() > 0);
	TestTrue(TEXT("glyph-run seam carries the run extents (003 #2)"), Run.Extents == Shaped.Extents);
	TestTrue(TEXT("glyph-run seam carries the engine-neutral font handle"), Run.Font == Shaped.Font);

	// Both seams came off the SAME single shape — no extra shape for the second consumer.
	TestEqual(TEXT("both seams produced from one shape (no double-shape)"), Inner->ShapeCallCount, 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
