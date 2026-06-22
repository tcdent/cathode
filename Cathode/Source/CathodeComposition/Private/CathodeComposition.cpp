#include "CathodeComposition.h"

#include "CathodeShaperTextMeasurer.h"

#include "CathodeLayout.h"   // ILayoutEngine, FLayoutResult, FLayoutPaintOp, CreateLiteHtmlLayoutEngine
#include "CathodeRaster.h"   // FPaintOp, FRasterGlyph, IRasterBackend, ISurfaceProducer, CreateBlend2DBackend, CreateSurfaceRasterizer
#include "CathodeResolver.h" // IResourceResolver, IBundleSource, CreateResourceResolver, CreateUEFileBundleSource (slice 3)
#include "CathodeSurfaces.h" // ISurfaceDocument, ISurfaceRenderTarget, the carries-it payloads
#include "CathodeText.h"     // FShapedRun, FShapedGlyph (the mapper's text case shapes via the shared adapter)

#include "Misc/Paths.h"      // FPaths::ProjectContentDir (the production default-root)
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

namespace Cathode
{

namespace
{

// --- ISurfaceDocument backed by the real Layout engine -----------------------------------------------------
//
// RenderFrame relays the document through the real ILayoutEngine and exposes the layout's paint-op array as the
// opaque FSurfacePaintBatch. The ITextMeasurer adapter is SHARED with the render-target backer (the same-bytes
// anchor: the FLayoutFontRef Layout measures with resolves to the SAME FFontHandle the mapper shapes + the
// Rasterizer draws). Adapter MUST precede Engine (the engine holds it by reference). LastResult is a member so
// the exposed Ops pointer stays valid across the synchronous tick (RenderFrame -> ProduceFrame).
class FLayoutSurfaceDocument final : public ISurfaceDocument
{
public:
	FLayoutSurfaceDocument(const ILayoutDocumentSource& InDocument, TSharedRef<FShaperTextMeasurer> InAdapter, bool bWireResolver)
		: Adapter(InAdapter)
		, BundleSource(bWireResolver ? CreateUEFileBundleSource(GetDefaultResourceRoot()) : nullptr)
		, Resolver(BundleSource.IsValid() ? CreateResourceResolver(*BundleSource) : nullptr)
		, Engine(CreateLiteHtmlLayoutEngine(*Adapter))
	{
		if (Engine.IsValid())
		{
			// PRODUCTION resolver wiring (slice 3): the production CALLER constructs the resolver over the
			// production default-root and injects it BEFORE SetDocument, so linked resources (@import/<link>)
			// resolve from /Game/Cathode via the real FUEFileBundleSource. A null Resolver (bWireResolver=false)
			// is the pre-hook behavior — linked rules never load -> defaults (the null-resolver control).
			if (Resolver.IsValid())
			{
				Engine->SetResourceResolver(Resolver.Get());
			}
			Engine->SetDocument(InDocument); // the document source must outlive this surface (the public contract)
		}
	}

	virtual FSurfacePaintBatch RenderFrame(int32 AvailableWidth) override
	{
		FSurfacePaintBatch Batch;
		if (Engine.IsValid())
		{
			LastResult = Engine->Relayout(AvailableWidth);
			Batch.Ops = &LastResult.PaintOps; // opaque to Surfaces; the render-target backer (same module) reads it
		}
		return Batch;
	}

private:
	TSharedRef<FShaperTextMeasurer> Adapter; // SHARED with the render-target backer; MUST precede Engine (held by ref)
	TUniquePtr<IBundleSource> BundleSource;   // production file-bundle source over GetDefaultResourceRoot(); MUST precede Resolver
	TUniquePtr<IResourceResolver> Resolver;   // production resolver; MUST precede Engine (engine holds it by pointer)
	TUniquePtr<ILayoutEngine> Engine;
	FLayoutResult LastResult;                // owns the paint ops the opaque batch points at, across the tick
};

// --- ISurfaceRenderTarget backed by the real Rasterizer producer -------------------------------------------
//
// ProduceFrame maps the layout ops (carried opaquely from the document backer) to the Rasterizer's FPaintOp
// stream and rasterizes into the Blend2D bitmap. The TEXT case (slice 2) shapes the un-shaped FLayoutPaintOp.Text
// via the SHARED adapter (the same FFontHandle Layout measured with), applies the glyph-run map (FShapedGlyph ->
// FRasterGlyph), and emits a DrawGlyphRun op. The producer hands back its published front buffer + dirty rects via
// the PUSH FFrameReadySeam — we capture + re-expose via GetReadyFrame. Backend MUST precede Producer (held by ref).

// Map the Rasterizer's neutral EPixelFormat to Surfaces' neutral ESurfacePixelFormat (both mirror the same three
// formats). The producer fills the neutral seam's Format from its captured bitmap; keep in sync if either grows.
static ESurfacePixelFormat ToSurfaceFormat(EPixelFormat Format)
{
	switch (Format)
	{
	case EPixelFormat::OpaqueBGRA8:         return ESurfacePixelFormat::OpaqueBGRA8;
	case EPixelFormat::Alpha8:              return ESurfacePixelFormat::Alpha8;
	case EPixelFormat::PremultipliedBGRA8:
	default:                                return ESurfacePixelFormat::PremultipliedBGRA8;
	}
}

class FRasterizerSurfaceRenderTarget final : public ISurfaceRenderTarget
{
public:
	FRasterizerSurfaceRenderTarget(int32 InWidth, int32 InHeight, ESurfacePixelFormat InFormat, TSharedRef<FShaperTextMeasurer> InAdapter)
		: Width(InWidth)
		, Height(InHeight)
		, Format(InFormat)
		, Adapter(InAdapter)
		, Backend(CreateBlend2DBackend())
		, Producer(Backend.IsValid() ? CreateSurfaceRasterizer(*Backend, InWidth, InHeight, FRasterConfig{}) : nullptr)
	{
		if (Producer.IsValid())
		{
			Producer->SetFrameReadySeam([this](const FBitmapData& Bitmap, const TArray<FIntRect>& DirtyRects)
			{
				CapturedBitmap = Bitmap;        // the published front buffer view (valid until the next frame)
				CapturedRects = DirtyRects;
				bHaveFrame = true;
			});
		}
	}

