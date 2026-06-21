#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeRaster.h"
#include "CathodeText.h" // 021's shaper + byte store (TEST-ONLY dep; the production Rasterizer stays a leaf)

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

// 022 — the LAST engine stub: real shaped text -> real pixels. FBlend2DContext::DrawGlyphRun (FPositionedGlyphRun
// -> BLFont via createFromData [cached by font identity; the cache OWNS its bytes] + BLGlyphRun -> fill_glyph_run)
// rasterizes 021's REAL hb_shape output (over the committed OFL Liberation Sans v2.1.5 — the SAME bytes -> SAME
// glyph-id space) to correct pixels in a REAL Blend2D bitmap (the first real BLContext + pixel-read).
//
// TAUTOLOGY-FREE (Blend2D owns the pixel VALUES): the LOCALIZED-INK relation -- (a) EXISTS a non-background pixel
// IN the glyph's laid-out box (a dense 'M' -> many; a no-op fill -> blank -> RED), AND (b) ALL pixels in a
// guaranteed-OUTSIDE margin are background (a fill-everything -> RED). NOT a hand-assert of Blend2D's exact pixels;
// NOT ALL-ink-in-box (a glyph is strokes + gaps -> EXISTS, not ALL); the position-aware part = the ink is IN the box.
// SAME-BYTES ANCHOR: ONE FFontHandle drives BOTH 021's shape AND 022's raster (the bytes the shaper shaped).
// WIN64-EXECUTED-NOT-SKIPPED (Blend2D + HarfBuzz Win64-only): #if PLATFORM_WINDOWS; the #else AddErrors (no vacuous
// #if'd-out green); the [WIN64-EXECUTED] marker emits the ink pixel COUNT (>0) — evidence only the executed raster emits.
// LEAF: the bridge (021's FGlyphRun + the byte store -> the neutral FPositionedGlyphRun) is TEST-only; the production
// Rasterizer carries NO CathodeText dep (the bytes are IN the seam).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRasterizerGlyphRasterTest,
	"Cathode.Rasterizer.Glyph.RealRasterPixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRasterizerGlyphRasterTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

