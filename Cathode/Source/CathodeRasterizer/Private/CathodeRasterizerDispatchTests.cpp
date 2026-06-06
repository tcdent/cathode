#if WITH_DEV_AUTOMATION_TESTS

#include "CathodePaintDispatch.h"
#include "CathodeRaster.h"

#include "Misc/AutomationTest.h"

// Specs for the Rasterizer's draw-DISPATCH — OUR routing logic. They drive the REAL production
// DispatchPaintOps (the same entry the live paint path calls) with a RECORDING context substituting the
// OUTPUT sink, and feed real draw-ops + glyph-runs through it. This CLOSES the input-seam invocation-trace
// held open since Slice 1: the production dispatch genuinely CONSUMES the input seam and routes each op to
// the backend with our-computed params. No forked/parallel dispatch. We assert WHICH call fired and WHAT
// geometry/color/origin WE forwarded (dispatch args) — NEVER a rendered/readback pixel. Authored, not run.

namespace
{
using namespace Cathode;

enum class ERecordedKind : uint8 { FillRect, FillBox, DrawImage, DrawGlyphRun, ClearRect, End };

struct FRecordedCall
{
	ERecordedKind Kind;
	FBox2f Rect = FBox2f(ForceInit);
	FColor Color = FColor::Black;
	const IRasterBitmap* Image = nullptr;
	FVector2f GlyphOrigin = FVector2f::ZeroVector;
};

// Records each IRasterContext call — the OUTPUT sink the dispatch routes to. No pixels.
class FRecordingContext final : public IRasterContext
{
public:
	TArray<FRecordedCall> Calls;

	virtual void ClipToRect(const FBox2f&) override {}
	virtual void ResetClip() override {}

	virtual void FillRect(const FBox2f& Rect, const FColor& Color) override
	{
		Calls.Add(FRecordedCall{ ERecordedKind::FillRect, Rect, Color });
	}
	virtual void FillBox(const FBox2f& Box, const FColor& Color) override
	{
		Calls.Add(FRecordedCall{ ERecordedKind::FillBox, Box, Color });
	}
	virtual void DrawImage(const FBox2f& DestRect, const IRasterBitmap& Source) override
	{
		FRecordedCall Call{ ERecordedKind::DrawImage, DestRect };
		Call.Image = &Source;
		Calls.Add(Call);
	}
	virtual void DrawGlyphRun(const FVector2f& Origin, const FPositionedGlyphRun&, const FColor& Color) override
	{
		FRecordedCall Call{ ERecordedKind::DrawGlyphRun };
		Call.Color = Color;
		Call.GlyphOrigin = Origin;
		Calls.Add(Call);
	}
	virtual void ClearRect(const FBox2f& Rect) override
	{
		Calls.Add(FRecordedCall{ ERecordedKind::ClearRect, Rect });
	}
	virtual void End() override
	{
		Calls.Add(FRecordedCall{ ERecordedKind::End });
	}
};

// A trivial bitmap to serve as a DrawImage source (no pixels).
class FStubBitmap final : public IRasterBitmap
{
public:
	virtual bool IsValid() const override { return true; }
	virtual int32 GetWidth() const override { return 0; }
	virtual int32 GetHeight() const override { return 0; }
	virtual EPixelFormat GetFormat() const override { return EPixelFormat::PremultipliedBGRA8; }
	virtual bool GetData(FBitmapData&) override { return false; }
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRasterizerDispatchRoutingTest,
	"Cathode.Rasterizer.Dispatch.RoutesEachOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRasterizerDispatchRoutingTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FRasterConfig Config; // decided defaults

	FStubBitmap SourceImage;

	TArray<FPaintOp> Ops;

	FPaintOp Fill;
	Fill.Kind = EPaintOpKind::FillRect;
	Fill.Rect = FBox2f(FVector2f(1.f, 2.f), FVector2f(11.f, 12.f));
	Fill.Color = FColor(10, 20, 30, 40);
	Ops.Add(Fill);

	FPaintOp Box;
	Box.Kind = EPaintOpKind::FillBox;
	Box.Rect = FBox2f(FVector2f(5.f, 5.f), FVector2f(15.f, 25.f));
	Box.Color = FColor(1, 2, 3, 4);
	Ops.Add(Box);

	FPaintOp Image;
	Image.Kind = EPaintOpKind::DrawImage;
	Image.Rect = FBox2f(FVector2f(0.f, 0.f), FVector2f(64.f, 64.f));
	Image.Image = &SourceImage;
	Ops.Add(Image);

