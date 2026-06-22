#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CathodeEgress.h"
#include "CathodeEgressUploader.h"
#include "CathodeSurfaces.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIResources.h"

// 024 slice 1b — the Texture Egress production path. TWO proofs, BOTH riding DriveAll(egress.GetSeam()) end-to-end
// (the egress's own copy + marshal + upload made the result; the test NEVER calls RHIUpdateTexture2D, and reads the
// EGRESS's own UTexture2D — a test-built texture / a re-hand-upload would prove the MECHANISM, not the egress):
//
//  - THE READBACK (the HARD GATE): read the egress-uploaded UTexture2D's GPU pixels back and compare to the source
//    bitmap. Binds source + region + pitch + FORMAT together via the on-GPU result. A DISTINCT-channel pattern
//    (R=200 dominant, B=40 low) makes a wrong format-map (B<->R, PF_B8G8R8A8 vs PF_R8G8B8A8) read R-dominant as
//    blue -> RED. Win64 + the real D3D RHI (the desktop session) — the de-risked slice-0 path.
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
	// second drive is idle — for the on-change gate). The dirty rect is the full surface (the 1b full-texture case).
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

	// A 3-distinct-channel pattern: B=40, G=120, R=200 (R dominant, clearly != B) — binds ANY channel permutation,
	// not just B<->R. Laid out at the given (possibly padded) stride.
	void FillDistinctPattern(TArray<uint8>& Bytes, int32 Stride, int32 W, int32 H)
	{
		Bytes.SetNumZeroed(H * Stride);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const int32 I = Y * Stride + X * 4;
				Bytes[I + 0] = 40;  // B
				Bytes[I + 1] = 120; // G
				Bytes[I + 2] = 200; // R
				Bytes[I + 3] = 255; // A
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
	Registry->DriveAll(Egress->GetSeam());
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
	Registry->DriveAll(Egress->GetSeam());
	FlushRenderingCommands();

	// === The spy's UNIQUE load-bearing gates (the readback cannot see these) ===
	TestEqual(TEXT("[WIN64-EXECUTED] the egress invoked the uploader exactly once on a real change"), Spy.Count, 1);
	TestTrue(TEXT("[WIN64-EXECUTED] the upload ran ON the render thread (IsInRenderingThread at the upload)"), Spy.bInRenderThread);

	// === Corroborating DIAGNOSTIC: egress-assembled vs the independently-KNOWN document (not test-fed -> not circular) ===
	TestEqual(TEXT("[diag] the egress source pitch == the neutral-view Stride (not Width*4 on a padded buffer)"), static_cast<int32>(Spy.Pitch), Stride);
	TestNotEqual(TEXT("[diag] the source pitch is NOT the naive Width*4 (the padded-stride trap)"), static_cast<int32>(Spy.Pitch), W * 4);
	TestEqual(TEXT("[diag] the upload region width == the (full) dirty rect"), static_cast<int32>(Spy.RegionWidth), W);
	TestEqual(TEXT("[diag] the upload region height == the (full) dirty rect"), static_cast<int32>(Spy.RegionHeight), H);
	TestEqual(TEXT("[diag] the upload region origin == the dirty rect origin (0,0)"), static_cast<int32>(Spy.RegionDestX + Spy.RegionDestY), 0);
	TestEqual(TEXT("[diag] the egress surface id == the driven surface"), Spy.Surface.Value, Surface.Value);

	bool bBytesMatch = (Spy.Bytes.Num() == H * Stride);
	if (bBytesMatch) { bBytesMatch = (FMemory::Memcmp(Spy.Bytes.GetData(), Source.GetData(), H * Stride) == 0); }
	TestTrue(TEXT("[diag] the egress's copied source bytes == the known document's dirty bytes"), bBytesMatch);

	AddInfo(FString::Printf(
		TEXT("[WIN64-EXECUTED][024-EGRESS-SPY] count=%d renderThread=%d pitch=%u (stride=%d width*4=%d) region=%ux%u@(%u,%u) bytes=%d/%d surface=%d"),
		Spy.Count, Spy.bInRenderThread ? 1 : 0, Spy.Pitch, Stride, W * 4, Spy.RegionWidth, Spy.RegionHeight, Spy.RegionDestX, Spy.RegionDestY, Spy.Bytes.Num(), H * Stride, Spy.Surface.Value));

	// ON-CHANGE (the spy's unique job): an IDLE drive (no surface change) must NOT invoke the uploader. A readback
	// would read the stale-but-correct texture and pass trivially -> only the spy binds idle.
	Registry->DriveAll(Egress->GetSeam());
	FlushRenderingCommands();
	TestEqual(TEXT("[WIN64-EXECUTED] an idle drive (no surface change) invoked the uploader ZERO additional times"), Spy.Count, 1);
	return true;
#else
	AddError(TEXT("Cathode.Egress.SeamSpy.AssemblesAndGatesUpload requires Win64 (UTexture2D RHI resource creation on the box)."));
	return false;
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
