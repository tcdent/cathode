#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CathodeEgress.h"
#include "CathodeEgressUploader.h"
#include "CathodeSurfaces.h"
#include "CathodeTripleBuffer.h" // the lock-free triple-buffer (slot-logic unit test below)

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIResources.h"

// The Texture Egress production path. TWO proofs, BOTH riding DriveAll(egress.GetSeam()) end-to-end
// (the egress's own copy + marshal + upload made the result; the test NEVER calls RHIUpdateTexture2D, and reads the
// EGRESS's own UTexture2D — a test-built texture / a re-hand-upload would prove the MECHANISM, not the egress):
//
//  - THE READBACK (the load-bearing proof): read the egress-uploaded UTexture2D's GPU pixels back and compare to the source
//    bitmap. Binds source + region + pitch + FORMAT together via the on-GPU result. A DISTINCT-channel pattern
//    (R=200 dominant, B=40 low) makes a wrong format-map (B<->R, PF_B8G8R8A8 vs PF_R8G8B8A8) read R-dominant as
//    blue -> RED. Win64 + the real D3D RHI.
//  - THE SEAM-SPY: captures the EGRESS's assembled values and binds the TWO the readback cannot see — ON-CHANGE
//    (an idle drive must NOT invoke the uploader; a stale-but-correct readback would pass trivially) and
//    RENDER-THREAD (IsInRenderingThread at the upload). The source/region/pitch checks are corroborating-DIAGNOSTIC
//    (the readback binds them via the result; the spy pinpoints WHICH is wrong on a readback RED), compared
//    EGRESS-ASSEMBLED vs the independently-KNOWN document (not test-fed -> not circular). A PADDED stride
//    (Stride > Width*4) bites a naive Width*4 pitch.

namespace
{
	using namespace Cathode;

	// A minimal synthetic document — a non-null batch so RenderAndRelay proceeds (the egress is source-agnostic;
	// it consumes only the neutral readiness, so the bitmap need not come from the composition engines — this
	// keeps the egress test free of a CathodeComposition dep, preserving its no-dependents confinement).
	class FSyntheticEgressDocument final : public ISurfaceDocument
	{
	public:
		virtual FSurfacePaintBatch RenderFrame(int32 /*AvailableWidth*/) override { return FSurfacePaintBatch{ this }; }
	};

	// A synthetic render target that publishes a KNOWN bitmap as the neutral view and CHANGES exactly once (so the
	// second drive is idle — for the on-change gate). The dirty rect is the full surface (the full-texture case).
	class FSyntheticEgressTarget final : public ISurfaceRenderTarget
	{
	public:
		FSyntheticEgressTarget(const uint8* InPixels, int32 InStride, int32 InW, int32 InH)
			: Pixels(InPixels), Stride(InStride), W(InW), H(InH) {}

		virtual bool ProduceFrame(const FSurfacePaintBatch& /*Batch*/) override { ++Frames; return Frames == 1; }
		virtual FSurfaceReadyFrame GetReadyFrame() const override
		{
			FSurfaceReadyFrame Frame;
			Frame.Buffer.Pixels = Pixels;
			Frame.Buffer.Stride = Stride;
			Frame.Buffer.Width = W;
			Frame.Buffer.Height = H;
			Frame.Buffer.Format = ESurfacePixelFormat::PremultipliedBGRA8;
			Frame.DirtyRects.Add(FIntRect(0, 0, W, H));
			return Frame;
		}
		virtual int32 GetWidth() const override { return W; }
		virtual int32 GetHeight() const override { return H; }
		virtual ESurfacePixelFormat GetFormat() const override { return ESurfacePixelFormat::PremultipliedBGRA8; }

	private:
		const uint8* Pixels;
		int32 Stride;
		int32 W;
		int32 H;
		mutable int32 Frames = 0;
	};

	FSurfaceId RegisterSynthetic(ISurfaceRegistry& Registry, const uint8* Pixels, int32 Stride, int32 W, int32 H)
	{
		FSurfaceRegistration Reg;
		Reg.Width = W;
		Reg.Height = H;
		Reg.Format = ESurfacePixelFormat::PremultipliedBGRA8;
		Reg.Document = MakeUnique<FSyntheticEgressDocument>();
		Reg.Target = MakeUnique<FSyntheticEgressTarget>(Pixels, Stride, W, H);
		return Registry.Register(MoveTemp(Reg));
	}

