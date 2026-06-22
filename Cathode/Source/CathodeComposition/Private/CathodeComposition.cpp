#include "CathodeComposition.h"

#include "CathodeShaperTextMeasurer.h"

#include "CathodeLayout.h"   // ILayoutEngine, FLayoutResult, FLayoutPaintOp, CreateLiteHtmlLayoutEngine
#include "CathodeRaster.h"   // FPaintOp, IRasterBackend, ISurfaceProducer, CreateBlend2DBackend, CreateSurfaceRasterizer
#include "CathodeSurfaces.h" // ISurfaceDocument, ISurfaceRenderTarget, the carries-it payloads

#include "Templates/UniquePtr.h"

namespace Cathode
{

namespace
{

// --- The non-text FLayoutPaintOp -> FPaintOp mapper (slice 1) -----------------------------------------------
//
// Layout emits its OWN neutral op type (FLayoutPaintOp); the Rasterizer consumes its own (FPaintOp). The two
// are distinct by design (the swap-boundary types), so the composition root maps between them. Slice 1 maps the
// NON-TEXT ops a colored box produces (FillRect / FillBox / ClearRect). The other kinds are out of slice-1
// scope and not emitted by a bg-color div: DrawText is slice 2's heart (shape -> FPositionedGlyphRun); DrawImage
// needs the resolver (slice 3); StrokeBorder is the deferred stroke op (Rasterizer COVERAGE #3); ClipPush/ClipPop
// have no FPaintOp clip op (clipping is an IRasterContext call, not a paint op). They are skipped here.
TArray<FPaintOp> MapLayoutPaintOpsNonText(const TArray<FLayoutPaintOp>& In)
{
	TArray<FPaintOp> Out;
	Out.Reserve(In.Num());
	for (const FLayoutPaintOp& L : In)
	{
		FPaintOp P;
		switch (L.Kind)
		{
		case ELayoutPaintOpKind::FillRect:
			P.Kind = EPaintOpKind::FillRect; P.Rect = L.Rect; P.Color = L.Color; Out.Add(P);
			break;
		case ELayoutPaintOpKind::FillBox:
			P.Kind = EPaintOpKind::FillBox; P.Rect = L.Rect; P.Color = L.Color; Out.Add(P);
			break;
		case ELayoutPaintOpKind::ClearRect:
			P.Kind = EPaintOpKind::ClearRect; P.Rect = L.Rect; Out.Add(P);
			break;
		default:
			break; // DrawText (slice 2) / DrawImage (slice 3) / StrokeBorder / ClipPush / ClipPop — not slice 1
		}
	}
	return Out;
}

// --- ISurfaceDocument backed by the real Layout engine -----------------------------------------------------
//
// RenderFrame relays the document through the real ILayoutEngine and exposes the layout's paint-op array as the
// opaque FSurfacePaintBatch. The Measurer MUST precede the Engine (the engine holds it by ref). LastResult is a
// member so the exposed Ops pointer stays valid across the synchronous tick (RenderFrame -> ProduceFrame).
class FLayoutSurfaceDocument final : public ISurfaceDocument
{
public:
	FLayoutSurfaceDocument(const ILayoutDocumentSource& InDocument, const FString& FontFilePath)
		: Measurer(FontFilePath)
		, Engine(CreateLiteHtmlLayoutEngine(Measurer))
	{
		if (Engine.IsValid())
		{
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
	FShaperTextMeasurer Measurer;        // MUST be declared before Engine (Engine holds it by reference)
	TUniquePtr<ILayoutEngine> Engine;
	FLayoutResult LastResult;            // owns the paint ops the opaque batch points at, across the tick
};

// --- ISurfaceRenderTarget backed by the real Rasterizer producer -------------------------------------------
//
// ProduceFrame maps the layout ops (carried opaquely from the document backer) to the Rasterizer's FPaintOp
// stream and rasterizes into the Blend2D bitmap. The producer hands back its published front buffer + dirty
// rects via the PUSH FFrameReadySeam (no pull) — we capture them and re-expose via GetReadyFrame as the
// opaque FSurfaceBufferRef. Backend MUST precede Producer (the producer holds the backend by reference).
class FRasterizerSurfaceRenderTarget final : public ISurfaceRenderTarget
{
public:
	FRasterizerSurfaceRenderTarget(int32 InWidth, int32 InHeight, ESurfacePixelFormat InFormat)
		: Width(InWidth)
		, Height(InHeight)
		, Format(InFormat)
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
		const TArray<FPaintOp> Ops = MapLayoutPaintOpsNonText(LayoutOps);
		return Producer->RasterizeFrame(Ops); // fires the frame-ready seam on a real change (captures the bitmap)
	}

	virtual FSurfaceReadyFrame GetReadyFrame() const override
	{
		FSurfaceReadyFrame Frame;
		if (bHaveFrame)
		{
			Frame.Buffer.Handle = &CapturedBitmap; // resolves to the real Blend2D FBitmapData at the consumer
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
	TUniquePtr<IRasterBackend> Backend;     // MUST be declared before Producer (producer holds it by reference)
	TUniquePtr<ISurfaceProducer> Producer;
	FBitmapData CapturedBitmap;             // the front-buffer view captured from the push seam
	TArray<FIntRect> CapturedRects;
	bool bHaveFrame = false;
};

} // namespace

FSurfaceRegistration MakeCompositionSurfaceRegistration(
	const ILayoutDocumentSource& Document,
	int32 Width,
	int32 Height,
	ESurfacePixelFormat Format,
	const FString& FontFilePath)
{
	FSurfaceRegistration Registration;
	Registration.Width = Width;
	Registration.Height = Height;
	Registration.Format = Format;
	Registration.Document = MakeUnique<FLayoutSurfaceDocument>(Document, FontFilePath);
	Registration.Target = MakeUnique<FRasterizerSurfaceRenderTarget>(Width, Height, Format);
	return Registration;
}

} // namespace Cathode
