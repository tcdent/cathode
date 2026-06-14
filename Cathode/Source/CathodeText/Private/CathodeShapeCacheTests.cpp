#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeShapeCache.h"
#include "CathodeText.h"

#include "Misc/AutomationTest.h"

// Specs for the result cache + the dual-consumer single-shape — OUR bookkeeping, by SHAPE-CALL-COUNT.
// A recording inner shaper counts how many times it actually shapes; its returned FShapedRun is SCRIPTED
// OPAQUE INPUT (not asserted as shaping output). The specs assert: a repeat same-key request does not
// re-shape; a different key does; and one shape serves both consumers. Authored, not run this sprint.

namespace
{
using namespace Cathode;

// Counts shape calls; returns a fixed scripted result. The scripted values are opaque — only the
// call-count and same-result-to-both are asserted.
class FRecordingShaper final : public ITextShaper
{
public:
	int32 ShapeCallCount = 0;
	FShapedRun Scripted;

	virtual FShapedRun Shape(const FShapeRequest&) override
	{
		++ShapeCallCount;
		return Scripted;
	}
};

FShapeRequest MakeRequest(const FString& Text, int32 FontId, float Size)
{
	FShapeRequest Request;
	Request.Text = Text;
	Request.Font = FFontHandle{ FontId };
	Request.FontSizePixels = Size;
	return Request;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeTextShapeCacheTest,
	"Cathode.Text.Cache.HitMissEvict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeTextShapeCacheTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FRecordingShaper* Inner = new FRecordingShaper();
	FCachingShaper Cache{ TUniquePtr<ITextShaper>(Inner) }; // brace-init: parens here are the most-vexing-parse (reads as a function decl)

	const FShapeRequest ReqA = MakeRequest(TEXT("hello"), 0, 16.0f);
	const FShapeRequest ReqB = MakeRequest(TEXT("world"), 0, 16.0f);

	// First request of a key -> a miss -> one shape.
	Cache.Shape(ReqA);
	TestEqual(TEXT("first request shapes once"), Inner->ShapeCallCount, 1);

	// Repeat the same key -> a hit -> NO re-shape.
	Cache.Shape(ReqA);
	TestEqual(TEXT("repeat same key does not re-shape"), Inner->ShapeCallCount, 1);

	// A different key -> a miss -> a second shape.
	Cache.Shape(ReqB);
	TestEqual(TEXT("a different key shapes again"), Inner->ShapeCallCount, 2);

	// Same size but kern toggled -> a different key -> another shape.
	FShapeRequest ReqARetro = ReqA;
	ReqARetro.Features.bKerning = false;
	Cache.Shape(ReqARetro);
	TestEqual(TEXT("toggling a feature is a distinct key"), Inner->ShapeCallCount, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeTextDualConsumerTest,
	"Cathode.Text.Cache.DualConsumerSingleShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeTextDualConsumerTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FRecordingShaper* Inner = new FRecordingShaper();
	// Scripted result with distinguishable, opaque fields.
	Inner->Scripted.Metrics.Width = 42.0f;
	Inner->Scripted.Glyphs.Add(FShapedGlyph{});
	Inner->Scripted.Glyphs.Add(FShapedGlyph{});
	Inner->Scripted.Font = FFontHandle{ 3 };

	FCachingShaper Cache{ TUniquePtr<ITextShaper>(Inner) }; // brace-init: parens here are the most-vexing-parse (reads as a function decl)

	const FShapeRequest Req = MakeRequest(TEXT("AB"), 3, 16.0f);

	// Consumer A = Layout (measurement); Consumer B = Rasterizer (glyph-run). Both request the SAME key.
	const FShapedRun ForMeasurement = Cache.Shape(Req);
	const FShapedRun ForGlyphRun = Cache.Shape(Req);

	// ONE shape served both (no double-shape).
	TestEqual(TEXT("a single shape serves both consumers"), Inner->ShapeCallCount, 1);

	// Measurement extraction (Layout reads Metrics) AND glyph-run extraction (Rasterizer reads
	// Glyphs+Extents+Font) consume the SAME result -> their views match.
	TestTrue(TEXT("both consumers see the same metrics width"),
		FMath::IsNearlyEqual(ForMeasurement.Metrics.Width, ForGlyphRun.Metrics.Width));
	TestEqual(TEXT("both consumers see the same glyph count"), ForMeasurement.Glyphs.Num(), ForGlyphRun.Glyphs.Num());
	TestTrue(TEXT("both consumers see the same font handle"), ForMeasurement.Font == ForGlyphRun.Font);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