	// A distinct-channel, PER-ROW-DISTINCT pattern: R=200 dominant (binds the format-map — a B<->R swap reads as
	// blue), G=80, and B varies BY ROW (4 + Y*8, kept < 90 so R stays dominant). The per-row B is what binds the
	// COPY'S STRIDED READ over a padded source: a wrong read-stride reads the wrong row's B -> the readback
	// mismatches (the padded-stride discriminator, since the uniform-tight copy's UPLOAD pitch is
	// the tight Width*4, not the source Stride). Laid out at the given (possibly padded) stride.
	void FillDistinctPattern(TArray<uint8>& Bytes, int32 Stride, int32 W, int32 H)
	{
		Bytes.SetNumZeroed(H * Stride);
		for (int32 Y = 0; Y < H; ++Y)
		{
			const uint8 RowB = static_cast<uint8>(4 + Y * 8); // per-row-distinct, < 90 so R stays dominant
			for (int32 X = 0; X < W; ++X)
			{
				const int32 I = Y * Stride + X * 4;
				Bytes[I + 0] = RowB; // B (per row)
				Bytes[I + 1] = 80;   // G
				Bytes[I + 2] = 200;  // R (dominant)
				Bytes[I + 3] = 255;  // A
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeEgressReadbackTest,
	"Cathode.Egress.Readback.ProductionUploadRoundTrips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeEgressReadbackTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
	const int32 W = 8;
	const int32 H = 8;
	const int32 Stride = W * 4 + 16; // PADDED — a wrong (Width*4) pitch would mis-align rows -> garbled readback
	TArray<uint8> Source;
	FillDistinctPattern(Source, Stride, W, H);

	// PRODUCTION egress (the real RHIUpdateTexture2D). The test never calls RHIUpdateTexture2D — only the egress does.
	TUniquePtr<ITextureEgress> Egress = CreateTextureEgress();
	TUniquePtr<ISurfaceRegistry> Registry = CreateSurfaceRegistry();
	const FSurfaceId Surface = RegisterSynthetic(*Registry, Source.GetData(), Stride, W, H);

	// Drive the production path: the egress copies + marshals + uploads into ITS OWN UTexture2D.
	Registry->DriveAll(Egress->GetSeam()); Egress->PumpGameThreadUploads(); /* split — DriveAll worker-emits (copy+store), Pump does the UObject+upload */
	FlushRenderingCommands();

	UTexture2D* Texture = Egress->GetTexture(Surface);
	if (Texture == nullptr) { AddError(TEXT("[024-EGRESS] the egress created no texture for the driven surface")); return false; }
	FTextureResource* Resource = Texture->GetResource();
	if (Resource == nullptr) { AddError(TEXT("[024-EGRESS] the egress texture has no RHI resource")); return false; }

	// Read back the EGRESS's GPU texture (non-circular: read the texture, compare to the source bitmap; never a re-read).
	TArray<FColor> Readback;
	ENQUEUE_RENDER_COMMAND(CathodeEgressReadback)(
		[Resource, W, H, &Readback](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* TexRHI = Resource->GetTexture2DRHI();
			if (TexRHI != nullptr)
			{
				RHICmdList.ReadSurfaceData(TexRHI, FIntRect(0, 0, W, H), Readback, FReadSurfaceDataFlags(RCM_UNorm));
			}
		});
	FlushRenderingCommands();

	if (Readback.Num() != W * H)
	{
		AddError(FString::Printf(TEXT("[024-EGRESS] readback returned %d pixels (expected %d)"), Readback.Num(), W * H));
		return false;
	}

	int32 Match = 0;        // exact == the source bitmap (binds source+region+pitch+format together)
	int32 RDominantOk = 0;  // R-dominant read as R (format-map correct)
	int32 RDominantSwap = 0; // R-dominant read as B (a B<->R format-map swap)
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const FColor& Px = Readback[Y * W + X];
			const int32 S = Y * Stride + X * 4; // padded source, BGRA
			if (Px.B == Source[S + 0] && Px.G == Source[S + 1] && Px.R == Source[S + 2]) { ++Match; }
			if (Px.R > 150 && Px.B < 90) { ++RDominantOk; }
			if (Px.B > 150 && Px.R < 90) { ++RDominantSwap; }
		}
	}
	AddInfo(FString::Printf(
		TEXT("[WIN64-EXECUTED][024-EGRESS] readback: match=%d/%d R-as-R=%d R-as-B(swap)=%d | sample(0,0) read B%uG%uR%u vs src B%uG%uR%u stride=%d dims=%dx%d"),
		Match, W * H, RDominantOk, RDominantSwap, Readback[0].B, Readback[0].G, Readback[0].R, Source[0], Source[1], Source[2], Stride, W, H));

	TestEqual(TEXT("[WIN64-EXECUTED] the egress's production upload round-trips the source bitmap on the GPU (== dirty bytes)"), Match, W * H);
	TestEqual(TEXT("the distinct-channel pattern reads R-dominant correctly (format-map bound)"), RDominantOk, W * H);
	TestEqual(TEXT("no B<->R channel swap (a wrong format-map would read R-dominant as blue)"), RDominantSwap, 0);
	return true;
#else
	AddError(TEXT("Cathode.Egress.Readback.ProductionUploadRoundTrips requires Win64 (the box's real D3D RHI via the desktop session)."));
	return false;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeEgressSeamSpyTest,
	"Cathode.Egress.SeamSpy.AssemblesAndGatesUpload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeEgressSeamSpyTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
	const int32 W = 8;
	const int32 H = 8;
	const int32 Stride = W * 4 + 16; // PADDED — bites a naive Width*4 pitch
	TArray<uint8> Source;
	FillDistinctPattern(Source, Stride, W, H);

	// The spy captures the EGRESS's assembled values on the render thread.
	struct FSpyState
	{
		int32 Count = 0;
		uint32 Pitch = 0;
		uint32 RegionDestX = 0, RegionDestY = 0, RegionWidth = 0, RegionHeight = 0;
		bool bInRenderThread = false;
		TArray<uint8> Bytes;
		FSurfaceId Surface;
	} Spy;
	FEgressUploader SpyUploader = [&Spy](const FEgressUpload& Upload)
	{
		++Spy.Count;
		Spy.Pitch = Upload.SourcePitch;
		Spy.RegionDestX = Upload.Region.DestX;
		Spy.RegionDestY = Upload.Region.DestY;
		Spy.RegionWidth = Upload.Region.Width;
		Spy.RegionHeight = Upload.Region.Height;
		Spy.bInRenderThread = IsInRenderingThread();
		Spy.Surface = Upload.Surface;
		Spy.Bytes.SetNumUninitialized(Upload.SourceNumBytes);
		if (Upload.SourceData != nullptr && Upload.SourceNumBytes > 0)
		{
			FMemory::Memcpy(Spy.Bytes.GetData(), Upload.SourceData, Upload.SourceNumBytes);
		}
	};

	TUniquePtr<ITextureEgress> Egress = CreateTextureEgressWithUploader(SpyUploader);
	TUniquePtr<ISurfaceRegistry> Registry = CreateSurfaceRegistry();
	const FSurfaceId Surface = RegisterSynthetic(*Registry, Source.GetData(), Stride, W, H);

	// Drive once (a real change) -> one upload.
	Registry->DriveAll(Egress->GetSeam()); Egress->PumpGameThreadUploads(); /* split — DriveAll worker-emits (copy+store), Pump does the UObject+upload */
	FlushRenderingCommands();

	// === The spy's UNIQUE load-bearing gates (the readback cannot see these) ===
	TestEqual(TEXT("[WIN64-EXECUTED] the egress invoked the uploader exactly once on a real change"), Spy.Count, 1);
	TestTrue(TEXT("[WIN64-EXECUTED] the upload ran ON the render thread (IsInRenderingThread at the upload)"), Spy.bInRenderThread);

	// === Corroborating DIAGNOSTIC: egress-assembled vs the independently-KNOWN document (not test-fed -> not circular) ===
	// The copy is uniform-TIGHT: the upload pitch is the tight Width*4 (NOT the source Stride), the copy is BOUNDED
	// to the rect, and the padded-source read correctness is bound by the readback's per-row-distinct strided read.
	TestEqual(TEXT("[diag] the egress source pitch == the TIGHT Width*4 (uniform-tight copy; the strided 1b pitch evolved)"), static_cast<int32>(Spy.Pitch), W * 4);
	TestEqual(TEXT("[diag] the upload region width == the (full) dirty rect"), static_cast<int32>(Spy.RegionWidth), W);
	TestEqual(TEXT("[diag] the upload region height == the (full) dirty rect"), static_cast<int32>(Spy.RegionHeight), H);
	TestEqual(TEXT("[diag] the upload region origin == the dirty rect origin (0,0)"), static_cast<int32>(Spy.RegionDestX + Spy.RegionDestY), 0);
	TestEqual(TEXT("[diag] the egress surface id == the driven surface"), Spy.Surface.Value, Surface.Value);

	// SourceNumBytes is BOUNDED to the rect (tight Width*Height*4 for the full frame, NOT the strided H*Stride).
	TestEqual(TEXT("the egress's copied bytes are BOUNDED to the rect (tight W*H*4, not the strided H*Stride)"), Spy.Bytes.Num(), W * H * 4);
	// the copied bytes == the rect's pixels extracted TIGHT from the PADDED source (the copy's strided read is correct)
	TArray<uint8> ExpectedTight;
	ExpectedTight.SetNumUninitialized(W * H * 4);
	for (int32 Y = 0; Y < H; ++Y) { FMemory::Memcpy(ExpectedTight.GetData() + Y * (W * 4), Source.GetData() + Y * Stride, W * 4); }
	const bool bBytesMatch = (Spy.Bytes.Num() == W * H * 4) && (FMemory::Memcmp(Spy.Bytes.GetData(), ExpectedTight.GetData(), W * H * 4) == 0);
	TestTrue(TEXT("[diag] the egress's tight copy == the document's rect pixels read at the source Stride"), bBytesMatch);

	AddInfo(FString::Printf(
		TEXT("[WIN64-EXECUTED][024-EGRESS-SPY] count=%d renderThread=%d pitch=%u (tight W*4=%d; src stride=%d) region=%ux%u@(%u,%u) bytes=%d/%d(tight) surface=%d"),
		Spy.Count, Spy.bInRenderThread ? 1 : 0, Spy.Pitch, W * 4, Stride, Spy.RegionWidth, Spy.RegionHeight, Spy.RegionDestX, Spy.RegionDestY, Spy.Bytes.Num(), W * H * 4, Spy.Surface.Value));

	// ON-CHANGE (the spy's unique job): an IDLE drive (no surface change) must NOT invoke the uploader. A readback
	// would read the stale-but-correct texture and pass trivially -> only the spy binds idle.
	Registry->DriveAll(Egress->GetSeam()); Egress->PumpGameThreadUploads(); /* split — DriveAll worker-emits (copy+store), Pump does the UObject+upload */
	FlushRenderingCommands();
	TestEqual(TEXT("[WIN64-EXECUTED] an idle drive (no surface change) invoked the uploader ZERO additional times"), Spy.Count, 1);
	return true;
#else
	AddError(TEXT("Cathode.Egress.SeamSpy.AssemblesAndGatesUpload requires Win64 (UTexture2D RHI resource creation on the box)."));
	return false;
#endif
}

namespace
{
	using namespace Cathode;

