#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Templates/UniquePtr.h"

// The Cathode text-shaping swap boundary: our internal contract over the shaping engine (today
// HarfBuzz). This header is the SWAP BOUNDARY — HarfBuzz's own types (hb_*) appear ONLY in the impl,
// never here, so the engine can be replaced without touching the bespoke orchestration (metrics
// derivation, the single-shaped-result dual-consumer, the shape-plan/result cache, feature mapping, the
// font registry + single-load byte store).
//
// Altitude is the handful of ops we need — shape one run (string + font + size + features -> our shaped
// result), and resolve a font handle to its bytes — NOT a 1:1 re-export of the HarfBuzz API and NOT a
// re-creation of a shaper. The shaped POSITIONS/ADVANCES/IDS are HarfBuzz's output (vendor value, never
// hand-asserted, like Blend2D pixels); we test only our orchestration on top.
//
// Text & Fonts is Core + HarfBuzz only — NO Blend2D / BL* here. The glyph-run + font seams are
// engine-NEUTRAL (ids/positions/extents + a font handle + raw font bytes); the Rasterizer maps the
// handle -> BLFont on its side. Same bytes -> same face -> same glyph-id space (the correctness anchor).

namespace Cathode
{

// Engine-neutral font handle. Identifies a registered font without naming any engine's font type. The
// shaper maps it -> hb_font; the Rasterizer (its side) maps it -> BLFont; both over the SAME
// bytes from the registry, so HarfBuzz glyph ids match Blend2D outlines.
struct FFontHandle
{
	int32 Id = INDEX_NONE;

	bool IsValid() const { return Id != INDEX_NONE; }
	bool operator==(const FFontHandle& Other) const { return Id == Other.Id; }
	bool operator!=(const FFontHandle& Other) const { return Id != Other.Id; }
};

// Per-run OpenType feature control. Defaults are kern/liga/calt on; kern=0
// (bKerning=false) is the deliberate retro-CRT unkerned look. Mapped to the HarfBuzz feature array in
// the impl (bespoke mapping, tested).
struct FTextFeatures
{
	bool bKerning = true;              // 'kern' — set false for retro-CRT raw advances
	bool bLigatures = true;            // 'liga'
	bool bContextualAlternates = true; // 'calt'
};

// A shaping request — the input AND the shape-plan/result cache key (text + font + size + features).
struct FShapeRequest
{
	FString Text;
	FFontHandle Font;
	float FontSizePixels = 16.0f;
	FTextFeatures Features;
};

// One shaped glyph, in our types: the glyph id + cluster (HarfBuzz output) and fractional pixel
// positions (HarfBuzz fixed-point advances/offsets scaled to pixels in the impl). Positions are kept
// FRACTIONAL — never snapped (CathodeRasterizer places glyphs at fractional positions).
struct FShapedGlyph
{
	uint32 GlyphId = 0;
	int32 Cluster = 0;

	// Cumulative draw position within the run (running pen advance + this glyph's offset), in fractional
	// px — never snapped. The Rasterizer places the glyph here.
	float X = 0.0f;
	float Y = 0.0f;

