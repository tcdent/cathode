#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeComposition.h"          // MakeCompositionSurfaceRegistration
#include "CathodeShaperTextMeasurer.h"   // the standalone Layout (to read the Layout-computed box)

#include "CathodeLayout.h"   // ILayoutEngine, FLayoutResult, FLayoutPaintOp, ILayoutDocumentSource
#include "CathodeRaster.h"   // FBitmapData (the real Blend2D bitmap the opaque Buffer.Handle resolves to)
#include "CathodeSurfaces.h" // FSurface, FSurfaceId, FSurfaceReadiness, FSurfaceReadinessSeam

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

// 023 slice 1 — the COMPOSITION ROOT drives ONE composed frame tick end-to-end: FSurface::RenderAndRelay -> the
// REAL Layout (ISurfaceDocument backer, RenderFrame -> paint ops) -> the REAL Rasterizer (ISurfaceRenderTarget
// backer, ProduceFrame -> the Blend2D bitmap) -> GetReadyFrame -> the published front buffer. A known inline,
// NON-TEXT document (a div at a NON-TRIVIAL Layout offset, with a bg color) rasterizes to the color AT its
// Layout-computed box AND blank elsewhere.
//
// THE DISCRIMINATOR IS THE WIN64 CONTENT (the structure is secondary/vacuous — a stub-backed seam also makes
// RenderAndRelay-true + the relay fire). The content (color at the LAYOUT-DERIVED box, read from the real layout
// result, not hardcoded) bites: a no-render (blank -> RED), a fill-everything (color outside -> RED), a
// direct-raster / wrong-position (color at (0,0), not the offset box -> RED). Blend2D + HarfBuzz + litehtml are
// Win64-only in the plugin, so the composed RUNTIME tick is Win64-gated (#if PLATFORM_WINDOWS; the #else
// AddErrors — no vacuous #if'd-out green); the cross-platform STRUCTURAL proof is the module compiling + linking
// (the build itself). The [WIN64-EXECUTED] marker emits the in-box/out-box pixel counts only the real render
// produces. The pixel read is the REAL Blend2D FBitmapData via the opaque Buffer.Handle (the 022 GetData analog
// at the Surface level), not a recording fake.

namespace
{
	// A minimal inline document: a div at a NON-TRIVIAL, deterministic offset (absolute left/top 20) sized 40x40
	// with a red background and NO text. The Layout-computed box is (20,20)-(60,60) — the offset bites a
	// direct-raster (which would fill at (0,0)). The styling is provided BOTH inline (<style>) AND via
	// GetDocumentCss so the box lays out regardless of which stylesheet seam the engine feeds (the rules are
	// identical -> idempotent cascade).
	static const TCHAR* BoxCss()
	{
		return TEXT("html,body{margin:0;padding:0;} ")
			TEXT("#box{position:absolute;left:20px;top:20px;width:40px;height:40px;background-color:#ff0000;}");
	}
	class FInlineBoxDocument final : public Cathode::ILayoutDocumentSource
	{
	public:
		virtual int32 GetRootNode() const override { return 0; }
		virtual int32 GetParentNode(int32 /*NodeId*/) const override { return INDEX_NONE; }
		virtual void GetChildNodes(int32 /*NodeId*/, TArray<int32>& OutChildren) const override { OutChildren.Reset(); }
		virtual bool IsLayoutBoundary(int32 /*NodeId*/) const override { return true; }

		virtual FString GetDocumentHtml() const override
		{
			return FString::Printf(TEXT("<html><head><style>%s</style></head><body><div id=\"box\"></div></body></html>"), BoxCss());
		}
		virtual FString GetSubtreeHtml(int32 /*NodeId*/) const override { return FString(); }
		virtual FString GetDocumentCss() const override { return BoxCss(); }
	};

	bool IsRedPixel(const uint8* Px, int32 Stride, int32 X, int32 Y)
	{
		// PremultipliedBGRA8: bytes [B,G,R,A]. Opaque red (#ff0000) -> B=0,G=0,R=255,A=255.
		const uint8* P = Px + Y * Stride + X * 4;
		return P[2] > 200 && P[1] < 60 && P[0] < 60 && P[3] > 100;
	}

