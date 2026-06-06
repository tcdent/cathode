#include "CathodeRaster.h"

// Blend2D — the CPU paint backend (JIT via AsmJit). Every BL* token is confined to this translation
// unit; swapping rasterizers means reimplementing this file and the factory, nothing else. The include
// path is wired by the execution-approval sprint's build (the header is vendored at
// vendor/lib/cathode/blend2d/blend2d/blend2d.h, built from source then); no build runs this sprint.
THIRD_PARTY_INCLUDES_START
#include "blend2d.h"
THIRD_PARTY_INCLUDES_END

// TODO-PREBUILD(naming): every BLImage/BLContext method in this file uses the snake_case API
// (create / is_empty / make_mutable / begin / set_hint / clip_to_rect / restore_clipping / fill_rect /
// fill_box / blit_image / clear_rect / end). Verify against the VENDORED Blend2D — older releases expose
// camelCase (isEmpty/makeMutable/fillRect/clipToRect/blitImage/clearRect/setHint). If the vendored API is
// camelCase, every call site here needs the rename.

namespace Cathode
{

namespace
{

BLFormat ToBLFormat(EPixelFormat Format)
{
	switch (Format)
	{
	case EPixelFormat::PremultipliedBGRA8: return BL_FORMAT_PRGB32;
	case EPixelFormat::OpaqueBGRA8:        return BL_FORMAT_XRGB32;
	case EPixelFormat::Alpha8:             return BL_FORMAT_A8;
	default:                               return BL_FORMAT_NONE;
	}
}

EPixelFormat FromBLFormat(uint32_t Format)
{
	switch (Format)
	{
	case BL_FORMAT_PRGB32: return EPixelFormat::PremultipliedBGRA8;
	case BL_FORMAT_XRGB32: return EPixelFormat::OpaqueBGRA8;
	case BL_FORMAT_A8:     return EPixelFormat::Alpha8;
	default:               return EPixelFormat::PremultipliedBGRA8;
	}
}

// Geometry/color mapping from our types to Blend2D's (vendor-confined here).
BLRect ToBLRect(const FBox2f& Box)
{
	return BLRect(Box.Min.X, Box.Min.Y, Box.Max.X - Box.Min.X, Box.Max.Y - Box.Min.Y);
}

BLBox ToBLBox(const FBox2f& Box)
{
	return BLBox(Box.Min.X, Box.Min.Y, Box.Max.X, Box.Max.Y);
}

BLRgba32 ToBLRgba32(const FColor& Color)
{
	// TODO-PREBUILD(arg-order): confirm the BLRgba32(r, g, b, a) byte-component constructor exists and
	// takes that argument order (vs a single packed 0xAARRGGBB value) in the vendored Blend2D.
	return BLRgba32(Color.R, Color.G, Color.B, Color.A);
}

// One Blend2D-backed CPU bitmap (wraps BLImage).
class FBlend2DBitmap final : public IRasterBitmap
{
public:
	bool Create(int32 Width, int32 Height, EPixelFormat InFormat)
	{
		Format = InFormat;
		return Image.create(Width, Height, ToBLFormat(InFormat)) == BL_SUCCESS;
	}

	virtual bool IsValid() const override { return !Image.is_empty(); }
	virtual int32 GetWidth() const override { return Image.width(); }
	virtual int32 GetHeight() const override { return Image.height(); }
	virtual EPixelFormat GetFormat() const override { return Format; }

	virtual bool GetData(FBitmapData& OutData) override
	{
		// TODO-PREBUILD(signature): confirm BLImage::make_mutable(BLImageData*) is the right call for
		// read/write pixel access (vs get_data for read-only) and the BLImageData field names
		// (pixel_data / stride / size.w / size.h / format) against the vendored Blend2D.
		BLImageData Data;
		if (Image.make_mutable(&Data) != BL_SUCCESS)
		{
			return false;
		}
		OutData.Pixels = Data.pixel_data;
		OutData.Stride = static_cast<int32>(Data.stride);
		OutData.Width = Data.size.w;
		OutData.Height = Data.size.h;
		OutData.Format = FromBLFormat(Data.format);
		return true;
	}

