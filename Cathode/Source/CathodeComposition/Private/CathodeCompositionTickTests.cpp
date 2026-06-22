#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeComposition.h"          // MakeCompositionSurfaceRegistration
#include "CathodeShaperTextMeasurer.h"   // the standalone Layout (to read the Layout-computed box)

#include "CathodeLayout.h"   // ILayoutEngine, FLayoutResult, FLayoutPaintOp, ILayoutDocumentSource
#include "CathodeRaster.h"   // FBitmapData (the real Blend2D bitmap the opaque Buffer.Handle resolves to)
#include "CathodeResolver.h" // IBundleSource, IResourceResolver, CreateUEFileBundleSource, CreateResourceResolver (slice 3)
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

	bool IsBluePixel(const uint8* Px, int32 Stride, int32 X, int32 Y)
	{
		// PremultipliedBGRA8: bytes [B,G,R,A]. Opaque blue (#0000ff) -> B=255,G=0,R=0,A=255.
		const uint8* P = Px + Y * Stride + X * 4;
		return P[0] > 200 && P[1] < 60 && P[2] < 60 && P[3] > 100;
	}

	// The slice-2 TEXT document: an absolute, SHRINK-TO-FIT div (width:auto) with a BLUE background + RED text.
	// The div's box width = the MEASURED text width (ITextMeasurer::MeasureWidth, the adapter) — so the BLUE bg's
	// horizontal extent is the adapter's measured width OBSERVABLE in the bitmap; the RED glyphs are the shaper's
	// output. The two colors discriminate the two halves: a stub-measurer (wrong width) collapses the blue box
	// (no blue at the right) while the red glyphs still render at their shaped positions -> the blue-extent
	// assertion REDs (the adapter), independent of the glyph ink (the shaper). Default OFL font (no font-family ->
	// no catalog needed; leaf-clean). Provided both inline (<style>) and via GetDocumentCss (idempotent).
	static const TCHAR* TextCss()
	{
		return TEXT("html,body{margin:0;padding:0;} ")
			TEXT("#t{position:absolute;left:20px;top:20px;font-size:48px;color:#ff0000;background-color:#0000ff;}");
	}
	class FInlineTextDocument final : public Cathode::ILayoutDocumentSource
	{
	public:
		virtual int32 GetRootNode() const override { return 0; }
		virtual int32 GetParentNode(int32 /*NodeId*/) const override { return INDEX_NONE; }
		virtual void GetChildNodes(int32 /*NodeId*/, TArray<int32>& OutChildren) const override { OutChildren.Reset(); }
		virtual bool IsLayoutBoundary(int32 /*NodeId*/) const override { return true; }

		virtual FString GetDocumentHtml() const override
		{
			return FString::Printf(TEXT("<html><head><style>%s</style></head><body><div id=\"t\">MM</div></body></html>"), TextCss());
		}
		virtual FString GetSubtreeHtml(int32 /*NodeId*/) const override { return FString(); }
		virtual FString GetDocumentCss() const override { return TextCss(); }
	};

	bool IsGreenPixel(const uint8* Px, int32 Stride, int32 X, int32 Y)
	{
		// PremultipliedBGRA8: bytes [B,G,R,A]. Opaque green (#00ff00) -> B=0,G=255,R=0,A=255.
		const uint8* P = Px + Y * Stride + X * 4;
		return P[1] > 200 && P[0] < 60 && P[2] < 60 && P[3] > 100;
	}

	// The slice-3 LINKED-RESOURCE document: @import's the PRODUCTION-staged slice3.css (which sets a distinct green
	// box). The green rule lives ONLY in the external fixture — NOT inline, NOT GetDocumentCss — so the rendered
	// green proves the PRODUCTION resolver LOADED the linked resource from the default-root. A null resolver -> the
	// @import never loads -> no green (the default; the null-resolver control).
	class FInlineLinkedDocument final : public Cathode::ILayoutDocumentSource
	{
	public:
		virtual int32 GetRootNode() const override { return 0; }
		virtual int32 GetParentNode(int32 /*NodeId*/) const override { return INDEX_NONE; }
		virtual void GetChildNodes(int32 /*NodeId*/, TArray<int32>& OutChildren) const override { OutChildren.Reset(); }
		virtual bool IsLayoutBoundary(int32 /*NodeId*/) const override { return true; }

		virtual FString GetDocumentHtml() const override
		{
			// A baseline inline box (always renders, regardless of the resolver) so a frame relays in BOTH the
			// resolver and null-control surfaces — the null-control then produces a real 0-green frame instead of
			// an empty no-paint-op frame (which would not relay). The linked green div's rule lives ONLY in the
			// external slice3.css, so it renders green ONLY with the production resolver. The baseline navy box is
			// at (4,4) — outside the green box region (24,24)-(64,64) and not green — so it never affects the count.
			return TEXT("<html><head><style>@import url('slice3.css');</style></head><body>")
				TEXT("<div style=\"position:absolute;left:4px;top:4px;width:6px;height:6px;background-color:#000080;\"></div>")
				TEXT("<div id=\"cathode-023-target\"></div>")
				TEXT("</body></html>");
		}
		virtual FString GetSubtreeHtml(int32 /*NodeId*/) const override { return FString(); }
		virtual FString GetDocumentCss() const override { return FString(); } // the green rule is ONLY in the external slice3.css
	};
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