	bool IsInkPixel(const uint8* Px, int32 Stride, int32 X, int32 Y)
	{
		return (Px + Y * Stride + X * 4)[3] > 0; // any non-transparent pixel
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeCompositionTickTest,
	"Cathode.Composition.Tick.ComposedFrameRendersDocumentBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeCompositionTickTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

#if PLATFORM_WINDOWS
	// Reuse 021's committed OFL fixture (the measurer's injected font; not text-exercised in slice 1).
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
	if (!Plugin.IsValid()) { AddError(TEXT("could not locate the Cathode plugin base dir (IPluginManager)")); return false; }
	const FString FontPath = Plugin->GetBaseDir() / TEXT("Source/CathodeText/Private/Tests/Fixtures/LiberationSans-Regular.ttf");
	if (!FPaths::FileExists(FontPath)) { AddError(FString::Printf(TEXT("the OFL font fixture is not on disk: %s"), *FontPath)); return false; }

	const int32 W = 128;
	const int32 H = 128;
	FInlineBoxDocument Document; // MUST outlive the surface (the document backer holds it by reference)

	// --- Read the Layout-COMPUTED box from the REAL layout (the bg fill op's Rect) — NOT a test-hardcoded box. ---
	FBox2f LayoutBox(ForceInit);
	bool bFoundBox = false;
	{
		FShaperTextMeasurer Measurer(FontPath);
		TUniquePtr<ILayoutEngine> Engine = CreateLiteHtmlLayoutEngine(Measurer);
		if (!Engine.IsValid()) { AddError(TEXT("standalone layout engine construction failed")); return false; }
		Engine->SetDocument(Document);
		const FLayoutResult Result = Engine->Relayout(W);
		for (const FLayoutPaintOp& Op : Result.PaintOps)
		{
			const bool bFill = (Op.Kind == ELayoutPaintOpKind::FillRect || Op.Kind == ELayoutPaintOpKind::FillBox);
			if (bFill && Op.Color.R == 255 && Op.Color.G == 0 && Op.Color.B == 0)
			{
				LayoutBox = Op.Rect;
				bFoundBox = true;
				break;
			}
		}
	}
	if (!bFoundBox) { AddError(TEXT("the real Layout produced no red bg fill op for the div — the document did not lay out")); return false; }
	// The box must be at a NON-TRIVIAL Layout offset (the margin applied) — a direct-raster at (0,0) would not match.
	TestTrue(FString::Printf(TEXT("the Layout-computed box is offset by the margin (Min=(%.1f,%.1f), expect ~(20,20))"), LayoutBox.Min.X, LayoutBox.Min.Y),
		LayoutBox.Min.X >= 15.0f && LayoutBox.Min.Y >= 15.0f);

	// --- Drive ONE composed tick via the registry — the EXPORTED production drive path (the device-spawn path
	// Register's a surface and DriveAll's it; DriveAll internally pumps each surface's RenderAndRelay -> real
	// Layout -> real Rasterizer -> GetReadyFrame). We drive through the registry (not FSurface directly) because
	// CreateSurfaceRegistry + the ISurfaceRegistry virtuals are CathodeSurfaces' exported entry; this needs no
	// widening of CathodeSurfaces' surface. ---
	TUniquePtr<ISurfaceRegistry> Registry = CreateSurfaceRegistry();
	if (!Registry.IsValid()) { AddError(TEXT("surface registry construction failed")); return false; }
	FSurfaceRegistration Registration = MakeCompositionSurfaceRegistration(Document, W, H, ESurfacePixelFormat::PremultipliedBGRA8, FontPath);
	const FSurfaceId SurfaceId = Registry->Register(MoveTemp(Registration));

	FSurfaceReadiness Captured;
	bool bRelayed = false;
	FSurfaceReadinessSeam Egress = [&Captured, &bRelayed](const FSurfaceReadiness& Readiness)
	{
		Captured = Readiness;
		bRelayed = true;
	};
	Registry->DriveAll(Egress);

	// STRUCTURE (secondary — a stub-backed seam would also pass these; the CONTENT below is the discriminator):
	TestTrue(TEXT("the readiness relay fired (a real change on the first drive)"), bRelayed);
	TestEqual(TEXT("the readiness is surface-addressed to the registered surface"), Captured.Surface.Value, SurfaceId.Value);
	TestTrue(TEXT("the ready frame carries a valid front-buffer ref"), Captured.Frame.Buffer.IsValid());

	// CONTENT (the load-bearing discriminator) — resolve the opaque Buffer.Handle to the REAL Blend2D FBitmapData.
	const FBitmapData* Bitmap = static_cast<const FBitmapData*>(Captured.Frame.Buffer.Handle);
	if (Bitmap == nullptr || Bitmap->Pixels == nullptr) { AddError(TEXT("the ready front buffer resolved to null pixels")); return false; }
	const uint8* Px = static_cast<const uint8*>(Bitmap->Pixels);
	const int32 Stride = Bitmap->Stride;

	const int32 BoxL = FMath::Clamp(FMath::FloorToInt(LayoutBox.Min.X), 0, W - 1);
	const int32 BoxT = FMath::Clamp(FMath::FloorToInt(LayoutBox.Min.Y), 0, H - 1);
	const int32 BoxR = FMath::Clamp(FMath::CeilToInt(LayoutBox.Max.X), 1, W);
	const int32 BoxB = FMath::Clamp(FMath::CeilToInt(LayoutBox.Max.Y), 1, H);

	// (a) EXISTS red INSIDE the Layout-computed box.
	int32 RedInBox = 0;
	for (int32 Y = BoxT; Y < BoxB; ++Y) { for (int32 X = BoxL; X < BoxR; ++X) { if (IsRedPixel(Px, Stride, X, Y)) { ++RedInBox; } } }

	// (b) ALL pixels in a guaranteed-OUTSIDE margin (box +/- gap) are blank (no ink leaked / not mis-positioned).
	const int32 Gap = 8;
	const int32 OutL = FMath::Clamp(BoxL - Gap, 0, W);
	const int32 OutT = FMath::Clamp(BoxT - Gap, 0, H);
	const int32 OutR = FMath::Clamp(BoxR + Gap, 0, W);
	const int32 OutB = FMath::Clamp(BoxB + Gap, 0, H);
	int32 InkOutside = 0;
	int32 RedOutside = 0;
	FString StraySamples; // first few outside-ink pixels (coord + B,G,R,A) — to identify what is drawn outside
	int32 StrayLogged = 0;
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const bool bInsideGapped = (X >= OutL && X < OutR && Y >= OutT && Y < OutB);
			if (bInsideGapped || !IsInkPixel(Px, Stride, X, Y)) { continue; }
			++InkOutside;
			if (IsRedPixel(Px, Stride, X, Y)) { ++RedOutside; }
			if (StrayLogged < 12)
			{
				const uint8* P = Px + Y * Stride + X * 4;
				StraySamples += FString::Printf(TEXT(" (%d,%d)=B%uG%uR%uA%u"), X, Y, P[0], P[1], P[2], P[3]);
				++StrayLogged;
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("[WIN64-EXECUTED][023-TICK] composed tick: red-in-box=%d (layout-box x[%d,%d] y[%d,%d]) red-outside=%d ink-outside=%d (gap=%d) | relayed=%d dims=%dx%d"),
		RedInBox, BoxL, BoxR, BoxT, BoxB, RedOutside, InkOutside, Gap, bRelayed ? 1 : 0, Bitmap->Width, Bitmap->Height));
	if (InkOutside > 0) { AddInfo(FString::Printf(TEXT("[023-TICK][diag] outside-ink samples:%s"), *StraySamples)); }

	// (a) the composed render produced the document's color AT its Layout-computed box (a no-render -> 0 -> RED).
	TestTrue(FString::Printf(TEXT("[WIN64-EXECUTED] the composed tick rendered the div's color IN its Layout-computed box (red-in-box=%d > 0)"), RedInBox), RedInBox > 0);
	// (b) blank everywhere outside the box (a fill-everything / mis-positioned render -> ink outside -> RED).
	TestEqual(FString::Printf(TEXT("the composed render left the outside-the-box margin blank (ink-outside=%d, expect 0)"), InkOutside), InkOutside, 0);

	return true;
#else
	// Blend2D + HarfBuzz + litehtml are Win64-only in the plugin, so the composed RUNTIME tick (the render proof)
	// cannot execute off-Win64. Do NOT vacuously pass on a structure-only path — the render content is the proof.
	AddError(TEXT("Cathode.Composition.Tick.ComposedFrameRendersDocumentBox requires Win64 (Blend2D/HarfBuzz/litehtml are Win64-only); the composed render did not execute on this platform — a non-Win64 pass would be vacuous."));
	return false;
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
