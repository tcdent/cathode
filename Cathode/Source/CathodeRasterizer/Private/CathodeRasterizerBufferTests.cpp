#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDoubleBuffer.h"
#include "CathodePixelFormat.h"
#include "CathodeRaster.h"

#include "Misc/AutomationTest.h"

// Specs for the Rasterizer's BESPOKE bookkeeping: double-buffer state and pixel-format math. They drive
// OUR logic through a RECORDING backend (no Blend2D, no pixels) and assert bookkeeping — buffer swap,
// draw-targets-back, frame-ready-only-on-change, dims=surface, format/stride math. ZERO pixel/color
// assertions; vendor rasterization is NOT tested. Authored, not run this sprint.

namespace
{
using namespace Cathode;

// A bitmap that records its requested dimensions/format; IsValid() always true. No pixels.
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
		OutData.Pixels = nullptr;
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

struct FCreateRecord
{
	int32 W;
	int32 H;
	EPixelFormat F;
};

// A backend that records every CreateBitmap call and hands back recording bitmaps.
class FRecordingBackend final : public IRasterBackend
{
public:
	TArray<FCreateRecord> Created;

	virtual TUniquePtr<IRasterBitmap> CreateBitmap(int32 Width, int32 Height, EPixelFormat Format) override
	{
		Created.Add(FCreateRecord{ Width, Height, Format });
		return MakeUnique<FRecordingBitmap>(Width, Height, Format);
	}

	virtual TUniquePtr<IRasterContext> BeginContext(IRasterBitmap&, const FRasterConfig&) override
	{
		return nullptr; // draw session is Slice 4
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRasterizerDoubleBufferTest,
	"Cathode.Rasterizer.Buffer.DoubleBufferBookkeeping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRasterizerDoubleBufferTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FRecordingBackend Backend;
	FDoubleBuffer Buffer(Backend, 100, 50, EPixelFormat::PremultipliedBGRA8);

	// Two buffers, each at the surface's native dimensions/format (never small-and-upscale).
	TestEqual(TEXT("allocated two buffers"), Backend.Created.Num(), 2);
	if (Backend.Created.Num() == 2)
	{
		TestEqual(TEXT("buffer 0 width = surface"), Backend.Created[0].W, 100);
		TestEqual(TEXT("buffer 0 height = surface"), Backend.Created[0].H, 50);
		TestTrue(TEXT("buffer 0 format = surface"), Backend.Created[0].F == EPixelFormat::PremultipliedBGRA8);
		TestEqual(TEXT("buffer 1 width = surface"), Backend.Created[1].W, 100);
		TestEqual(TEXT("buffer 1 height = surface"), Backend.Created[1].H, 50);
	}
	TestTrue(TEXT("double buffer is valid"), Buffer.IsValid());
	TestTrue(TEXT("dimensions equal the surface"), Buffer.GetWidth() == 100 && Buffer.GetHeight() == 50);

	IRasterBitmap* const InitialBack = Buffer.GetBackBuffer();
	IRasterBitmap* const InitialFront = Buffer.GetFrontBuffer();
	TestTrue(TEXT("back and front are distinct buffers"), InitialBack != InitialFront);
	TestTrue(TEXT("both buffers non-null"), InitialBack != nullptr && InitialFront != nullptr);

	// No draw -> no publish, no frame-ready; the draw target stays the same buffer.
	TestFalse(TEXT("swap is a no-op when nothing changed"), Buffer.SwapIfChanged());
	TestTrue(TEXT("back buffer unchanged after no-op swap"), Buffer.GetBackBuffer() == InitialBack);

	// A draw happened -> mark changed -> swap publishes (frame-ready) and toggles the buffers.
	Buffer.MarkChanged();
	TestTrue(TEXT("change is flagged"), Buffer.HasChanged());
	TestTrue(TEXT("swap occurs on real change (emit frame-ready)"), Buffer.SwapIfChanged());
	TestTrue(TEXT("the drawn buffer is now the front (published)"), Buffer.GetFrontBuffer() == InitialBack);
	TestTrue(TEXT("the old front is now the back (next draw target)"), Buffer.GetBackBuffer() == InitialFront);
	TestFalse(TEXT("changed flag cleared after swap"), Buffer.HasChanged());

	// Nothing new drawn -> swap is a no-op again.
	TestFalse(TEXT("swap is a no-op again when unchanged"), Buffer.SwapIfChanged());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRasterizerPixelFormatTest,
	"Cathode.Rasterizer.Buffer.PixelFormatMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRasterizerPixelFormatTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	TestEqual(TEXT("PremultipliedBGRA8 is 4 bytes/pixel"), PixelFormatBytesPerPixel(EPixelFormat::PremultipliedBGRA8), 4);
	TestEqual(TEXT("OpaqueBGRA8 is 4 bytes/pixel"), PixelFormatBytesPerPixel(EPixelFormat::OpaqueBGRA8), 4);
	TestEqual(TEXT("Alpha8 is 1 byte/pixel"), PixelFormatBytesPerPixel(EPixelFormat::Alpha8), 1);

	TestEqual(TEXT("min row stride 100px BGRA8 = 400"), MinRowStride(100, EPixelFormat::PremultipliedBGRA8), 400);
	TestEqual(TEXT("min row stride of zero width = 0"), MinRowStride(0, EPixelFormat::PremultipliedBGRA8), 0);

	TestEqual(TEXT("min buffer size 100x50 BGRA8 = 20000"),
		static_cast<int32>(MinBufferSize(100, 50, EPixelFormat::PremultipliedBGRA8)), 20000);
	TestEqual(TEXT("min buffer size of zero height = 0"),
		static_cast<int32>(MinBufferSize(100, 0, EPixelFormat::PremultipliedBGRA8)), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