	// A synthetic target that publishes a known (mutable) bitmap and emits a CONFIGURED dirty-rect set per drive
	// (frame 1 full, frame 2 an offset sub-rect, ...), changing while frames remain then idling. For the regional
	// proof. Reuses FSyntheticEgressDocument (visible at file scope from the first anon namespace).
	class FSequencedEgressTarget final : public ISurfaceRenderTarget
	{
	public:
		FSequencedEgressTarget(const uint8* InPixels, int32 InStride, int32 InW, int32 InH, TArray<TArray<FIntRect>> InFrames)
			: Pixels(InPixels), Stride(InStride), W(InW), H(InH), Frames(MoveTemp(InFrames)) {}

		virtual bool ProduceFrame(const FSurfacePaintBatch& /*Batch*/) override { ++Produced; return Produced <= Frames.Num(); }
		virtual FSurfaceReadyFrame GetReadyFrame() const override
		{
			FSurfaceReadyFrame Frame;
			Frame.Buffer.Pixels = Pixels;
			Frame.Buffer.Stride = Stride;
			Frame.Buffer.Width = W;
			Frame.Buffer.Height = H;
			Frame.Buffer.Format = ESurfacePixelFormat::PremultipliedBGRA8;
			const int32 Index = Produced - 1;
			if (Frames.IsValidIndex(Index)) { Frame.DirtyRects = Frames[Index]; }
			return Frame;
		}
		virtual int32 GetWidth() const override { return W; }
		virtual int32 GetHeight() const override { return H; }
		virtual ESurfacePixelFormat GetFormat() const override { return ESurfacePixelFormat::PremultipliedBGRA8; }