	virtual bool ProduceFrame(const FSurfacePaintBatch& Batch) override
	{
		if (!Producer.IsValid() || Batch.Ops == nullptr)
		{
			return false;
		}
		const TArray<FLayoutPaintOp>& LayoutOps = *static_cast<const TArray<FLayoutPaintOp>*>(Batch.Ops);

		TArray<FPaintOp> Ops;
		Ops.Reserve(LayoutOps.Num());
		// The glyph runs in DrawGlyphRun ops carry BORROWED views (FPositionedGlyphRun.Glyphs); hold the FRasterGlyph
		// arrays here so they outlive the synchronous RasterizeFrame below. Reserve so this never reallocates — the
		// views into its elements must stay valid (the FontBytes views borrow the registry, alive via the adapter).
		TArray<TArray<FRasterGlyph>> GlyphBacking;
		GlyphBacking.Reserve(LayoutOps.Num());

		for (const FLayoutPaintOp& L : LayoutOps)
		{
			switch (L.Kind)
			{
			case ELayoutPaintOpKind::FillRect:
				{ FPaintOp P; P.Kind = EPaintOpKind::FillRect; P.Rect = L.Rect; P.Color = L.Color; Ops.Add(P); }
				break;
			case ELayoutPaintOpKind::FillBox:
				{ FPaintOp P; P.Kind = EPaintOpKind::FillBox; P.Rect = L.Rect; P.Color = L.Color; Ops.Add(P); }
				break;
			case ELayoutPaintOpKind::ClearRect:
				{ FPaintOp P; P.Kind = EPaintOpKind::ClearRect; P.Rect = L.Rect; Ops.Add(P); }
				break;
			case ELayoutPaintOpKind::DrawText:
				{
					if (L.Text.IsEmpty()) { break; }
					FShapedRun Shaped;
					float FontSizePx = 0.0f;
					if (!Adapter->ShapeText(L.Font, L.Text, Shaped, FontSizePx)) { break; } // SAME FFontHandle as the measure
					TArrayView<const uint8> FontBytes;
					if (!Adapter->GetFontBytes(Shaped.Font, FontBytes) || FontBytes.Num() == 0) { break; }
					// the 022 glyph-run map: FShapedGlyph -> FRasterGlyph (id + fractional pen position from the run origin)
					TArray<FRasterGlyph> Glyphs;
					Glyphs.Reserve(Shaped.Glyphs.Num());
					for (const FShapedGlyph& G : Shaped.Glyphs) { Glyphs.Add(FRasterGlyph{ G.GlyphId, G.X, G.Y }); }
					if (Glyphs.Num() == 0) { break; }
					TArray<FRasterGlyph>& Backing = GlyphBacking[GlyphBacking.Add(MoveTemp(Glyphs))];

					FPaintOp P;
					P.Kind = EPaintOpKind::DrawGlyphRun;
					// L.Position is the text box's TOP-LEFT (litehtml pos.y, CathodeLayoutEngine.cpp draw_text);
					// DrawGlyphRun's origin is the BASELINE. The mapper bridges Layout's top-left convention to the
					// Rasterizer's baseline convention by adding the font ascent. Without this the glyphs render
					// above the box top and are clipped out -> zero glyph ink (browse 023 slice-2 diagnosis).
					P.GlyphOrigin = FVector2f(L.Position.X, L.Position.Y + Shaped.Metrics.Ascent);
					P.Color = L.Color;
					P.GlyphRun.FontBytes = FontBytes;   // borrowed from the registry (the adapter outlives the tick)
					P.GlyphRun.FontId = Shaped.Font.Id;
					P.GlyphRun.FontSizePx = FontSizePx;
					P.GlyphRun.Glyphs = Backing;        // borrowed from GlyphBacking (held across RasterizeFrame below)
					Ops.Add(P);
				}
				break;
			default:
				break; // DrawImage (slice 3 / resolver) / StrokeBorder (deferred stroke op) / ClipPush / ClipPop
			}
		}

		return Producer->RasterizeFrame(Ops); // borrows valid here: GlyphBacking + the adapter/registry are alive
	}

