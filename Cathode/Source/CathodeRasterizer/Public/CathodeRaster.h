#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

// The Cathode rasterizer swap boundary: our internal contract over the CPU paint backend (today
// Blend2D, JIT via AsmJit). This header is the SWAP BOUNDARY — Blend2D's own types (BL*, and AsmJit)
// appear ONLY in the implementation, never here, so the backend can be replaced without touching the
// Rasterizer's bespoke logic (bitmap/buffer bookkeeping, dirty-region tracking, draw-dispatch).
//
// Altitude is the handful of paint ops the backend needs — create a bitmap, begin a draw session, fill
// rect / fill box / draw image / draw glyph run, end — NOT a 1:1 re-export of the Blend2D API and NOT a
// re-creation of a 2D engine. The op-set is what Layout & Style and Text & Fonts feed; rendering
// quality/AA come from config (FRasterConfig), never hardcoded in a draw call.

namespace Cathode
{

// Pixel format of the CPU bitmap, in our terms; mapped to a Blend2D BLFormat in the impl.
//
// DECIDED (human): the surface format is PremultipliedBGRA8 — sRGB-ENCODED BGRA8. The Rasterizer blends
// in sRGB (Blend2D default, not forced linear) and emits these sRGB bytes so the output lines up with an
// adjacent UTexture2D flagged sRGB=true on an unlit/emissive material (encode/decode match; the scene
// does not relight the screen). PremultipliedBGRA8 is the default surface format.
enum class EPixelFormat : uint8
{
	PremultipliedBGRA8, // 32bpp, alpha-premultiplied, sRGB-encoded (impl -> BL_FORMAT_PRGB32) — the surface default
	OpaqueBGRA8,        // 32bpp, alpha ignored                     (impl -> BL_FORMAT_XRGB32)
	Alpha8,             // 8bpp coverage                            (impl -> BL_FORMAT_A8)
};

// The decided default surface format (see EPixelFormat note).
inline constexpr EPixelFormat DefaultSurfaceFormat = EPixelFormat::PremultipliedBGRA8;

// A read/write view of one CPU bitmap's pixels — the unit handed to Texture Egress (mapped from
// BLImageData in the impl).
struct FBitmapData
{
	void* Pixels = nullptr;
	int32 Stride = 0; // bytes per row
	int32 Width = 0;
	int32 Height = 0;
	EPixelFormat Format = DefaultSurfaceFormat;
};

// The OUTPUT SEAM: on a frame-ready, the Rasterizer hands a produced CPU bitmap (the published front
// buffer) plus the dirty rectangles to this integration point. Future Surfaces / Texture Egress consume
// it — uploading the dirty sub-rects into the surface's texture (lining up with the 002 frame-ready
// path). Generalizable and evolvable; Core-only — no CathodeRuntime coupling. Default unset until those
// components wire it.
using FFrameReadySeam = TFunction<void(const FBitmapData& Bitmap, const TArray<FIntRect>& DirtyRects)>;

// One positioned glyph in the Rasterizer's OWN neutral terms (NO CathodeText type — the Rasterizer stays a
// leaf). A glyph id + the fractional offset from the run origin (the shaped pen position). The id indexes the
// SAME font face the Rasterizer builds from the run's FontBytes (same bytes -> same glyph-id space — the
// correctness anchor with the shaper).
struct FRasterGlyph
{
	uint32 GlyphId = 0;
	float X = 0.0f; // fractional offset from the run origin (the shaped pen position)
	float Y = 0.0f;
};

// A positioned glyph run from Text & Fonts — the INPUT SEAM for text (the fields GREW here, sprint 022). It is
// ENGINE-NEUTRAL: it carries the font BYTES + the Rasterizer's own glyph type, so the Rasterizer needs NO
// CathodeText dep (the bytes + ids are INJECTED, not a dependency — the leaf-preserving design).
//
// BORROW CONTRACT (load-bearing): FontBytes and Glyphs are BORROWED VIEWS into data owned EXTERNALLY (the
// caller / Text & Fonts' single-load byte store). They are valid ONLY for the duration of the SYNCHRONOUS
// DrawGlyphRun call. The Rasterizer reads FontBytes during the call and COPIES it into an owned, cached
// BLFontFace on a cache miss (so the cache, which outlives the borrow, never dangles). FPositionedGlyphRun
// MUST NOT be retained past the owning data, or DrawGlyphRun would read a dangling view -> UAF.
struct FPositionedGlyphRun
{
	TArrayView<const uint8> FontBytes;     // BORROWED font-file bytes (see the borrow contract) — for BLFontFace
	int64 FontId = 0;                      // font identity for the BLFontFace cache (createFromData ONCE per font)
	float FontSizePx = 0.0f;               // the shaped font size, pixels
	TArrayView<const FRasterGlyph> Glyphs; // BORROWED glyph ids + fractional positions (the shaped run)
};

// Image-scaling quality for content-image draws (impl -> BLPatternQuality).
enum class EImageScalingQuality : uint8
{
	NearestNeighbor,
	Bilinear, // Blend2D default
};

// Gradient rendering quality (impl -> BLGradientQuality).
enum class EGradientQuality : uint8
{
	Nearest,
	Smooth, // Blend2D default
	Dither,
};

// Dirty-region granularity — our-bespoke (applied by the dirty-region tracker, Slice 3).
enum class EDirtyRegionGranularity : uint8
{
	PerRect,       // track each dirty rect independently (least overdraw)
	BoundingUnion, // coalesce to one bounding rect (coarser, fewer uploads)
};

// Rendering configuration — the tunable knobs, read when a draw session begins. ALL knobs are DECIDED
// (the human's tuning set) and exposed here at top level for easy later re-tuning; none are left
// undecided. No baked fidelity guesses — each value is a decision or a deliberate Blend2D/arch default.
struct FRasterConfig
{
	// DECIDED (human): grayscale analytic anti-aliasing, never subpixel, no supersampling — Blend2D's
	// default text/vector quality (the 4K AA bar is met by true-outline grayscale AA, not supersampling).
	// true = AA on (the only path); an aliased retro-CRT look, if ever needed, is a separate mechanism,
	// not this flag (BLRenderingQuality has only ANTIALIAS today).
	bool bAntiAlias = true;

