#include "CathodeEgress.h"
#include "CathodeEgressUploader.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "UObject/StrongObjectPtr.h"
#include "PixelFormat.h"

#include "CathodeTripleBuffer.h" // 029 #24 — the lock-free triple-buffer (the published-index release/acquire fence pairing)

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

	// Bytes per pixel for the neutral surface formats (BGRA8 = 4, Alpha8 = 1).
	int32 BytesPerPixel(ESurfacePixelFormat Format)
	{
		return (Format == ESurfacePixelFormat::Alpha8) ? 1 : 4;
	}

	// One assembled upload per dirty rect (slice 2, the dirty-rect REGIONAL upload — uniform TIGHT, one copy path):
	// a TIGHT command-owned copy of ONLY that rect's pixels (read from the STRIDED source — step source rows by the
	// view's Stride, offset by the rect origin — into a no-padding buffer) + the matching FUpdateTextureRegion2D
	// (DestX/DestY = the rect origin, SrcX/SrcY = 0 since the tight buffer STARTS at the rect, Width/Height = the
	// rect). SourcePitch is the TIGHT pitch (RectW*bpp). The COPY and the upload are BOTH bounded to R — a few
	// pixels for a few pixels. No full-frame special case: a full-frame rect copies tight too (the strided-read
	// correctness is bound by the readback over a padded, per-row-distinct source).
	struct FAssembledRegion
	{
		FUpdateTextureRegion2D Region{ 0u, 0u, 0, 0, 0u, 0u };
		uint32 SourcePitch = 0;
		TArray<uint8> Bytes; // the tight copy for THIS rect (owned by the render command)
	};

	TArray<FAssembledRegion> AssembleDirtyUploads(const TArray<FIntRect>& Rects, int32 Width, int32 Height, const uint8* Source, int32 Stride, int32 Bpp)
	{
		TArray<FAssembledRegion> Uploads;
		const auto AssembleRect = [&](const FIntRect& Rect)
		{
			const int32 MinX = FMath::Clamp(Rect.Min.X, 0, Width);
			const int32 MinY = FMath::Clamp(Rect.Min.Y, 0, Height);
			const int32 MaxX = FMath::Clamp(Rect.Max.X, 0, Width);
			const int32 MaxY = FMath::Clamp(Rect.Max.Y, 0, Height);
			const int32 RectW = MaxX - MinX;
			const int32 RectH = MaxY - MinY;
			if (RectW <= 0 || RectH <= 0)
			{
				return; // empty after clamping
			}
			const int32 TightPitch = RectW * Bpp;
			FAssembledRegion Assembled;
			Assembled.SourcePitch = static_cast<uint32>(TightPitch);
			Assembled.Bytes.SetNumUninitialized(TightPitch * RectH);
			// Copy ONLY the rect's rows from the STRIDED source into the tight buffer: step the source by Stride,
			// offset MinX*Bpp within each row; write contiguous tight rows. A wrong read-stride misaligns the rows
			// from a padded source -> the readback mismatches (the relocated padded-stride discriminator).
			for (int32 Row = 0; Row < RectH; ++Row)
			{
				const uint8* SrcRow = Source + static_cast<int64>(MinY + Row) * Stride + static_cast<int64>(MinX) * Bpp;
				uint8* DstRow = Assembled.Bytes.GetData() + static_cast<int64>(Row) * TightPitch;
				FMemory::Memcpy(DstRow, SrcRow, TightPitch);
			}
			Assembled.Region = FUpdateTextureRegion2D(
				static_cast<uint32>(MinX), static_cast<uint32>(MinY), // DestX, DestY
				0, 0,                                                 // SrcX, SrcY — the tight buffer starts at the rect
				static_cast<uint32>(RectW), static_cast<uint32>(RectH));
			Uploads.Add(MoveTemp(Assembled));
		};

		if (Rects.Num() == 0)
		{
			AssembleRect(FIntRect(0, 0, Width, Height)); // nothing reported -> the whole texture
		}
		else
		{
			for (const FIntRect& Rect : Rects)
			{
				AssembleRect(Rect);
			}
		}
		return Uploads;
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

	// 028 S2 — a per-surface PENDING frame: the worker-emit's owned tight copies (the stubbed double-buffer cross),
	// held until the game-thread pump creates the texture + uploads. PURE CPU bytes — NO UObject, safe off the game thread.
	struct FPendingFrame
	{
		FSurfaceId Surface;  // 029 #24 — the surface this frame is for (rides the slot; the game-thread upload keys Textures on it)
		TArray<FAssembledRegion> Uploads;
		int32 Width = 0;
		int32 Height = 0;
		ESurfacePixelFormat Format = ESurfacePixelFormat::PremultipliedBGRA8;
	};

	// 029 #24 — the egress holds a single-surface lock-free FTripleBuffer<FPendingFrame> (CathodeTripleBuffer.h): the worker
	// publishes the latest frame, the game thread acquires it, with no per-frame HolderLock. #27 fans out to a per-surface map.
	class FTextureEgress final : public ITextureEgress
	{
	public:
		explicit FTextureEgress(FEgressUploader InUploader)
			: Uploader(InUploader ? MoveTemp(InUploader) : FEgressUploader(&ProductionUpload))
		{
		}

		// 028 S2 — the WORKER-side emit (safe off the game thread): copy + store, ZERO UObject.
		virtual FSurfaceReadinessSeam GetSeam() override
		{
			return [this](const FSurfaceReadiness& Readiness) { WorkerEmit(Readiness); };
		}

		// 028 S2 — the GAME-THREAD upload half: for each surface with pending copied bytes, create/find its texture
		// (UObject) + marshal the render-thread upload from the stored copies.
		virtual void PumpGameThreadUploads() override
		{
			// 029 #24 — ACQUIRE the latest published frame LOCK-FREE (the triple-buffer acquire == the old snapshot-under-
			// lock, minus the per-frame contention). Single-surface -> exactly one published frame; #27 fans out to a
			// per-surface triple-buffer map + a per-surface drain loop.
			FPendingFrame Frame;
			if (!Frames.Acquire(Frame))
			{
				return; // no new frame since the last pump
			}
			{
				const FSurfaceId Surface = Frame.Surface;
				if (Frame.Uploads.Num() == 0)
				{
					return;
				}

				FEgressTexture& Tex = Textures.FindOrAdd(Surface);
				if (!Tex.Texture.IsValid())
				{
					UTexture2D* NewTexture = UTexture2D::CreateTransient(Frame.Width, Frame.Height, ToTexturePixelFormat(Frame.Format));
					if (NewTexture == nullptr)
					{
						Textures.Remove(Surface);
						return;
					}
					// 029 #27 — CLEAR the texture on create: zero the mip BEFORE UpdateResource so the create uploads a CLEAN
					// (0) texture. The dirty-rect upload then writes ONLY the rendered rects, leaving the MARGIN deterministic-0
					// — vs the uninitialized CreateTransient GPU memory (= the prior test's residual) that drove the #30 out-box
					// ORDER-SENSITIVITY (the egress-readback ISOLATION fix; lets the #30 floor restore 0.80 -> 0.85).
					if (FTexturePlatformData* PlatformData = NewTexture->GetPlatformData())
					{
						if (PlatformData->Mips.Num() > 0)
						{
							FTexture2DMipMap& Mip = PlatformData->Mips[0];
							if (void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE))
							{
								FMemory::Memzero(MipData, static_cast<SIZE_T>(Mip.BulkData.GetBulkDataSize()));
							}
							Mip.BulkData.Unlock();
						}
					}
					NewTexture->SRGB = true;       // PremultipliedBGRA8 is sRGB-encoded — matches the surface contract
					NewTexture->UpdateResource();  // create the RHI resource (a render-thread init; precedes the upload by FIFO)
					Tex.Texture.Reset(NewTexture);
					Tex.Width = Frame.Width;
					Tex.Height = Frame.Height;
				}

				FTextureResource* Resource = Tex.Texture->GetResource();
				if (Resource == nullptr)
				{
					return;
				}

				FEgressUploader UploaderForCommand = Uploader;
				TArray<FAssembledRegion> Uploads = MoveTemp(Frame.Uploads);
				ENQUEUE_RENDER_COMMAND(CathodeEgressUpload)(
					[Resource, Uploads = MoveTemp(Uploads), Surface, UploaderForCommand](FRHICommandListImmediate& RHICmdList)
					{
						FRHITexture* TexRHI = Resource->GetTexture2DRHI();
						for (const FAssembledRegion& Assembled : Uploads)
						{
							FEgressUpload Upload;
							Upload.Texture = TexRHI;
							Upload.Region = Assembled.Region;
							Upload.SourcePitch = Assembled.SourcePitch;
							Upload.SourceData = Assembled.Bytes.GetData();
							Upload.SourceNumBytes = Assembled.Bytes.Num();
							Upload.Surface = Surface;
							UploaderForCommand(Upload);
						}
					});
			}
		}

		virtual UTexture2D* GetTexture(FSurfaceId Surface) const override
		{
			const FEgressTexture* Found = Textures.Find(Surface);
			return Found ? Found->Texture.Get() : nullptr;
		}

		virtual void Release(FSurfaceId Surface) override
		{
			Textures.Remove(Surface); // 029 #24 — drops the TStrongObjectPtr -> the texture is GC-collectable. Game-thread only;
			// the triple-buffer's stale frame (if any) is never acquired after the owner's gone (no PumpGameThreadUploads after Dispose).
		}

	private:
		// WORKER-side emit (028 S2): COPY the dirty bytes (the borrow is valid only synchronously — copy HERE) into an
		// owned buffer + STORE in the per-surface holder. ZERO UObject / ZERO RHI -> safe on the worker thread. The
		// game-thread PumpGameThreadUploads() does the texture creation + upload from the stored copy.
		void WorkerEmit(const FSurfaceReadiness& Readiness)
		{
			const FSurfaceBufferRef& Buffer = Readiness.Frame.Buffer;
			if (!Buffer.IsValid() || Buffer.Width <= 0 || Buffer.Height <= 0 || Buffer.Stride <= 0)
			{
				return;
			}

			const int32 Bpp = BytesPerPixel(Buffer.Format);
			TArray<FAssembledRegion> Uploads = AssembleDirtyUploads(
				Readiness.Frame.DirtyRects, Buffer.Width, Buffer.Height,
				static_cast<const uint8*>(Buffer.Pixels), Buffer.Stride, Bpp);
			if (Uploads.Num() == 0)
			{
				return;
			}

			// 029 #24 — publish LOCK-FREE: the deep-copy (above) is done; build the frame + publish it to the triple-buffer
			// (a slot move + a release-exchange of the index). No HolderLock -> no per-frame contention with the game drain.
			FPendingFrame Pending;
			Pending.Surface = Readiness.Surface;
			Pending.Uploads = MoveTemp(Uploads);
			Pending.Width = Buffer.Width;
			Pending.Height = Buffer.Height;
			Pending.Format = Buffer.Format;
			Frames.Publish(MoveTemp(Pending));
		}

		FEgressUploader Uploader;
		TMap<FSurfaceId, FEgressTexture> Textures;     // GAME-thread — the UObject textures
		FTripleBuffer<FPendingFrame> Frames;           // 029 #24 — the worker publishes, the game thread acquires (LOCK-FREE, single-surface; #27 -> a per-surface map)
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