// 023 slice 2 — the TEXT PATH through the composed tick. Exercises BOTH halves discriminatingly: the ADAPTER
// (ITextMeasurer::MeasureWidth -> the shrink-to-fit box's WIDTH, observable as the BLUE bg's right extent) AND the
// SHAPER + the 022 glyph-run map (the RED glyph ink). The two colors separate them: a stub-measurer collapses the
// blue box (no blue at the right) while the red glyphs still render -> the blue-extent assertion REDs the adapter
// independently of the shaper. The SAME-BYTES ANCHOR: one shared FShaperTextMeasurer (one FFontHandle) threads
// MeasureWidth (Layout) + ShapeText (the mapper) + the bytes (DrawGlyphRun). Composed through FSurface::DriveAll ->
// real Layout (measure+position) -> the mapper (shape) -> real Rasterizer (DrawGlyphRun), NOT 022's isolation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeCompositionTextTickTest,
	"Cathode.Composition.Tick.ComposedFrameRendersTextAtMeasuredWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeCompositionTextTickTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

#if PLATFORM_WINDOWS
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
	if (!Plugin.IsValid()) { AddError(TEXT("could not locate the Cathode plugin base dir")); return false; }
	const FString FontPath = Plugin->GetBaseDir() / TEXT("Source/CathodeText/Private/Tests/Fixtures/LiberationSans-Regular.ttf");
	if (!FPaths::FileExists(FontPath)) { AddError(FString::Printf(TEXT("the OFL font fixture is not on disk: %s"), *FontPath)); return false; }

	const int32 W = 200;
	const int32 H = 96;
	FInlineTextDocument Document; // MUST outlive the surface (the document backer holds it by reference)

	// Read the BLUE shrink-to-fit box from the REAL layout — its WIDTH is the adapter's MEASURED text width.
	FBox2f BlueBox(ForceInit);
	bool bFoundBlue = false;
	bool bFoundText = false;
	{
		FShaperTextMeasurer Measurer(FontPath);
		TUniquePtr<ILayoutEngine> Engine = CreateLiteHtmlLayoutEngine(Measurer);
		if (!Engine.IsValid()) { AddError(TEXT("standalone layout engine construction failed")); return false; }
		Engine->SetDocument(Document);
		const FLayoutResult Result = Engine->Relayout(W);
		FLayoutPaintOp TextOp;
		FString OpSeq;
		for (const FLayoutPaintOp& Op : Result.PaintOps)
		{
			OpSeq += FString::Printf(TEXT("%d "), (int32)Op.Kind);
			const bool bFill = (Op.Kind == ELayoutPaintOpKind::FillRect || Op.Kind == ELayoutPaintOpKind::FillBox);
			if (bFill && Op.Color.R == 0 && Op.Color.G == 0 && Op.Color.B == 255) { BlueBox = Op.Rect; bFoundBlue = true; }
			if (Op.Kind == ELayoutPaintOpKind::DrawText && !Op.Text.IsEmpty()) { TextOp = Op; bFoundText = true; }
		}
		// DIAGNOSTIC (red-in-box=0): the DrawText op's color/position/font + the standalone shaping (mirrors the
		// mapper) — pins whether the color is non-red, the shaping fails, or the bytes are missing.
		if (bFoundText)
		{
			FShapedRun DiagShaped; float DiagSize = 0.0f;
			const bool bShaped = Measurer.ShapeText(TextOp.Font, TextOp.Text, DiagShaped, DiagSize);
			TArrayView<const uint8> DiagBytes;
			const bool bBytes = bShaped && Measurer.GetFontBytes(DiagShaped.Font, DiagBytes);
			AddInfo(FString::Printf(TEXT("[023-TEXT][diag] opseq=[%s] DrawText: color=R%uG%uB%uA%u pos=(%.1f,%.1f) fontId=%d textlen=%d | shaped=%d glyphs=%d sizepx=%.1f bytes=%d num=%d"),
				*OpSeq, TextOp.Color.R, TextOp.Color.G, TextOp.Color.B, TextOp.Color.A, TextOp.Position.X, TextOp.Position.Y, TextOp.Font.Id, TextOp.Text.Len(),
				bShaped ? 1 : 0, DiagShaped.Glyphs.Num(), DiagSize, bBytes ? 1 : 0, bBytes ? DiagBytes.Num() : 0));
		}
	}
	if (!bFoundBlue) { AddError(TEXT("the real Layout produced no blue shrink-to-fit bg box for the text div")); return false; }
	if (!bFoundText) { AddError(TEXT("the real Layout produced no DrawText op — the text did not lay out")); return false; }
	// The blue box width IS the adapter's measured text width — assert it is a REAL measure (a stub-zero-width
	// measurer would collapse it; this is the adapter EXERCISED, not slice-1's wired-not-exercised).
	const float MeasuredWidth = BlueBox.Max.X - BlueBox.Min.X;
	TestTrue(FString::Printf(TEXT("the shrink-to-fit box width is the adapter's MEASURED text width (>=40; got %.1f)"), MeasuredWidth), MeasuredWidth >= 40.0f);
	TestTrue(FString::Printf(TEXT("the box is at the non-trivial Layout offset (Min.X=%.1f)"), BlueBox.Min.X), BlueBox.Min.X >= 15.0f);

	// Drive the composed tick via the registry (the production drive path).
	TUniquePtr<ISurfaceRegistry> Registry = CreateSurfaceRegistry();
	if (!Registry.IsValid()) { AddError(TEXT("surface registry construction failed")); return false; }
	FSurfaceRegistration Registration = MakeCompositionSurfaceRegistration(Document, W, H, ESurfacePixelFormat::PremultipliedBGRA8, FontPath);
	const FSurfaceId SurfaceId = Registry->Register(MoveTemp(Registration));
	FSurfaceReadiness Captured;
	bool bRelayed = false;
	FSurfaceReadinessSeam Egress = [&Captured, &bRelayed](const FSurfaceReadiness& Readiness) { Captured = Readiness; bRelayed = true; };
	Registry->DriveAll(Egress);

	TestTrue(TEXT("the readiness relay fired"), bRelayed);
	TestTrue(TEXT("the ready frame carries a valid front-buffer ref"), Captured.Frame.Buffer.IsValid());

	const FBitmapData* Bitmap = static_cast<const FBitmapData*>(Captured.Frame.Buffer.Handle);
	if (Bitmap == nullptr || Bitmap->Pixels == nullptr) { AddError(TEXT("the ready front buffer resolved to null pixels")); return false; }
	const uint8* Px = static_cast<const uint8*>(Bitmap->Pixels);
	const int32 Stride = Bitmap->Stride;

	const int32 BoxL = FMath::Clamp(FMath::FloorToInt(BlueBox.Min.X), 0, W - 1);
	const int32 BoxT = FMath::Clamp(FMath::FloorToInt(BlueBox.Min.Y), 0, H - 1);
	const int32 BoxR = FMath::Clamp(FMath::CeilToInt(BlueBox.Max.X), 1, W);
	const int32 BoxB = FMath::Clamp(FMath::CeilToInt(BlueBox.Max.Y), 1, H);

	// (ADAPTER — observable in the bitmap, absolute threshold): the BLUE bg reaches the MEASURED width. Scan
	// x in [60, W): a real "MM"@48px measure (~80px) renders blue there; a stub-zero measure collapses the box ->
	// no blue past the origin -> RED. BLUE specifically (not red) — the glyphs render regardless of the measurer;
	// only the bg box's right extent reflects MeasureWidth.
	int32 BlueRight = 0;
	for (int32 Y = BoxT; Y < BoxB; ++Y) { for (int32 X = 60; X < W; ++X) { if (IsBluePixel(Px, Stride, X, Y)) { ++BlueRight; } } }

	// (SHAPER): EXISTS red glyph ink inside the box (the text shaped + rastered through the composed tick).
	int32 RedInBox = 0;
	for (int32 Y = BoxT; Y < BoxB; ++Y) { for (int32 X = BoxL; X < BoxR; ++X) { if (IsRedPixel(Px, Stride, X, Y)) { ++RedInBox; } } }

	// DIAGNOSTIC (red-in-box=0): in-box ink that is NOT the blue bg — a glyph rendered in a wrong color (black/AA
	// over blue) would show here; 0 means no glyph pixels rendered at all (op not produced/dispatched).
	int32 NonBlueInkInBox = 0;
	FString DarkSamples;
	int32 DarkLogged = 0;
	for (int32 Y = BoxT; Y < BoxB; ++Y)
	{
		for (int32 X = BoxL; X < BoxR; ++X)
		{
			if (IsInkPixel(Px, Stride, X, Y) && !IsBluePixel(Px, Stride, X, Y))
			{
				++NonBlueInkInBox;
				if (DarkLogged < 10) { const uint8* P = Px + Y * Stride + X * 4; DarkSamples += FString::Printf(TEXT(" (%d,%d)=B%uG%uR%uA%u"), X, Y, P[0], P[1], P[2], P[3]); ++DarkLogged; }
			}
		}
	}
	AddInfo(FString::Printf(TEXT("[023-TEXT][diag] in-box-non-blue ink=%d samples:%s"), NonBlueInkInBox, *DarkSamples));

	// (LOCALIZATION): blank outside the box + gap.
	const int32 Gap = 8;
	const int32 OutL = FMath::Clamp(BoxL - Gap, 0, W);
	const int32 OutT = FMath::Clamp(BoxT - Gap, 0, H);
	const int32 OutR = FMath::Clamp(BoxR + Gap, 0, W);
	const int32 OutB = FMath::Clamp(BoxB + Gap, 0, H);
	int32 InkOutside = 0;
	for (int32 Y = 0; Y < H; ++Y) { for (int32 X = 0; X < W; ++X) { const bool bIn = (X >= OutL && X < OutR && Y >= OutT && Y < OutB); if (!bIn && IsInkPixel(Px, Stride, X, Y)) { ++InkOutside; } } }

	AddInfo(FString::Printf(
		TEXT("[WIN64-EXECUTED][023-TEXT] composed text tick: blue-right(x>=60)=%d red-in-box=%d ink-outside=%d | measured-box x[%d,%d] y[%d,%d] width=%.1f relayed=%d dims=%dx%d"),
		BlueRight, RedInBox, InkOutside, BoxL, BoxR, BoxT, BoxB, MeasuredWidth, bRelayed ? 1 : 0, Bitmap->Width, Bitmap->Height));

	// (ADAPTER) the MEASURED width is observable in the bitmap: the blue bg reached it (a stub-measurer -> RED).
	TestTrue(FString::Printf(TEXT("[WIN64-EXECUTED] the blue bg reaches the MEASURED text width (blue past x=60: %d > 0)"), BlueRight), BlueRight > 0);
	// (SHAPER) the glyphs rendered inside the laid-out text box.
	TestTrue(FString::Printf(TEXT("[WIN64-EXECUTED] the shaper rendered red glyph ink in the text box (red-in-box=%d > 0)"), RedInBox), RedInBox > 0);
	// (LOCALIZATION) nothing rendered outside the box + gap.
	TestEqual(FString::Printf(TEXT("the composed text render left the outside-the-box margin blank (ink-outside=%d, expect 0)"), InkOutside), InkOutside, 0);

	return true;
