#if WITH_DEV_AUTOMATION_TESTS

#include "CathodePixelFormat.h"
#include "CathodeRaster.h"
#include "CathodeSurfaceRasterizer.h"

#include "Misc/AutomationTest.h"

// Specs for the live producer path (FSurfaceRasterizer). They drive the REAL rasterize-frame entry with a
// RECORDING backend (records the dispatch + clip, supplies bitmap data) and a recording output seam, and
// assert OUR handoff bookkeeping — the live entry routes through the SAME DispatchPaintOps, clips to the
// dirty rects (dirty-only rendering), produces + hands off the bitmap + dirty-rect, and fires frame-ready
// ONLY on change. NEVER pixels. Authored, not run this sprint.

namespace
{
using namespace Cathode;

enum class ERecordedKind : uint8 { Clip, ResetClip, FillRect, FillBox, DrawImage, DrawGlyphRun, ClearRect, End };

struct FRecordedCall
{
	ERecordedKind Kind;
	FBox2f Rect = FBox2f(ForceInit);
};

// Records the draw session calls into a shared sink so the test can inspect them after the (locally-owned)
// context is destroyed by RasterizeFrame.
class FRecordingContext final : public IRasterContext
{
public:
	explicit FRecordingContext(TArray<FRecordedCall>& InSink) : Sink(InSink) {}

	virtual void ClipToRect(const FBox2f& Rect) override { Sink.Add(FRecordedCall{ ERecordedKind::Clip, Rect }); }
	virtual void ResetClip() override { Sink.Add(FRecordedCall{ ERecordedKind::ResetClip }); }
	virtual void FillRect(const FBox2f& Rect, const FColor&) override { Sink.Add(FRecordedCall{ ERecordedKind::FillRect, Rect }); }
	virtual void FillBox(const FBox2f& Rect, const FColor&) override { Sink.Add(FRecordedCall{ ERecordedKind::FillBox, Rect }); }
	virtual void DrawImage(const FBox2f& Rect, const IRasterBitmap&) override { Sink.Add(FRecordedCall{ ERecordedKind::DrawImage, Rect }); }
	virtual void DrawGlyphRun(const FVector2f&, const FPositionedGlyphRun&, const FColor&) override { Sink.Add(FRecordedCall{ ERecordedKind::DrawGlyphRun }); }
	virtual void ClearRect(const FBox2f& Rect) override { Sink.Add(FRecordedCall{ ERecordedKind::ClearRect, Rect }); }
	virtual void End() override { Sink.Add(FRecordedCall{ ERecordedKind::End }); }

private:
	TArray<FRecordedCall>& Sink;
};

class FRecordingBitmap final : public IRasterBitmap
{
public:
	FRecordingBitmap(int32 InW, int32 InH, EPixelFormat InF) : W(InW), H(InH), F(InF) {}

	virtual bool IsValid() const override { return true; }
	virtual int32 GetWidth() const override { return W; }
	virtual int32 GetHeight() const override { return H; }
	virtual EPixelFormat GetFormat() const override { return F; }
	virtual bool GetData(FBitmapData& OutData) override
	{
		OutData.Pixels = const_cast<FRecordingBitmap*>(this); // non-null sentinel; no real pixels
		OutData.Stride = W * PixelFormatBytesPerPixel(F);
		OutData.Width = W;
		OutData.Height = H;
		OutData.Format = F;
		return true;
	}

private:
	int32 W;
	int32 H;
	EPixelFormat F;
};

class FRecordingBackend final : public IRasterBackend
{
public:
	TArray<FRecordedCall> DispatchedCalls; // what the draw session recorded

	virtual TUniquePtr<IRasterBitmap> CreateBitmap(int32 Width, int32 Height, EPixelFormat Format) override
	{
		return MakeUnique<FRecordingBitmap>(Width, Height, Format);
	}
	virtual TUniquePtr<IRasterContext> BeginContext(IRasterBitmap&, const FRasterConfig&) override
	{
		return MakeUnique<FRecordingContext>(DispatchedCalls);
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRasterizerFrameTest,
	"Cathode.Rasterizer.Frame.ProduceClipAndHandOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRasterizerFrameTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FRecordingBackend Backend;
	FRasterConfig Config;
	FSurfaceRasterizer Rasterizer(Backend, 100, 50, Config);
	TestTrue(TEXT("rasterizer valid"), Rasterizer.IsValid());

	// Recording output seam.
	bool bHandedOff = false;
	FBitmapData HandedBitmap;
	TArray<FIntRect> HandedDirty;
	Rasterizer.SetFrameReadySeam([&](const FBitmapData& Bitmap, const TArray<FIntRect>& Dirty)
	{
		bHandedOff = true;
		HandedBitmap = Bitmap;
		HandedDirty = Dirty;
	});

	// A frame with one fill op at (10,10..20,20).
	TArray<FPaintOp> Ops;
	FPaintOp Fill;
	Fill.Kind = EPaintOpKind::FillRect;
	Fill.Rect = FBox2f(FVector2f(10.f, 10.f), FVector2f(20.f, 20.f));
	Fill.Color = FColor(1, 2, 3, 4);
	Ops.Add(Fill);

	const bool bProduced = Rasterizer.RasterizeFrame(Ops);
	TestTrue(TEXT("a frame was produced on a real change"), bProduced);

	// Live entry routed through the SAME dispatch AND clipped to the dirty rect (dirty-only rendering).
	TestTrue(TEXT("at least clip + fill recorded"), Backend.DispatchedCalls.Num() >= 2);
	if (Backend.DispatchedCalls.Num() >= 2)
	{
		TestTrue(TEXT("first call clips to the dirty rect (dirty-only)"),
			Backend.DispatchedCalls[0].Kind == ERecordedKind::Clip
			&& Backend.DispatchedCalls[0].Rect == FBox2f(FVector2f(10.f, 10.f), FVector2f(20.f, 20.f)));
		TestTrue(TEXT("the op is dispatched within the clip (same DispatchPaintOps)"),
			Backend.DispatchedCalls[1].Kind == ERecordedKind::FillRect
			&& Backend.DispatchedCalls[1].Rect == Fill.Rect);
	}

	// Output seam genuinely produced + handed off the bitmap + dirty rect.
	TestTrue(TEXT("frame-ready seam invoked"), bHandedOff);
	TestEqual(TEXT("handed-off bitmap is the surface size"), HandedBitmap.Width, 100);
	TestEqual(TEXT("handed-off bitmap height is the surface size"), HandedBitmap.Height, 50);
	TestTrue(TEXT("handed-off bitmap has pixel data"), HandedBitmap.Pixels != nullptr);
	TestEqual(TEXT("one dirty rect handed off"), HandedDirty.Num(), 1);
	if (HandedDirty.Num() == 1)
	{
		TestTrue(TEXT("the dirty rect covers the painted op"), HandedDirty[0] == FIntRect(10, 10, 20, 20));
	}

	// Frame-ready ONLY on change: an empty frame produces nothing and does not hand off.
	bHandedOff = false;
	const bool bProducedEmpty = Rasterizer.RasterizeFrame(TArray<FPaintOp>());
	TestFalse(TEXT("no frame produced when nothing is dirty"), bProducedEmpty);
	TestFalse(TEXT("frame-ready not fired when nothing changed"), bHandedOff);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
