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

// Frees the heap font-bytes copy a BLFontData owns — create_from_data's external-data destroy callback. The copy
// is FMemory::Malloc'd at a cache miss and handed to BLFontData::create_from_data(ptr, size, destroy); Blend2D
// invokes this when the (cached) font data is released, so the cached face is self-contained and frees its bytes.
// (We CANNOT use BLArray::assign_data to own the copy: bl_array_assign_data sizes the new impl from the array's
// OLD size, not the new count [vendored array.cpp:883], so a copy INTO a fresh BLArray heap-overflows its buffer —
// an asan-grounded, repro-confirmed system-crasher. The external-data ownership form sidesteps it entirely.)
void BL_CDECL Blend2DFreeOwnedFontBytes(void* /*Impl*/, void* ExternalData, void* /*UserData*/) BL_NOEXCEPT_C
{
	FMemory::Free(ExternalData);
}

// One Blend2D-backed CPU bitmap (wraps BLImage).
class FBlend2DBitmap final : public IRasterBitmap
{
public:
	bool Create(int32 Width, int32 Height, EPixelFormat InFormat)
	{
		Format = InFormat;
		if (Image.create(Width, Height, ToBLFormat(InFormat)) != BL_SUCCESS)
		{
			return false;
		}
		// A freshly-created BLImage has UNDEFINED pixels (create does not zero them). Define the initial state as
		// transparent (clear_all -> all pixels 0 = transparent for PRGB32) so the dirty-region accumulation model
		// has a clean base: FSurfaceRasterizer::RasterizeFrame redraws ONLY the dirty rects, so the area outside
		// the first frame's dirty rects would otherwise show uninitialized garbage (browse 023 — found in the
		// first real composed render of a transparent-page surface; the 34->635 non-deterministic-across-builds
		// ink-outside was the tell). Clearing HERE, at the real bitmap owner, defines every surface buffer's
		// initial state on creation WITHOUT recording a paint op — the recording-backend tests use a separate
		// FRecordingBitmap and are untouched (a clear orchestrated from FDoubleBuffer would have to go through the
		// recorded dispatch API, breaking FrameTests' DispatchedCalls[0]==Clip).
		BLContext ClearCtx(Image);
		ClearCtx.clear_all();
		ClearCtx.end();
		return true;
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
		if (FaceCache == nullptr || Run.Glyphs.Num() == 0 || Run.FontBytes.Num() == 0)
		{
			return;
		}

		// Cache the BLFontFace by font identity (createFromData ONCE per distinct font; reuse across runs — NOT a
		// per-run re-parse). The cache OWNS its bytes: on a miss, COPY the BORROWED FontBytes view into a heap
		// buffer that the BLFontData OWNS — via create_from_data's (ptr, size, destroy) external-data form, which
		// frees the copy through Blend2DFreeOwnedFontBytes when the (cached) font data is released. The cached face
		// is then self-contained and OUTLIVES the per-call borrow (no dangling on a later call); the heap copy has
		// a stable address, so a later cache rehash that moves the BLFontFace does not dangle the data pointer. The
		// seam's FontBytes borrow only needs to be valid for THIS synchronous call's copy (the borrow contract).
		// NOT BLArray::assign_data — that copy heap-overflows a fresh array (see Blend2DFreeOwnedFontBytes).
		BLFontFace* CachedFace = FaceCache->Find(Run.FontId);
		if (CachedFace == nullptr)
		{
			const int32 NumBytes = Run.FontBytes.Num();
			void* OwnedCopy = FMemory::Malloc(static_cast<SIZE_T>(NumBytes)); // BLFontData takes ownership (freed via the destroy cb)
			FMemory::Memcpy(OwnedCopy, Run.FontBytes.GetData(), static_cast<SIZE_T>(NumBytes)); // COPY the borrow once
			BLFontData FontData;
			if (FontData.create_from_data(OwnedCopy, static_cast<size_t>(NumBytes), &Blend2DFreeOwnedFontBytes, nullptr) != BL_SUCCESS)
			{
				FMemory::Free(OwnedCopy); // create failed -> the destroy cb never fires -> free the copy ourselves
				return;
			}
			BLFontFace Face;
			if (Face.create_from_data(FontData, 0) != BL_SUCCESS) { return; } // the face ref-counts the font data (owns the copy)
			CachedFace = &FaceCache->Add(Run.FontId, MoveTemp(Face));
		}

		BLFont Font;
		if (Font.create_from_face(*CachedFace, Run.FontSizePx) != BL_SUCCESS) { return; }

		// Build the BLGlyphRun: glyph ids + per-glyph placements. advance 0 + placement = the shaped offset ->
		// each glyph sits at (Origin + its shaped offset). The origin is a fractional BLPoint (never snapped).
		TArray<uint32> Ids;
		TArray<BLGlyphPlacement> Placements;
		Ids.Reserve(Run.Glyphs.Num());
		Placements.Reserve(Run.Glyphs.Num());
		for (const FRasterGlyph& G : Run.Glyphs)
		{
			Ids.Add(G.GlyphId);
			BLGlyphPlacement Placement{};
			Placement.placement = BLPointI(FMath::RoundToInt(G.X), FMath::RoundToInt(G.Y));
			Placement.advance = BLPointI(0, 0);
			Placements.Add(Placement);
		}
		BLGlyphRun GlyphRun{};
		GlyphRun.size = static_cast<size_t>(Ids.Num());
		GlyphRun.set_glyph_data(Ids.GetData());
		GlyphRun.set_placement_data(Placements.GetData());
		GlyphRun.placement_type = BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET;

		Context.fill_glyph_run(BLPoint(Origin.X, Origin.Y), Font, GlyphRun, ToBLRgba32(Color));
	}