	// This glyph's advance, px — for measurement and the running pen.
	float XAdvance = 0.0f;
	float YAdvance = 0.0f;
};

// Run-level metrics — OUR derivation on the shaped glyphs (Width = sum of advances) plus the font's
// horizontal extents. This is what Layout & Style measures against. Our SUM/extent math is the tested
// bespoke part; the per-glyph advances it sums are HarfBuzz's (never hand-asserted).
struct FTextMetrics
{
	float Width = 0.0f;   // sum of glyph x-advances
	float Ascent = 0.0f;  // from the font's horizontal extents
	float Descent = 0.0f;
	float LineGap = 0.0f;
};

// THE SINGLE SHAPED RESULT — one shape call produces this, and BOTH consumers read the SAME instance
// (no double-shape): Layout reads Metrics; the Rasterizer reads Glyphs + Extents + Font. Extents (the
// run's bounding box) is the dependency CathodeRasterizer/COVERAGE.md #2/#1 named — it gives glyph paint
// ops real dirty bounds so glyph-only frames render.
struct FShapedRun
{
	TArray<FShapedGlyph> Glyphs; // the glyph-run for the Rasterizer (ids + fractional positions)
	FTextMetrics Metrics;        // the measurement for Layout
	FBox2f Extents = FBox2f(ForceInit); // run bounding box (CathodeRasterizer COVERAGE.md #2 — glyph dirty bounds)
	FFontHandle Font;            // engine-neutral handle (Rasterizer maps -> BLFont via the byte store)
};

// --- The two seams off the ONE shaped result (no double-shape; no BL*) ---

// The GLYPH-RUN SEAM to the Rasterizer: the engine-neutral output the integration layer maps to CathodeRasterizer's
// FPositionedGlyphRun — glyph ids + fractional positions (in Glyphs) + the run Extents + the FFontHandle
// (the Rasterizer maps it -> BLFont). A view over the shaped run (no copy); valid while the FShapedRun
// lives. NO Blend2D / BL*.
struct FGlyphRun
{
	TArrayView<const FShapedGlyph> Glyphs;
	FBox2f Extents = FBox2f(ForceInit);
	FFontHandle Font;
};

// The MEASUREMENT SEAM (to Layout): the metrics Layout measures against, read off the shaped result.
inline const FTextMetrics& GetMetrics(const FShapedRun& Shaped)
{
	return Shaped.Metrics;
}

// The GLYPH-RUN SEAM: produce the neutral glyph-run from the SAME shaped result the measurement seam read.
inline FGlyphRun GetGlyphRun(const FShapedRun& Shaped)
{
	FGlyphRun Run;
	Run.Glyphs = TArrayView<const FShapedGlyph>(Shaped.Glyphs);
	Run.Extents = Shaped.Extents;
	Run.Font = Shaped.Font;
	return Run;
}

// Engine-neutral font BYTE store: resolves a handle to the font's raw bytes, loaded ONCE and shared.
// This is the shared currency between the shaper (bytes -> hb_font) and the Rasterizer (bytes -> BLFont)
// — neither engine's font type crosses it.
class IFontByteStore
{
public:
	virtual ~IFontByteStore() = default;

	/** The const bytes for a handle (single-load, shared lifetime). False if the handle is unknown. */
	virtual bool GetFontBytes(FFontHandle Handle, TArrayView<const uint8>& OutBytes) const = 0;
};

// The font registry: loads + registers font files (once each), owns the byte lifetime, hands out
// handles. Byte source is the local file system / UE file or asset APIs (single-load-share; UE coupling
// allowed, no UPTIME coupling).
class IFontRegistry : public IFontByteStore
{
public:
	/** Load + register a font file once; a repeat path returns the SAME handle without reloading. */
	virtual FFontHandle RegisterFontFile(const FString& Path) = 0;
};

// The shaping engine — the swap boundary. Shape one run into our FShapedRun (wraps hb_shape + the
// buffer/font setup over the handle's bytes). Swapping shapers swaps only this implementation.
class ITextShaper
{
public:
	virtual ~ITextShaper() = default;

	/** Shape one run: text + font + size + features -> the single shaped result both consumers read. */
	virtual FShapedRun Shape(const FShapeRequest& Request) = 0;
};

// Construct the HarfBuzz-backed shaper over a font byte store. The single place HarfBuzz is bound; the
// shaper builds hb_font from the store's bytes for a handle.
CATHODETEXT_API TUniquePtr<ITextShaper> CreateHarfBuzzShaper(const IFontByteStore& Fonts);

// Construct the font registry (single-load byte store).
CATHODETEXT_API TUniquePtr<IFontRegistry> CreateFontRegistry();

} // namespace Cathode