#else
	AddError(TEXT("Cathode.Composition.Tick.ComposedFrameRendersTextAtMeasuredWidth requires Win64 (Blend2D/HarfBuzz/litehtml are Win64-only); the composed text render did not execute — a non-Win64 pass would be vacuous."));
	return false;
#endif
}

// 023 slice 3 — the A2 PRODUCTION RESOLVER through the composed tick. A linked-CSS document (@import slice3.css,
// whose green-box rule lives ONLY in the external production-staged fixture) renders the linked green box through
// the PRODUCTION resolver (CathodeComposition constructs CreateResourceResolver(CreateUEFileBundleSource(
// GetDefaultResourceRoot())) + SetResourceResolver — the production CALLER, over the production default-root
// /Game/Cathode, NOT a test-root). The NULL-RESOLVER CONTROL (bWireResolver=false) -> the @import never loads ->
// no green -> the discriminator. Plus the in-tree DISPOSITIVE gate: the default-root literal == the
// +DirectoriesToAlwaysStageAsUFS=/Game/Cathode staging path. (Cook-staging into the cooked pak is corroborated-
// inference — an editor run reads on-disk; the empirical packaged read is a deferred packaged-build pass.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeCompositionResolverTickTest,
	"Cathode.Composition.Tick.ComposedFrameRendersLinkedResource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeCompositionResolverTickTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

