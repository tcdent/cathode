#include "CathodeEgress.h"
#include "CathodeEgressUploader.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "UObject/StrongObjectPtr.h"
#include "PixelFormat.h"

namespace Cathode
{
namespace
{
	// Map the neutral surface format to the UE pixel format the UTexture2D is created with. THIS is the 1b
	// format-map bind: a wrong choice swaps channels IN THE TEXTURE, which only the on-GPU readback can see (the
	// source bytes stay [B,G,R,A] regardless). PremultipliedBGRA8 / OpaqueBGRA8 are both BGRA8 byte layout ->
	// PF_B8G8R8A8 (sRGB flagged separately); Alpha8 -> PF_A8.
	EPixelFormat ToTexturePixelFormat(ESurfacePixelFormat Format)
	{
		switch (Format)
		{
		case ESurfacePixelFormat::Alpha8:
			return PF_A8;
		case ESurfacePixelFormat::OpaqueBGRA8:
		case ESurfacePixelFormat::PremultipliedBGRA8:
		default:
			return PF_B8G8R8A8;
		}
	}

	// The bounding rect of the surface's dirty rectangles, clamped to the texture. 1b uploads this single region
	// (on the first frame the whole surface is dirty -> the full texture). Slice 2 uploads per-sub-rect.
	FIntRect DirtyBoundsClamped(const TArray<FIntRect>& Rects, int32 Width, int32 Height)
	{
		const FIntRect Full(0, 0, Width, Height);
		if (Rects.Num() == 0)
		{
			return Full; // nothing reported -> treat the whole texture as dirty
		}
		FIntRect Bounds = Rects[0];
		for (int32 i = 1; i < Rects.Num(); ++i)
		{
			Bounds.Min.X = FMath::Min(Bounds.Min.X, Rects[i].Min.X);
			Bounds.Min.Y = FMath::Min(Bounds.Min.Y, Rects[i].Min.Y);
			Bounds.Max.X = FMath::Max(Bounds.Max.X, Rects[i].Max.X);
			Bounds.Max.Y = FMath::Max(Bounds.Max.Y, Rects[i].Max.Y);
		}
		Bounds.Min.X = FMath::Clamp(Bounds.Min.X, 0, Width);
		Bounds.Min.Y = FMath::Clamp(Bounds.Min.Y, 0, Height);
		Bounds.Max.X = FMath::Clamp(Bounds.Max.X, 0, Width);
		Bounds.Max.Y = FMath::Clamp(Bounds.Max.Y, 0, Height);
		return Bounds;
	}

	// The PRODUCTION uploader: the real RHIUpdateTexture2D. Runs on the render thread (the egress invokes it inside
	// its ENQUEUE_RENDER_COMMAND). The de-risked slice-0 RHI path.
	void ProductionUpload(const FEgressUpload& Upload)
	{
		if (Upload.Texture != nullptr)
		{
			RHIUpdateTexture2D(Upload.Texture, 0, Upload.Region, Upload.SourcePitch, Upload.SourceData);
		}
	}

	// The per-surface GPU target: the UTexture2D (GC-held) + its fixed dims.
	struct FEgressTexture
	{
		TStrongObjectPtr<UTexture2D> Texture;
		int32 Width = 0;
		int32 Height = 0;
	};

	class FTextureEgress final : public ITextureEgress
	{
	public:
		explicit FTextureEgress(FEgressUploader InUploader)
			: Uploader(InUploader ? MoveTemp(InUploader) : FEgressUploader(&ProductionUpload))
		{
		}

		virtual FSurfaceReadinessSeam GetSeam() override
		{
			return [this](const FSurfaceReadiness& Readiness) { OnReadiness(Readiness); };
		}

		virtual UTexture2D* GetTexture(FSurfaceId Surface) const override
		{
			const FEgressTexture* Found = Textures.Find(Surface);
			return Found ? Found->Texture.Get() : nullptr;
		}

		virtual void Release(FSurfaceId Surface) override
		{
			Textures.Remove(Surface); // drops the TStrongObjectPtr -> the texture is GC-collectable
		}