	private:
		const uint8* Pixels;
		int32 Stride;
		int32 W;
		int32 H;
		TArray<TArray<FIntRect>> Frames;
		mutable int32 Produced = 0;
	};

	FSurfaceId RegisterSequenced(ISurfaceRegistry& Registry, const uint8* Pixels, int32 Stride, int32 W, int32 H, TArray<TArray<FIntRect>> Frames)
	{
		FSurfaceRegistration Reg;
		Reg.Width = W;
		Reg.Height = H;
		Reg.Format = ESurfacePixelFormat::PremultipliedBGRA8;
		Reg.Document = MakeUnique<FSyntheticEgressDocument>();
		Reg.Target = MakeUnique<FSequencedEgressTarget>(Pixels, Stride, W, H, MoveTemp(Frames));
		return Registry.Register(MoveTemp(Reg));
	}

	// Overwrite a sub-rect with a B-DOMINANT distinct color (B=200, G=80, R=40) — clearly != the R-dominant base,
	// so a wrong DEST offset places B outside R (-> the unchanged region mismatches) or a wrong SRC offset reads
	// the base instead of B (-> R mismatches): the readback REDs either way.
	void FillSubRect(TArray<uint8>& Bytes, int32 Stride, FIntRect R)
	{
		for (int32 Y = R.Min.Y; Y < R.Max.Y; ++Y)
		{
			for (int32 X = R.Min.X; X < R.Max.X; ++X)
			{
				const int32 I = Y * Stride + X * 4;
				Bytes[I + 0] = 200; // B (dominant)
				Bytes[I + 1] = 80;  // G
				Bytes[I + 2] = 40;  // R
				Bytes[I + 3] = 255; // A
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeEgressRegionalSeamSpyTest,
	"Cathode.Egress.Regional.DirtyRectRegionIsSubRect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeEgressRegionalSeamSpyTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
	const int32 W = 8;
	const int32 H = 8;
	const int32 Stride = W * 4 + 16;     // padded
	const FIntRect R(2, 2, 6, 6);        // a 4x4 sub-rect at (2,2) — OFFSET, not (0,0), not the full dims
	TArray<uint8> Bitmap;
	FillDistinctPattern(Bitmap, Stride, W, H);

	struct FSpyState
	{
		int32 Count = 0;
		bool bInRenderThread = true;
		TArray<FUpdateTextureRegion2D> Regions;
		TArray<int32> ByteCounts;
	} Spy;
	FEgressUploader SpyUploader = [&Spy](const FEgressUpload& Upload)
	{
		++Spy.Count;
		Spy.bInRenderThread &= IsInRenderingThread();
		Spy.Regions.Add(Upload.Region);
		Spy.ByteCounts.Add(Upload.SourceNumBytes);
	};

	TUniquePtr<ITextureEgress> Egress = CreateTextureEgressWithUploader(SpyUploader);
	TUniquePtr<ISurfaceRegistry> Registry = CreateSurfaceRegistry();
	TArray<TArray<FIntRect>> Frames;
	Frames.Add({ FIntRect(0, 0, W, H) }); // frame 1: the full texture
	Frames.Add({ R });                    // frame 2: only the offset sub-rect
	const FSurfaceId Surface = RegisterSequenced(*Registry, Bitmap.GetData(), Stride, W, H, MoveTemp(Frames));

	Registry->DriveAll(Egress->GetSeam()); Egress->PumpGameThreadUploads(); /* split — DriveAll worker-emits (copy+store), Pump does the UObject+upload */ FlushRenderingCommands(); // frame 1 (full)
	Registry->DriveAll(Egress->GetSeam()); Egress->PumpGameThreadUploads(); /* split — DriveAll worker-emits (copy+store), Pump does the UObject+upload */ FlushRenderingCommands(); // frame 2 (sub-rect)

	TestEqual(TEXT("[WIN64-EXECUTED] two single-region uploads over two change-frames"), Spy.Count, 2);
	TestTrue(TEXT("[WIN64-EXECUTED] every upload ran on the render thread"), Spy.bInRenderThread);
	if (Spy.Regions.Num() == 2 && Spy.ByteCounts.Num() == 2)
	{
		// frame 1: the full texture (origin 0,0, full dims, tight W*H*4 bytes)
		TestEqual(TEXT("frame-1 region origin == (0,0)"), static_cast<int32>(Spy.Regions[0].DestX + Spy.Regions[0].DestY), 0);
		TestEqual(TEXT("frame-1 region width == full"), static_cast<int32>(Spy.Regions[0].Width), W);
		TestEqual(TEXT("frame-1 copy bytes == the full frame (tight W*H*4)"), Spy.ByteCounts[0], W * H * 4);
		// frame 2: the dirty-rect-only discriminator — region == R (an offset sub-rect, NOT the full texture)
		TestEqual(TEXT("[WIN64-EXECUTED] frame-2 region DestX == R.Min.X (offset)"), static_cast<int32>(Spy.Regions[1].DestX), R.Min.X);
		TestEqual(TEXT("[WIN64-EXECUTED] frame-2 region DestY == R.Min.Y (offset)"), static_cast<int32>(Spy.Regions[1].DestY), R.Min.Y);
		TestEqual(TEXT("[WIN64-EXECUTED] frame-2 region width == R width"), static_cast<int32>(Spy.Regions[1].Width), R.Width());
		TestEqual(TEXT("[WIN64-EXECUTED] frame-2 region height == R height"), static_cast<int32>(Spy.Regions[1].Height), R.Height());
		TestNotEqual(TEXT("[WIN64-EXECUTED] frame-2 region is NOT the full texture (dirty-rect-only)"), static_cast<int32>(Spy.Regions[1].Width), W);
		// frame 2: the COPY is bounded to R too (tight R.W*R.H*4), NOT the full frame
		TestEqual(TEXT("[WIN64-EXECUTED] frame-2 copy bytes bounded to R (tight R.W*R.H*4)"), Spy.ByteCounts[1], R.Width() * R.Height() * 4);
		TestNotEqual(TEXT("frame-2 copy bytes are NOT the full-frame size (the copy bound)"), Spy.ByteCounts[1], W * H * 4);
	}
	else
	{
		AddError(TEXT("[024-EGRESS-REGIONAL-SPY] expected exactly two single-region uploads"));
	}

	const FUpdateTextureRegion2D F1 = (Spy.Regions.Num() > 0) ? Spy.Regions[0] : FUpdateTextureRegion2D(0u, 0u, 0, 0, 0u, 0u);
	const FUpdateTextureRegion2D F2 = (Spy.Regions.Num() > 1) ? Spy.Regions[1] : FUpdateTextureRegion2D(0u, 0u, 0, 0, 0u, 0u);
	const int32 B1 = (Spy.ByteCounts.Num() > 0) ? Spy.ByteCounts[0] : -1;
	const int32 B2 = (Spy.ByteCounts.Num() > 1) ? Spy.ByteCounts[1] : -1;
	AddInfo(FString::Printf(
		TEXT("[WIN64-EXECUTED][024-EGRESS-REGIONAL-SPY] count=%d rt=%d f1=%ux%u@(%u,%u) bytes=%d | f2=%ux%u@(%u,%u) bytes=%d (R=%dx%d@(%d,%d) full-tight=%d)"),
		Spy.Count, Spy.bInRenderThread ? 1 : 0, F1.Width, F1.Height, F1.DestX, F1.DestY, B1, F2.Width, F2.Height, F2.DestX, F2.DestY, B2,
		R.Width(), R.Height(), R.Min.X, R.Min.Y, W * H * 4));

	// on-change: an idle drive (frames exhausted) -> no additional upload
	Registry->DriveAll(Egress->GetSeam()); Egress->PumpGameThreadUploads(); /* split — DriveAll worker-emits (copy+store), Pump does the UObject+upload */ FlushRenderingCommands();
	TestEqual(TEXT("[WIN64-EXECUTED] an idle drive invoked the uploader ZERO additional times"), Spy.Count, 2);
	return true;
#else
	AddError(TEXT("Cathode.Egress.Regional.DirtyRectRegionIsSubRect requires Win64."));
	return false;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeEgressRegionalReadbackTest,
	"Cathode.Egress.Regional.SubRectContentAndUnchangedIntact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeEgressRegionalReadbackTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
	const int32 W = 8;
	const int32 H = 8;
	const int32 Stride = W * 4 + 16;     // padded — frame-1's per-row-distinct A binds the copy's strided read
	const FIntRect R(2, 2, 6, 6);        // 4x4 at (2,2)
	TArray<uint8> Bitmap;
	FillDistinctPattern(Bitmap, Stride, W, H); // pattern A: R-dominant, per-row-distinct

	TUniquePtr<ITextureEgress> Egress = CreateTextureEgress(); // production
	TUniquePtr<ISurfaceRegistry> Registry = CreateSurfaceRegistry();
	TArray<TArray<FIntRect>> Frames;
	Frames.Add({ FIntRect(0, 0, W, H) }); // frame 1: full A
	Frames.Add({ R });                    // frame 2: only R
	const FSurfaceId Surface = RegisterSequenced(*Registry, Bitmap.GetData(), Stride, W, H, MoveTemp(Frames));

	Registry->DriveAll(Egress->GetSeam()); Egress->PumpGameThreadUploads(); /* split — DriveAll worker-emits (copy+store), Pump does the UObject+upload */ FlushRenderingCommands(); // frame 1: upload the full A
	FillSubRect(Bitmap, Stride, R);                                  // change ONLY R -> pattern B (B-dominant)
	Registry->DriveAll(Egress->GetSeam()); Egress->PumpGameThreadUploads(); /* split — DriveAll worker-emits (copy+store), Pump does the UObject+upload */ FlushRenderingCommands(); // frame 2: upload ONLY R (now B)

	UTexture2D* Texture = Egress->GetTexture(Surface);
	if (Texture == nullptr) { AddError(TEXT("[024-EGRESS-REGIONAL] no egress texture")); return false; }
	FTextureResource* Resource = Texture->GetResource();
	if (Resource == nullptr) { AddError(TEXT("[024-EGRESS-REGIONAL] no RHI resource")); return false; }

	TArray<FColor> Readback;
	ENQUEUE_RENDER_COMMAND(CathodeEgressRegionalReadback)(
		[Resource, W, H, &Readback](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* TexRHI = Resource->GetTexture2DRHI();
			if (TexRHI != nullptr) { RHICmdList.ReadSurfaceData(TexRHI, FIntRect(0, 0, W, H), Readback, FReadSurfaceDataFlags(RCM_UNorm)); }
		});
	FlushRenderingCommands();
	if (Readback.Num() != W * H) { AddError(FString::Printf(TEXT("[024-EGRESS-REGIONAL] readback %d != %d"), Readback.Num(), W * H)); return false; }

	int32 RPixels = 0, NonRPixels = 0, RMatchB = 0, NonRMatchA = 0;
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const FColor& Px = Readback[Y * W + X];
			const bool bInR = (X >= R.Min.X && X < R.Max.X && Y >= R.Min.Y && Y < R.Max.Y);
			if (bInR)
			{
				++RPixels;
				if (Px.B == 200 && Px.G == 80 && Px.R == 40) { ++RMatchB; } // the new sub-rect content (B-dominant)
			}
			else
			{
				++NonRPixels;
				const int32 S = Y * Stride + X * 4;
				if (Px.B == Bitmap[S + 0] && Px.G == Bitmap[S + 1] && Px.R == Bitmap[S + 2]) { ++NonRMatchA; } // retained per-row A
			}
		}
	}
	AddInfo(FString::Printf(
		TEXT("[WIN64-EXECUTED][024-EGRESS-REGIONAL] R(sub-rect)-match-B=%d/%d  nonR-retains-A=%d/%d  (R=%dx%d@(%d,%d) stride=%d)"),
		RMatchB, RPixels, NonRMatchA, NonRPixels, R.Width(), R.Height(), R.Min.X, R.Min.Y, Stride));

	TestEqual(TEXT("[WIN64-EXECUTED] the dirty sub-rect R received the new content B at the right offset"), RMatchB, RPixels);
	TestEqual(TEXT("[WIN64-EXECUTED] the UNCHANGED region retained frame-1's per-row content A (only R was uploaded)"), NonRMatchA, NonRPixels);
	return true;
#else
	AddError(TEXT("Cathode.Egress.Regional.SubRectContentAndUnchangedIntact requires Win64 (the box's real D3D RHI)."));
	return false;
#endif
}

// The lock-free triple-buffer SLOT LOGIC (single-threaded). The CONCURRENCY (whole-frame-or-nothing) is proven by
// the release/acquire fence-pairing ARGUMENT documented in CathodeTripleBuffer.h, NOT a stress race (concurrency
// is proven by the fence ORDERING). This test proves the slot ROTATION the fence ordering rides: no-publish ->
// Acquire false; publish -> Acquire the WHOLE value intact; publish-twice -> the LATEST (intermediate dropped); acquire
// with no new publish -> false (no stale re-read). A multi-field value (Id + a heap TArray + Tag) bites a partial move.
namespace
{
	struct FTripleTestFrame
	{
		int32 Id = 0;
		TArray<int32> Data; // a HEAP field — a partial/torn move would leave it inconsistent with the scalars
		int32 Tag = 0;
	};
	FTripleTestFrame MakeTripleTestFrame(int32 Id, int32 Tag)
	{
		FTripleTestFrame F;
		F.Id = Id;
		F.Tag = Tag;
		F.Data = { Id, Id + 1, Id + 2 };
		return F;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeTripleBufferSlotLogicTest,
	"Cathode.Egress.TripleBuffer.SlotLogic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeTripleBufferSlotLogicTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;
	FTripleBuffer<FTripleTestFrame> Buf;
	FTripleTestFrame Out;

	// (1) empty -> Acquire returns false (no published value).
	TestFalse(TEXT("[029-#24] empty triple-buffer: Acquire returns false (nothing published)"), Buf.Acquire(Out));

	// (2) publish one -> Acquire returns it, WHOLE (every field together — a partial move would mismatch).
	Buf.Publish(MakeTripleTestFrame(7, 70));
	TestTrue(TEXT("[029-#24] after one publish, Acquire returns true"), Buf.Acquire(Out));
	TestEqual(TEXT("[029-#24] the acquired Id is intact (7)"), Out.Id, 7);
	TestEqual(TEXT("[029-#24] the acquired Tag is intact (70)"), Out.Tag, 70);
	TestTrue(TEXT("[029-#24] the acquired HEAP field is intact ({7,8,9}) — whole-value move"), Out.Data == TArray<int32>({ 7, 8, 9 }));

	// (3) a 2nd Acquire with NO new publish -> false (no stale re-read of the last value).
	TestFalse(TEXT("[029-#24] a 2nd Acquire with no new publish returns false (no stale re-read)"), Buf.Acquire(Out));

	// (4) publish TWICE without acquiring -> Acquire returns the LATEST (the intermediate is dropped — latest-wins).
	Buf.Publish(MakeTripleTestFrame(11, 110));
	Buf.Publish(MakeTripleTestFrame(22, 220));
	TestTrue(TEXT("[029-#24] after two publishes, Acquire returns true"), Buf.Acquire(Out));
	TestEqual(TEXT("[029-#24] Acquire returns the LATEST (22, not the dropped 11)"), Out.Id, 22);
	TestEqual(TEXT("[029-#24] the latest Tag (220)"), Out.Tag, 220);
	TestTrue(TEXT("[029-#24] the latest HEAP field ({22,23,24})"), Out.Data == TArray<int32>({ 22, 23, 24 }));

	// (5) a 3rd cycle -> the 3 slots keep rotating correctly (no exhaustion / aliasing across cycles).
	Buf.Publish(MakeTripleTestFrame(33, 330));
	TestTrue(TEXT("[029-#24] a 3rd publish/acquire cycle acquires"), Buf.Acquire(Out));
	TestEqual(TEXT("[029-#24] the 3rd cycle value (33)"), Out.Id, 33);
	TestFalse(TEXT("[029-#24] and is then empty again (no stale)"), Buf.Acquire(Out));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