	// DECIDED (human): no supersampling.
	bool bSupersample = false;

	// DECIDED (human): fractional glyph positioning — do NOT snap glyph origins to the pixel grid.
	bool bFractionalGlyphPositioning = true;

	// DECIDED (human): glyph outlines are rasterized true (no hinting / no grid-fit). Hinting POLICY is
	// owned by Text & Fonts (shaping); the Rasterizer simply does not grid-fit the outlines it is given.
	bool bGridFitGlyphs = false;

	// DECIDED (human): blend in sRGB space (Blend2D default) — do NOT force linear blending. Pairs with
	// the sRGB-encoded BGRA8 output and the adjacent UTexture2D sRGB=true + unlit/emissive material.
	bool bBlendInLinearSpace = false;

	// DECIDED (human = Blend2D default): content-image scaling quality.
	EImageScalingQuality ImageScalingQuality = EImageScalingQuality::Bilinear;

	// DECIDED (human = Blend2D default): gradient rendering quality.
	EGradientQuality GradientQuality = EGradientQuality::Smooth;

	// DECIDED (human = arch default): double-buffering (front/back). Two buffers.
	uint8 BufferCount = 2;

	// DECIDED (human = sensible default): dirty-region granularity (Slice 3 applies it).
	EDirtyRegionGranularity DirtyRegionGranularity = EDirtyRegionGranularity::PerRect;

	// DECIDED (human, RQ1 CLOSED): single-threaded — Blend2D renders SYNCHRONOUSLY on our worker thread
	// with NO own thread pool. 0 = synchronous (maps to BLContextCreateInfo.thread_count = 0); aligns
	// with the 002 single-dedicated-worker model. Exposed for tuning, but the decision is single-thread.
	uint32 BackendThreadCount = 0;
};

// One CPU bitmap (wraps BLImage). The Rasterizer draws into the back buffer; Texture Egress reads it.
class IRasterBitmap
{
public:
	virtual ~IRasterBitmap() = default;

	virtual bool IsValid() const = 0;
	virtual int32 GetWidth() const = 0;
	virtual int32 GetHeight() const = 0;
	virtual EPixelFormat GetFormat() const = 0;

	/** Mutable pixel view for egress upload (wraps BLImage::make_mutable -> BLImageData). */
	virtual bool GetData(FBitmapData& OutData) = 0;
};

// --- The paint-op INPUT SEAM (draw calls from Layout & Style; glyph runs from Text & Fonts) ---

enum class EPaintOpKind : uint8
{
	FillRect,
	FillBox,      // box / border fill
	DrawImage,
	DrawGlyphRun,
	ClearRect,
};

// A single paint operation — the input seam Layout & Style emits as a stream; the draw-dispatch consumes
// it and routes each op to the backend draw session. Deliberately minimal and EVOLVABLE: the op kinds and
// fields grow with Layout & Text, not pre-specified to a full taxonomy. Geometry is in our types
// (FBox2f / FVector2f / FColor); text is the evolvable FPositionedGlyphRun. A fat tagged struct keeps the
// seam one simple type while the shape settles.
struct FPaintOp
{
	EPaintOpKind Kind = EPaintOpKind::FillRect;

	FBox2f Rect = FBox2f(ForceInit);  // rect / box / image-dest / clear rect
	FColor Color = FColor::Black;     // fill / text color

	const IRasterBitmap* Image = nullptr; // DrawImage source