	// Internal accessor for the draw session (same translation unit).
	BLImage& GetBLImage() { return Image; }
	const BLImage& GetBLImage() const { return Image; }

private:
	BLImage Image;
	EPixelFormat Format = EPixelFormat::PremultipliedBGRA8;
};

// A Blend2D draw session (wraps BLContext). The FRasterConfig is applied ONCE at begin (thread_count at
// context creation, AA/quality as a hint) — never re-applied per draw op.
class FBlend2DContext final : public IRasterContext
{
public:
	bool Begin(FBlend2DBitmap& Target, const FRasterConfig& Config)
	{
		BLContextCreateInfo CreateInfo{};
		// TODO-PREBUILD(struct-field): verify BLContextCreateInfo.thread_count field name (vs threadCount)
		// against the vendored Blend2D.
		CreateInfo.thread_count = Config.BackendThreadCount; // 0 = synchronous on our worker (decided)
		if (Context.begin(Target.GetBLImage(), CreateInfo) != BL_SUCCESS)
		{
			return false;
		}
		// Rendering config applied once here, not per op. Grayscale analytic AA is the only
		// BLRenderingQuality; sRGB blending is the Blend2D default (config does not force linear).
		// TODO-PREBUILD(enum): verify set_hint takes (BLContextHint, uint32) and the enum names
		// BL_CONTEXT_HINT_RENDERING_QUALITY / BL_RENDERING_QUALITY_ANTIALIAS against the vendored Blend2D.
		Context.set_hint(BL_CONTEXT_HINT_RENDERING_QUALITY, BL_RENDERING_QUALITY_ANTIALIAS);
		return true;
	}

	virtual void ClipToRect(const FBox2f& Rect) override
	{
		Context.clip_to_rect(ToBLRect(Rect));
	}

	virtual void ResetClip() override
	{
		Context.restore_clipping();
	}

	virtual void FillRect(const FBox2f& Rect, const FColor& Color) override
	{
		Context.fill_rect(ToBLRect(Rect), ToBLRgba32(Color));
	}

	virtual void FillBox(const FBox2f& Box, const FColor& Color) override
	{
		Context.fill_box(ToBLBox(Box), ToBLRgba32(Color));
	}

	virtual void DrawImage(const FBox2f& DestRect, const IRasterBitmap& Source) override
	{
		// The backend only produces FBlend2DBitmap, so this downcast is safe.
		const FBlend2DBitmap& Src = static_cast<const FBlend2DBitmap&>(Source);
		Context.blit_image(ToBLRect(DestRect), Src.GetBLImage());
	}

	virtual void DrawGlyphRun(const FVector2f& Origin, const FPositionedGlyphRun& Run, const FColor& Color) override
	{
		// TODO(Text & Fonts): map FPositionedGlyphRun -> BLFont + BLGlyphRun and
		// Context.fill_glyph_run(BLPoint(Origin.X, Origin.Y), Font, GlyphRun, ToBLRgba32(Color)). The
		// glyph data (font + shaped glyphs) is supplied by Text & Fonts (future). The origin is already
		// fractional — the draw-dispatch never snaps it — so no rounding happens here either.
		(void)Origin;
		(void)Run;
		(void)Color;
	}

	virtual void ClearRect(const FBox2f& Rect) override
	{
		Context.clear_rect(ToBLRect(Rect));
	}

	virtual void End() override
	{
		Context.end();
	}

private:
	BLContext Context;
};

// The Blend2D-backed paint backend — the single place Blend2D (+ AsmJit) is bound.
class FBlend2DBackend final : public IRasterBackend
{
public:
	virtual TUniquePtr<IRasterBitmap> CreateBitmap(int32 Width, int32 Height, EPixelFormat Format) override
	{
		TUniquePtr<FBlend2DBitmap> Bitmap = MakeUnique<FBlend2DBitmap>();
		Bitmap->Create(Width, Height, Format);
		return Bitmap; // IsValid()==false on failure (never null) — same contract as the runtime factory
	}

	virtual TUniquePtr<IRasterContext> BeginContext(IRasterBitmap& Target, const FRasterConfig& Config) override
	{
		// The backend only ever produces FBlend2DBitmap, so this downcast is safe.
		FBlend2DBitmap& BitmapTarget = static_cast<FBlend2DBitmap&>(Target);
		TUniquePtr<FBlend2DContext> Session = MakeUnique<FBlend2DContext>();
		if (!Session->Begin(BitmapTarget, Config))
		{
			return nullptr;
		}
		return Session;
	}
};

} // namespace

TUniquePtr<IRasterBackend> CreateBlend2DBackend()
{
	return MakeUnique<FBlend2DBackend>();
}

} // namespace Cathode