	private:
		// Game-thread readiness callback. Create/find the surface's texture, COPY the dirty bytes here (the borrow
		// is valid only synchronously), and marshal the upload onto the render thread with the copy owned by the
		// command.
		void OnReadiness(const FSurfaceReadiness& Readiness)
		{
			const FSurfaceBufferRef& Buffer = Readiness.Frame.Buffer;
			if (!Buffer.IsValid() || Buffer.Width <= 0 || Buffer.Height <= 0 || Buffer.Stride <= 0)
			{
				return;
			}

			FEgressTexture& Entry = Textures.FindOrAdd(Readiness.Surface);
			if (!Entry.Texture.IsValid())
			{
				UTexture2D* NewTexture = UTexture2D::CreateTransient(Buffer.Width, Buffer.Height, ToTexturePixelFormat(Buffer.Format));
				if (NewTexture == nullptr)
				{
					Textures.Remove(Readiness.Surface);
					return;
				}
				NewTexture->SRGB = true;       // PremultipliedBGRA8 is sRGB-encoded — matches the surface contract
				NewTexture->UpdateResource();  // create the RHI resource (a render-thread init; precedes the upload by FIFO)
				Entry.Texture.Reset(NewTexture);
				Entry.Width = Buffer.Width;
				Entry.Height = Buffer.Height;
			}

			FTextureResource* Resource = Entry.Texture->GetResource();
			if (Resource == nullptr)
			{
				return;
			}

			// COPY the dirty bytes BY VALUE at the callback (the borrow can't cross the async hop). 1b copies the
			// full strided buffer (Height * Stride), preserving the source pitch; slice 2 copies per-sub-rect.
			const int32 NumBytes = Buffer.Height * Buffer.Stride;
			TArray<uint8> Copy;
			Copy.SetNumUninitialized(NumBytes);
			FMemory::Memcpy(Copy.GetData(), Buffer.Pixels, NumBytes);

			const uint32 SourcePitch = static_cast<uint32>(Buffer.Stride);
			const FIntRect Dirty = DirtyBoundsClamped(Readiness.Frame.DirtyRects, Buffer.Width, Buffer.Height);
			const FUpdateTextureRegion2D Region(
				static_cast<uint32>(Dirty.Min.X), static_cast<uint32>(Dirty.Min.Y), // DestX, DestY
				Dirty.Min.X, Dirty.Min.Y,                                           // SrcX, SrcY into the copy
				static_cast<uint32>(Dirty.Width()), static_cast<uint32>(Dirty.Height()));

			const FSurfaceId Surface = Readiness.Surface;
			FEgressUploader UploaderForCommand = Uploader;

			// Marshal onto the render thread; the command OWNS the copy (moved in). The RHI texture is resolved
			// inside the command (after the resource's render-thread init has run, by FIFO ordering).
			ENQUEUE_RENDER_COMMAND(CathodeEgressUpload)(
				[Resource, Region, SourcePitch, Surface, Bytes = MoveTemp(Copy), UploaderForCommand](FRHICommandListImmediate& RHICmdList)
				{
					FEgressUpload Upload;
					Upload.Texture = Resource->GetTexture2DRHI();
					Upload.Region = Region;
					Upload.SourcePitch = SourcePitch;
					Upload.SourceData = Bytes.GetData();
					Upload.SourceNumBytes = Bytes.Num();
					Upload.Surface = Surface;
					UploaderForCommand(Upload);
				});
		}

		FEgressUploader Uploader;
		TMap<FSurfaceId, FEgressTexture> Textures;
	};
} // namespace

TUniquePtr<ITextureEgress> CreateTextureEgress()
{
	return MakeUnique<FTextureEgress>(FEgressUploader());
}

TUniquePtr<ITextureEgress> CreateTextureEgressWithUploader(FEgressUploader Uploader)
{
	return MakeUnique<FTextureEgress>(MoveTemp(Uploader));
}

} // namespace Cathode