	virtual void ClearRect(const FBox2f& Rect) override
	{
		Context.clear_rect(ToBLRect(Rect));
	}

	virtual void End() override
	{
		Context.end();
	}

	// The backend's font-face cache (set before Begin). Not owned here — the backend persists it across sessions.
	void SetFaceCache(TMap<int64, BLFontFace>* InCache) { FaceCache = InCache; }

private:
	BLContext Context;
	// Borrowed pointer to the backend's BLFontFace-by-identity cache (createFromData once per font; the cache
	// OWNS its bytes). nullptr until BeginContext wires it.
	TMap<int64, BLFontFace>* FaceCache = nullptr;
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
		Session->SetFaceCache(&FontFaceCache); // the cache persists across sessions (createFromData once per font)
		if (!Session->Begin(BitmapTarget, Config))
		{
			return nullptr;
		}
		return Session;
	}

private:
	// Font-face cache, keyed by font identity (FPositionedGlyphRun.FontId). createFromData ONCE per distinct
	// font; the cache OWNS its bytes (each face's BLFontData owns a heap copy of the seam's byte-view, freed via
	// the external-data destroy callback -> the cached face is self-contained, surviving the per-call borrow).
	// Persists for the backend's lifetime.
	TMap<int64, BLFontFace> FontFaceCache;
};

} // namespace

TUniquePtr<IRasterBackend> CreateBlend2DBackend()
{
	return MakeUnique<FBlend2DBackend>();
}

bool DecodeImageSize(const uint8* EncodedData, int32 EncodedSize, int32& OutWidth, int32& OutHeight)
{
	OutWidth = 0;
	OutHeight = 0;
	if (EncodedData == nullptr || EncodedSize <= 0)
	{
		return false;
	}
	// A REAL Blend2D decode of the encoded bytes -> the image's OWN intrinsic dimensions (never hardcoded).
	// Non-image / corrupt bytes return non-BL_SUCCESS -> 0x0 (so the caller distinguishes a real image).
	BLImage Image;
	if (Image.read_from_data(EncodedData, static_cast<size_t>(EncodedSize)) != BL_SUCCESS)
	{
		return false;
	}
	OutWidth = Image.width();
	OutHeight = Image.height();
	return OutWidth > 0 && OutHeight > 0;
}

} // namespace Cathode