	FPaintOp Clear;
	Clear.Kind = EPaintOpKind::ClearRect;
	Clear.Rect = FBox2f(FVector2f(0.f, 0.f), FVector2f(100.f, 100.f));
	Ops.Add(Clear);

	// Drive the REAL production dispatch; the recording context substitutes the OUTPUT sink only.
	FRecordingContext Context;
	DispatchPaintOps(Ops, Context, Config);

	TestEqual(TEXT("every op routed exactly once"), Context.Calls.Num(), 4);
	if (Context.Calls.Num() == 4)
	{
		// FillRect — routed with our geometry + color (dispatch args, not a readback pixel).
		TestTrue(TEXT("op0 routed to FillRect"), Context.Calls[0].Kind == ERecordedKind::FillRect);
		TestTrue(TEXT("op0 forwarded our rect"), Context.Calls[0].Rect == Fill.Rect);
		TestTrue(TEXT("op0 forwarded our color"), Context.Calls[0].Color == Fill.Color);

		// FillBox.
		TestTrue(TEXT("op1 routed to FillBox"), Context.Calls[1].Kind == ERecordedKind::FillBox);
		TestTrue(TEXT("op1 forwarded our box"), Context.Calls[1].Rect == Box.Rect);
		TestTrue(TEXT("op1 forwarded our color"), Context.Calls[1].Color == Box.Color);

		// DrawImage — routed with the dest rect and the source image pointer.
		TestTrue(TEXT("op2 routed to DrawImage"), Context.Calls[2].Kind == ERecordedKind::DrawImage);
		TestTrue(TEXT("op2 forwarded the dest rect"), Context.Calls[2].Rect == Image.Rect);
		TestTrue(TEXT("op2 forwarded the source image"), Context.Calls[2].Image == &SourceImage);

		// ClearRect.
		TestTrue(TEXT("op3 routed to ClearRect"), Context.Calls[3].Kind == ERecordedKind::ClearRect);
		TestTrue(TEXT("op3 forwarded our rect"), Context.Calls[3].Rect == Clear.Rect);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRasterizerFractionalGlyphTest,
	"Cathode.Rasterizer.Dispatch.FractionalGlyphPositioning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRasterizerFractionalGlyphTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FPaintOp Glyph;
	Glyph.Kind = EPaintOpKind::DrawGlyphRun;
	Glyph.GlyphOrigin = FVector2f(10.5f, 20.25f); // deliberately fractional
	Glyph.Color = FColor(5, 6, 7, 8);
	TArray<FPaintOp> Ops;
	Ops.Add(Glyph);

	// Default config: fractional positioning ON -> origin must arrive UNSNAPPED.
	{
		FRasterConfig Config; // bFractionalGlyphPositioning = true
		FRecordingContext Context;
		DispatchPaintOps(Ops, Context, Config);

		TestEqual(TEXT("one glyph-run routed"), Context.Calls.Num(), 1);
		if (Context.Calls.Num() == 1)
		{
			TestTrue(TEXT("routed to DrawGlyphRun"), Context.Calls[0].Kind == ERecordedKind::DrawGlyphRun);
			TestTrue(TEXT("origin X unsnapped (10.5)"), FMath::IsNearlyEqual(Context.Calls[0].GlyphOrigin.X, 10.5f));
			TestTrue(TEXT("origin Y unsnapped (20.25)"), FMath::IsNearlyEqual(Context.Calls[0].GlyphOrigin.Y, 20.25f));
			TestTrue(TEXT("color forwarded"), Context.Calls[0].Color == Glyph.Color);
		}
	}

	// Fractional positioning OFF -> the origin is rounded to the pixel grid (proves the flag drives it).
	{
		FRasterConfig Config;
		Config.bFractionalGlyphPositioning = false;
		FRecordingContext Context;
		DispatchPaintOps(Ops, Context, Config);

		TestEqual(TEXT("one glyph-run routed"), Context.Calls.Num(), 1);
		if (Context.Calls.Num() == 1)
		{
			TestTrue(TEXT("origin X snapped to 11"), FMath::IsNearlyEqual(Context.Calls[0].GlyphOrigin.X, 11.0f));
			TestTrue(TEXT("origin Y snapped to 20"), FMath::IsNearlyEqual(Context.Calls[0].GlyphOrigin.Y, 20.0f));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