	virtual FSurfaceReadyFrame GetReadyFrame() const override
	{
		FSurfaceReadyFrame Frame;
		if (bHaveFrame)
		{
			// Populate the NEUTRAL pixel VIEW from the captured Blend2D FBitmapData (RQ2 (ii) — the seam carries no
			// vendor type; Egress reads Pixels/Stride/dims/format, never an FBitmapData). A pass-through VIEW: the
			// SAME Pixels pointer + Stride, no copy/conversion — the borrow contract is unchanged from the 023
			// Handle (the async copy-at-callback that crosses the render-thread hop is Egress's, 1b). Format is
			// populated but first READ downstream (1b picks PF_B8G8R8A8 from it); 1a's consumers read [B,G,R,A] off
			// Pixels directly, so the format map is populated-not-bound here.
			Frame.Buffer.Pixels = CapturedBitmap.Pixels;
			Frame.Buffer.Stride = CapturedBitmap.Stride;
			Frame.Buffer.Width = CapturedBitmap.Width;
			Frame.Buffer.Height = CapturedBitmap.Height;
			Frame.Buffer.Format = ToSurfaceFormat(CapturedBitmap.Format);
			Frame.DirtyRects = CapturedRects;
		}
		return Frame;
	}

	virtual int32 GetWidth() const override { return Width; }
	virtual int32 GetHeight() const override { return Height; }
	virtual ESurfacePixelFormat GetFormat() const override { return Format; }

private:
	int32 Width = 0;
	int32 Height = 0;
	ESurfacePixelFormat Format = ESurfacePixelFormat::PremultipliedBGRA8;
	TSharedRef<FShaperTextMeasurer> Adapter; // SHARED with the document backer (one FFontHandle: measure+shape+raster)
	TUniquePtr<IRasterBackend> Backend;      // MUST be declared before Producer (producer holds it by reference)
	TUniquePtr<ISurfaceProducer> Producer;
	FBitmapData CapturedBitmap;              // the front-buffer view captured from the push seam
	TArray<FIntRect> CapturedRects;
	bool bHaveFrame = false;
};

} // namespace

FString GetDefaultResourceRoot()
{
	// The production content root: /Game/Cathode == ProjectContentDir()/Cathode. This literal MUST EQUAL the
	// staging entry (+DirectoriesToAlwaysStageAsUFS=/Game/Cathode in Config/DefaultGame.ini) — a mismatch would
	// ship a content root the cook cannot stage (the 019 coupling). /Game/ is the ProjectContentDir mount, so
	// /Game/Cathode resolves to ProjectContentDir()/Cathode (the editor's Content/Cathode; the cooked pak after
	// staging).
	return FPaths::ProjectContentDir() / TEXT("Cathode");
}

FSurfaceRegistration MakeCompositionSurfaceRegistration(
	const ILayoutDocumentSource& Document,
	int32 Width,
	int32 Height,
	ESurfacePixelFormat Format,
	const FString& FontFilePath,
	bool bWireResourceResolver)
{
	// ONE adapter, SHARED by both backers — the same-bytes anchor: Layout measures, the mapper shapes, and the
	// Rasterizer draws over the SAME font (one FFontHandle resolved from the same registry/bytes).
	TSharedRef<FShaperTextMeasurer> Adapter = MakeShared<FShaperTextMeasurer>(FontFilePath);

	FSurfaceRegistration Registration;
	Registration.Width = Width;
	Registration.Height = Height;
	Registration.Format = Format;
	Registration.Document = MakeUnique<FLayoutSurfaceDocument>(Document, Adapter, bWireResourceResolver);
	Registration.Target = MakeUnique<FRasterizerSurfaceRenderTarget>(Width, Height, Format, Adapter);
	return Registration;
}

} // namespace Cathode