#if PLATFORM_WINDOWS
	// REUSE 021's committed OFL fixture cross-module (no re-commit).
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
	if (!Plugin.IsValid()) { AddError(TEXT("could not locate the Cathode plugin base dir (IPluginManager)")); return false; }
	const FString FontPath = Plugin->GetBaseDir() / TEXT("Source/CathodeText/Private/Tests/Fixtures/LiberationSans-Regular.ttf");
	if (!FPaths::FileExists(FontPath)) { AddError(FString::Printf(TEXT("the committed OFL font fixture is not on disk: %s"), *FontPath)); return false; }

	// 021's shaper over the OFL font -- ONE FFontHandle drives BOTH the shape AND the raster (the same-bytes anchor).
	TUniquePtr<IFontRegistry> Registry = CreateFontRegistry();
	const FFontHandle FontHandle = Registry->RegisterFontFile(FontPath);
	TUniquePtr<ITextShaper> Shaper = CreateHarfBuzzShaper(*Registry);

	const float SizePx = 48.0f;
	FShapeRequest Req;
	Req.Text = TEXT("M"); // a DENSE glyph -> robust ink-in-box
	Req.Font = FontHandle;
	Req.FontSizePixels = SizePx;
	const FShapedRun Shaped = Shaper->Shape(Req);
	const FGlyphRun ShapedGlyphs = GetGlyphRun(Shaped); // 021's REAL shaped glyph(s) of 'M'

	// the SAME bytes 021's hb_font used -- a BORROWED view (the Registry holds them alive across the synchronous draw).
	TArrayView<const uint8> FontBytes;
	if (!Registry->GetFontBytes(FontHandle, FontBytes) || FontBytes.Num() == 0)
	{
		AddError(TEXT("could not get the font bytes from the registry")); return false;
	}

	// bridge 021's FGlyphRun -> the Rasterizer's NEUTRAL FPositionedGlyphRun (NO CathodeText type in the seam).
	TArray<FRasterGlyph> RasterGlyphs;
	for (const FShapedGlyph& G : ShapedGlyphs.Glyphs) { RasterGlyphs.Add({ G.GlyphId, G.X, G.Y }); }
	if (RasterGlyphs.Num() == 0) { AddError(TEXT("021 shaped no glyphs for 'M'")); return false; }

	FPositionedGlyphRun Run;
	Run.FontBytes = FontBytes;  // borrowed (Registry alive across the draw — the borrow contract)
	Run.FontId = FontHandle.Id; // the neutral identity for the face cache
	Run.FontSizePx = SizePx;
	Run.Glyphs = RasterGlyphs;  // borrowed (RasterGlyphs alive across the draw)

	// the FIRST real Blend2D bitmap + context + pixel-read.
	const int32 W = 128, H = 128;
	TUniquePtr<IRasterBackend> Backend = CreateBlend2DBackend();
	TUniquePtr<IRasterBitmap> Bitmap = Backend->CreateBitmap(W, H, EPixelFormat::PremultipliedBGRA8);
	if (!Bitmap.IsValid() || !Bitmap->IsValid()) { AddError(TEXT("Blend2D bitmap creation failed")); return false; }
	const FRasterConfig Config;
	TUniquePtr<IRasterContext> Ctx = Backend->BeginContext(*Bitmap, Config);
	if (!Ctx.IsValid()) { AddError(TEXT("Blend2D context begin failed")); return false; }

	const FVector2f Origin(40.0f, 90.0f); // baseline at y=90; the glyph extends up (ascent) above it
	Ctx->ClearRect(FBox2f(FVector2f(0.0f, 0.0f), FVector2f(static_cast<float>(W), static_cast<float>(H)))); // clear to transparent (alpha 0 = blank)
	Ctx->DrawGlyphRun(Origin, Run, FColor(255, 255, 255, 255)); // opaque white ink
	Ctx->End(); // flush sync -> commit the glyph fill to the bitmap pixels

	FBitmapData Data;
	if (!Bitmap->GetData(Data) || Data.Pixels == nullptr) { AddError(TEXT("GetData failed / null pixels")); return false; }
	const uint8* Px = static_cast<const uint8*>(Data.Pixels);
	// PremultipliedBGRA8 (BL_FORMAT_PRGB32): little-endian bytes [B,G,R,A]; alpha at +3. ink = alpha>0; blank = alpha==0.
	auto AlphaAt = [&](int32 x, int32 y) -> uint8 { return Px[y * Data.Stride + x * 4 + 3]; };

	// the glyph's laid-out box: x in [Origin.X, Origin.X + the run width]; vertical = +/- the font size around the
	// baseline (generous — it only must CONTAIN the glyph for the EXISTS-ink scan).
	const int32 BoxL = FMath::Clamp(FMath::FloorToInt(Origin.X), 0, W - 1);
	const int32 BoxR = FMath::Clamp(FMath::CeilToInt(Origin.X + Shaped.Metrics.Width), 1, W);
	const int32 BoxT = FMath::Clamp(FMath::FloorToInt(Origin.Y - SizePx), 0, H - 1);
	const int32 BoxB = FMath::Clamp(FMath::CeilToInt(Origin.Y + SizePx * 0.5f), 1, H);

	// (a) INK: EXISTS a non-background pixel IN the box (a dense 'M' -> many). Count = the WIN64-executed evidence.
	int32 InkInBox = 0;
	for (int32 y = BoxT; y < BoxB; ++y) { for (int32 x = BoxL; x < BoxR; ++x) { if (AlphaAt(x, y) > 0) { ++InkInBox; } } }

	// (b) BLANK: ALL pixels in the guaranteed-outside HORIZONTAL margins (before Origin.X-gap; after Origin.X+width+gap),
	// every row, are background. The glyph's x-extent is [Origin.X, Origin.X+width]; outside +/- the gap is blank for ALL y.
	const int32 Gap = 8;
	const int32 LeftMargin = FMath::Clamp(FMath::FloorToInt(Origin.X) - Gap, 0, W);
	const int32 RightMargin = FMath::Clamp(FMath::CeilToInt(Origin.X + Shaped.Metrics.Width) + Gap, 0, W);
	int32 MarginInk = 0;
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < LeftMargin; ++x) { if (AlphaAt(x, y) > 0) { ++MarginInk; } }   // before the glyph
		for (int32 x = RightMargin; x < W; ++x) { if (AlphaAt(x, y) > 0) { ++MarginInk; } }   // after the glyph
	}

	AddInfo(FString::Printf(TEXT("[WIN64-EXECUTED][022-RASTER] real fill_glyph_run: ink-in-box=%d (box x[%d,%d] y[%d,%d]) margin-ink=%d (L<%d, R>=%d) | glyphs=%d size=%.0f width=%.2f"),
		InkInBox, BoxL, BoxR, BoxT, BoxB, MarginInk, LeftMargin, RightMargin, RasterGlyphs.Num(), SizePx, Shaped.Metrics.Width));

	// (a) [WIN64-EXECUTED] + INK-in-box: EXISTS ink in the glyph's laid-out box (a no-op / #if'd-out path -> 0 -> RED).
	TestTrue(FString::Printf(TEXT("[WIN64-EXECUTED] (ink) real fill_glyph_run produced ink IN the glyph's laid-out box (count=%d > 0)"), InkInBox), InkInBox > 0);
	// (b) BLANK margins: ALL guaranteed-outside margin pixels are background (a fill-everything -> margin ink -> RED).
	TestEqual(FString::Printf(TEXT("(blank) ALL guaranteed-outside margin pixels are background (margin-ink=%d, expect 0)"), MarginInk), MarginInk, 0);

	return true;
#else
	// WIN64-EXECUTED-NOT-SKIPPED: Blend2D + HarfBuzz are Win64-only; do NOT vacuously pass on a non-Win64 build
	// (the raster + assertions would be #if'd out -> a meaningless green). Fail explicitly.
	AddError(TEXT("Cathode.Rasterizer.Glyph.RealRasterPixels requires Win64 (Blend2D + HarfBuzz are Win64-only); the real raster did not execute on this platform — a non-Win64 pass would be vacuous."));
	return false;
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