#if PLATFORM_WINDOWS
	// (1) THE DEFAULT-ROOT <-> STAGING MATCH (in-tree, dispositive): the production default-root literal MUST equal
	// the staging path. GetDefaultResourceRoot() = ProjectContentDir()/Cathode == /Game/Cathode (the
	// +DirectoriesToAlwaysStageAsUFS=/Game/Cathode entry; /Game/ is the ProjectContentDir mount). A mismatch would
	// ship a content root the cook can't stage.
	const FString DefaultRoot = GetDefaultResourceRoot();
	TestEqual(TEXT("the production default-root == ProjectContentDir()/Cathode (matches the +DirectoriesToAlwaysStageAsUFS=/Game/Cathode staging path)"), DefaultRoot, FPaths::ProjectContentDir() / TEXT("Cathode"));

	// The linked fixture must be on disk at the production default-root (editor: Content/Cathode/slice3.css).
	const FString LinkedCssPath = DefaultRoot / TEXT("slice3.css");
	if (!FPaths::FileExists(LinkedCssPath)) { AddError(FString::Printf(TEXT("the linked CSS fixture is not at the production default-root: %s"), *LinkedCssPath)); return false; }

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
	if (!Plugin.IsValid()) { AddError(TEXT("could not locate the Cathode plugin base dir")); return false; }
	const FString FontPath = Plugin->GetBaseDir() / TEXT("Source/CathodeText/Private/Tests/Fixtures/LiberationSans-Regular.ttf");
	if (!FPaths::FileExists(FontPath)) { AddError(FString::Printf(TEXT("the OFL font fixture is not on disk: %s"), *FontPath)); return false; }

	const int32 W = 128;
	const int32 H = 128;
	FInlineLinkedDocument Document; // MUST outlive the surfaces below

	// (2) Read the linked GREEN box from the layout WITH the production resolver (the linked rule loaded). Mirrors
	// the production caller's wiring: CreateUEFileBundleSource(DefaultRoot) -> CreateResourceResolver -> SetResourceResolver.
	FBox2f GreenBox(ForceInit);
	bool bFoundGreen = false;
	{
		FShaperTextMeasurer Measurer(FontPath);
		TUniquePtr<IBundleSource> BundleSource = CreateUEFileBundleSource(DefaultRoot);
		TUniquePtr<IResourceResolver> Resolver = BundleSource.IsValid() ? CreateResourceResolver(*BundleSource) : nullptr;
		TUniquePtr<ILayoutEngine> Engine = CreateLiteHtmlLayoutEngine(Measurer);
		if (!Engine.IsValid() || !Resolver.IsValid()) { AddError(TEXT("standalone resolver/engine construction failed")); return false; }
		Engine->SetResourceResolver(Resolver.Get());
		Engine->SetDocument(Document);
		const FLayoutResult Result = Engine->Relayout(W);
		for (const FLayoutPaintOp& Op : Result.PaintOps)
		{
			const bool bFill = (Op.Kind == ELayoutPaintOpKind::FillRect || Op.Kind == ELayoutPaintOpKind::FillBox);
			if (bFill && Op.Color.R == 0 && Op.Color.G == 255 && Op.Color.B == 0) { GreenBox = Op.Rect; bFoundGreen = true; }
		}
	}
	if (!bFoundGreen) { AddError(TEXT("the linked CSS rule did not load via the resolver — no green box in the layout result")); return false; }
	TestTrue(FString::Printf(TEXT("the linked box is at the linked offset (Min=(%.1f,%.1f), expect ~(24,24))"), GreenBox.Min.X, GreenBox.Min.Y), GreenBox.Min.X >= 18.0f && GreenBox.Min.Y >= 18.0f);

	// (3) Compose through the PRODUCTION caller + count green in the linked box: resolver (true) vs null control
	// (false). The count is read INSIDE the lambda while the registry (the FSurface + the captured bitmap) is alive.
	auto ComposeAndCountGreenInBox = [&](bool bWireResolver) -> int32
	{
		TUniquePtr<ISurfaceRegistry> Registry = CreateSurfaceRegistry();
		if (!Registry.IsValid()) { return -1; }
		FSurfaceRegistration Registration = MakeCompositionSurfaceRegistration(Document, W, H, ESurfacePixelFormat::PremultipliedBGRA8, FontPath, bWireResolver);
		Registry->Register(MoveTemp(Registration));
		FSurfaceReadiness Captured;
		bool bRelayed = false;
		Registry->DriveAll([&Captured, &bRelayed](const FSurfaceReadiness& R) { Captured = R; bRelayed = true; });
		if (!bRelayed || !Captured.Frame.Buffer.IsValid()) { return -1; }
		const FBitmapData* Bitmap = static_cast<const FBitmapData*>(Captured.Frame.Buffer.Handle);
		if (Bitmap == nullptr || Bitmap->Pixels == nullptr) { return -1; }
		const uint8* Px = static_cast<const uint8*>(Bitmap->Pixels);
		const int32 Stride = Bitmap->Stride;
		const int32 GL = FMath::Clamp(FMath::FloorToInt(GreenBox.Min.X), 0, W - 1);
		const int32 GT = FMath::Clamp(FMath::FloorToInt(GreenBox.Min.Y), 0, H - 1);
		const int32 GR = FMath::Clamp(FMath::CeilToInt(GreenBox.Max.X), 1, W);
		const int32 GB = FMath::Clamp(FMath::CeilToInt(GreenBox.Max.Y), 1, H);
		int32 Green = 0;
		for (int32 Y = GT; Y < GB; ++Y) { for (int32 X = GL; X < GR; ++X) { if (IsGreenPixel(Px, Stride, X, Y)) { ++Green; } } }
		return Green;
	};

	const int32 GreenWithResolver = ComposeAndCountGreenInBox(true);
	const int32 GreenNullControl = ComposeAndCountGreenInBox(false);

	AddInfo(FString::Printf(
		TEXT("[WIN64-EXECUTED][023-RESOLVER] composed linked-resource tick: green-in-box(resolver)=%d green-in-box(null-control)=%d | linked-box x[%.0f,%.0f] y[%.0f,%.0f] default-root=%s dims=%dx%d"),
		GreenWithResolver, GreenNullControl, GreenBox.Min.X, GreenBox.Max.X, GreenBox.Min.Y, GreenBox.Max.Y, *DefaultRoot, W, H));

	if (GreenWithResolver < 0 || GreenNullControl < 0) { AddError(TEXT("a composed linked-resource tick failed to produce a ready bitmap")); return false; }
	// (PRODUCTION RESOLVER) the linked CSS's green box rendered through the composed tick at the laid-out position.
	TestTrue(FString::Printf(TEXT("[WIN64-EXECUTED] the linked resource's green rendered via the PRODUCTION resolver (green-in-box=%d > 0)"), GreenWithResolver), GreenWithResolver > 0);
	// (NULL-RESOLVER CONTROL) without the resolver the @import never loads -> no green (the default) -> RED if leaked.
	TestEqual(FString::Printf(TEXT("the null-resolver control rendered NO green (the linked rule never loaded; green=%d, expect 0)"), GreenNullControl), GreenNullControl, 0);

	return true;
#else
	AddError(TEXT("Cathode.Composition.Tick.ComposedFrameRendersLinkedResource requires Win64 (Blend2D/HarfBuzz/litehtml are Win64-only); the composed linked-resource render did not execute."));
	return false;
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
