#include "CathodeText.h"

#include "CathodeGlyphAssembly.h"
#include "CathodeShapeFeatures.h"
#include "CathodeTextMetrics.h"

#include "Containers/StringConv.h"

// HarfBuzz — the text shaper. Every hb_* token is confined to this translation unit; swapping shapers
// means reimplementing this file and the factory, nothing else. The include path is wired in
// CathodeText.Build.cs (the headers are vendored under Source/ThirdParty/harfbuzz/include).
THIRD_PARTY_INCLUDES_START
#include "hb.h"
#include "hb-ot.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

namespace
{

// The HarfBuzz-backed shaper. Builds hb_font from the byte store's bytes for a handle (same bytes ->
// same face -> same glyph-id space as the Rasterizer's BLFont), shapes, and assembles our FShapedRun
// glyphs via the bespoke (hb_*-free) assembly, then derives the run Metrics + Extents.
class FHarfBuzzShaper final : public ITextShaper
{
public:
	explicit FHarfBuzzShaper(const IFontByteStore& InFonts)
		: Fonts(InFonts)
	{
	}

	virtual FShapedRun Shape(const FShapeRequest& Request) override
	{
		FShapedRun Result;
		Result.Font = Request.Font;

		TArrayView<const uint8> Bytes;
		if (!Fonts.GetFontBytes(Request.Font, Bytes) || Bytes.Num() == 0)
		{
			return Result; // unknown font -> empty run
		}

		// Build hb_font over the SAME bytes the Rasterizer will use (READONLY: hb references the
		// registry's bytes, which outlive this call; no copy).
		hb_blob_t* Blob = hb_blob_create(
			reinterpret_cast<const char*>(Bytes.GetData()), static_cast<unsigned int>(Bytes.Num()),
			HB_MEMORY_MODE_READONLY, nullptr, nullptr);
		hb_face_t* Face = hb_face_create(Blob, 0);
		hb_font_t* Font = hb_font_create(Face);

		// Scale so advances come back in 26.6 fixed-point pixels; the bespoke assembly divides by 64.
		const int Scale = FMath::RoundToInt(Request.FontSizePixels * 64.0f);
		hb_font_set_scale(Font, Scale, Scale);
		hb_ot_font_set_funcs(Font);

		// Build the buffer from the UTF-8 text + guessed segment properties (script/lang/direction).
		hb_buffer_t* Buffer = hb_buffer_create();
		const FTCHARToUTF8 Utf8(*Request.Text);
		hb_buffer_add_utf8(Buffer, Utf8.Get(), Utf8.Length(), 0, -1);
		hb_buffer_guess_segment_properties(Buffer);

		// Per-run features: our neutral mapping -> hb_feature_t at the thin vendor edge.
		const TArray<FShapeFeature> NeutralFeatures = BuildShapeFeatures(Request.Features);
		TArray<hb_feature_t> Features;
		Features.Reserve(NeutralFeatures.Num());
		for (const FShapeFeature& Feature : NeutralFeatures)
		{
			hb_feature_t HbFeature{};
			HbFeature.tag = Feature.Tag;
			HbFeature.value = Feature.Value;
			HbFeature.start = 0;
			HbFeature.end = static_cast<unsigned int>(-1);
			Features.Add(HbFeature);
		}

		hb_shape(Font, Buffer, Features.GetData(), static_cast<unsigned int>(Features.Num()));

		// Read the vendor output into our neutral raw glyphs (the engine-reading is the vendor edge).
		unsigned int Count = 0;
		const hb_glyph_info_t* Infos = hb_buffer_get_glyph_infos(Buffer, &Count);
		const hb_glyph_position_t* Positions = hb_buffer_get_glyph_positions(Buffer, &Count);

		TArray<FRawGlyph> Raw;
		Raw.Reserve(Count);
		for (unsigned int Index = 0; Index < Count; ++Index)
		{
			FRawGlyph Glyph;
			Glyph.GlyphId = Infos[Index].codepoint; // post-shape, codepoint is the glyph id
			Glyph.Cluster = static_cast<int32>(Infos[Index].cluster);
			Glyph.XAdvance = Positions[Index].x_advance;
			Glyph.YAdvance = Positions[Index].y_advance;
			Glyph.XOffset = Positions[Index].x_offset;
			Glyph.YOffset = Positions[Index].y_offset;
			Raw.Add(Glyph);
		}

		// Bespoke assembly: 26.6 fixed-point -> px, cumulative positions.
		constexpr float PixelsPerUnit = 1.0f / 64.0f;
		Result.Glyphs = AssembleGlyphs(Raw, PixelsPerUnit);

		// Vertical extents: read the font's horizontal extents (vendor) and scale to px (our scale), then
		// derive our metrics (width = sum of advances) + run bounding box (CathodeRasterizer COVERAGE.md #2).
		hb_font_extents_t HbExtents{};
		hb_font_get_h_extents(Font, &HbExtents);
		const float Ascent = HbExtents.ascender * PixelsPerUnit;   // above baseline (positive)
		const float Descent = -HbExtents.descender * PixelsPerUnit; // descender is negative -> positive descent
		const float LineGap = HbExtents.line_gap * PixelsPerUnit;

		Result.Metrics = DeriveMetrics(Result.Glyphs, Ascent, Descent, LineGap);
		Result.Extents = DeriveExtents(Result.Glyphs, Ascent, Descent);

		hb_buffer_destroy(Buffer);
		hb_font_destroy(Font);
		hb_face_destroy(Face);
		hb_blob_destroy(Blob);

		return Result; // one shaped result: Glyphs (Rasterizer) + Metrics (Layout) + Extents, both consumers read it
	}

private:
	const IFontByteStore& Fonts;
};

} // namespace

TUniquePtr<ITextShaper> CreateHarfBuzzShaper(const IFontByteStore& Fonts)
{
	return MakeUnique<FHarfBuzzShaper>(Fonts);
}

} // namespace Cathode