	FVector2f GlyphOrigin = FVector2f::ZeroVector; // DrawGlyphRun origin (kept fractional)
	FPositionedGlyphRun GlyphRun;                  // DrawGlyphRun glyphs (evolvable)
};

// A draw session targeting one bitmap (wraps BLContext begin..end). The op-set is exactly what Layout &
// Text feed — fill rect, fill box/border, draw image, draw glyph run, clear — not the full Blend2D API.
// Quality/AA are applied from the FRasterConfig passed at BeginContext, never hardcoded per op.
class IRasterContext
{
public:
	virtual ~IRasterContext() = default;

	/** Clip subsequent draws to a rect — used to rasterize DIRTY REGIONS ONLY (not the whole surface).
	 *  Wraps BLContext::clip_to_rect. */
	virtual void ClipToRect(const FBox2f& Rect) = 0;

	/** Remove the clip set by ClipToRect (wraps BLContext::restore_clipping). */
	virtual void ResetClip() = 0;

	virtual void FillRect(const FBox2f& Rect, const FColor& Color) = 0;
	virtual void FillBox(const FBox2f& Box, const FColor& Color) = 0; // box / border fill
	virtual void DrawImage(const FBox2f& DestRect, const IRasterBitmap& Source) = 0;
	virtual void DrawGlyphRun(const FVector2f& Origin, const FPositionedGlyphRun& Run, const FColor& Color) = 0;
	virtual void ClearRect(const FBox2f& Rect) = 0;

	/** Flush + finish the session (wraps BLContext::end). */
	virtual void End() = 0;
};

// The paint-backend swap boundary root. Creates bitmaps and begins draw sessions — the single place the
// vendor is bound. Swapping rasterizers swaps only this implementation. The bespoke orchestration
// (buffers, dirty regions, draw-dispatch) and the specs run against a RECORDING backend through this
// same interface.
class IRasterBackend
{
public:
	virtual ~IRasterBackend() = default;

	virtual TUniquePtr<IRasterBitmap> CreateBitmap(int32 Width, int32 Height, EPixelFormat Format) = 0;

	/** Begin a draw session on the target bitmap, applying the rendering config (AA/quality/threads). */
	virtual TUniquePtr<IRasterContext> BeginContext(IRasterBitmap& Target, const FRasterConfig& Config) = 0;
};

// Construct the Blend2D-backed backend. The single place Blend2D (+ AsmJit) is bound; swapping the
// rasterizer swaps this implementation.
CATHODERASTERIZER_API TUniquePtr<IRasterBackend> CreateBlend2DBackend();

// The per-surface frame PRODUCER seam (sprint 023): the minimal public surface over the bespoke
// FSurfaceRasterizer (the double-buffer + dirty-region + draw-dispatch producer) so the integration
// composition root can CONSTRUCT and DRIVE one surface's producer WITHOUT the concrete class or its
// buffering/dirty internals (FDoubleBuffer/FDirtyRegion stay Private) leaking into the public API — the
// same abstract-interface + factory idiom as IRasterBackend/CreateBlend2DBackend. The Rasterizer stays a
// leaf (Core + Blend2D); this exposes only the producer behavior the composition root injects behind
// CathodeSurfaces' ISurfaceRenderTarget.
class ISurfaceProducer
{
public:
	virtual ~ISurfaceProducer() = default;

	/** Rasterize one frame from the input ops; true iff a real change was produced + handed off (a
	 *  frame-ready). On a change, the FFrameReadySeam fires with the published front buffer + dirty rects. */
	virtual bool RasterizeFrame(const TArray<FPaintOp>& Ops) = 0;

	/** Wire the output seam the produced front-buffer bitmap + dirty rects are handed to (on a change). */
	virtual void SetFrameReadySeam(FFrameReadySeam Seam) = 0;

	virtual int32 GetWidth() const = 0;
	virtual int32 GetHeight() const = 0;
};

// Construct the per-surface producer (the bespoke double-buffer/dirty-region rasterizer) over a backend at
// fixed dims + config. The single construction point for an out-of-module surface producer; the concrete
// FSurfaceRasterizer (and its FDoubleBuffer/FDirtyRegion) stay Private.
CATHODERASTERIZER_API TUniquePtr<ISurfaceProducer> CreateSurfaceRasterizer(IRasterBackend& Backend, int32 Width, int32 Height, const FRasterConfig& Config);

// Decode an encoded image's INTRINSIC dimensions from its raw bytes (the 003 decode-for-size), via Blend2D's
// image decoder. Returns true + the real WxH for a decodable image; false + 0x0 for empty/undecodable input.
// Blend2D (BL*) stays confined to the impl. CathodeLayout's get_image_size routing calls this at integration.
CATHODERASTERIZER_API bool DecodeImageSize(const uint8* EncodedData, int32 EncodedSize, int32& OutWidth, int32& OutHeight);

} // namespace Cathode
