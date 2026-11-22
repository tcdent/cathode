#include "CathodeEgress.h"
#include "CathodeEgressUploader.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "UObject/StrongObjectPtr.h"
#include "PixelFormat.h"

#include "CathodeTripleBuffer.h" // the lock-free triple-buffer (the published-index release/acquire fence pairing)

namespace Cathode
{
namespace
{
	// Map the neutral surface format to the UE pixel format the UTexture2D is created with. THIS is the
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

	// One assembled upload per dirty rect (the dirty-rect REGIONAL upload — uniform TIGHT, one copy path):
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
			// from a padded source -> the readback mismatches (the padded-stride discriminator).
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
	// its ENQUEUE_RENDER_COMMAND).
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

	// A per-surface PENDING frame: the worker-emit's owned tight copies (what crosses the triple-buffer),
	// held until the game-thread pump creates the texture + uploads. PURE CPU bytes — NO UObject, safe off the game thread.
	struct FPendingFrame
	{
		FSurfaceId Surface;  // the surface this frame is for (rides the slot; the game-thread upload keys Textures on it)
		TArray<FAssembledRegion> Uploads;
		int32 Width = 0;
		int32 Height = 0;
		ESurfacePixelFormat Format = ESurfacePixelFormat::PremultipliedBGRA8;
	};

	// The egress holds a per-surface map of lock-free FTripleBuffer<FPendingFrame> (CathodeTripleBuffer.h): the worker
	// publishes a surface's latest frame, the game thread acquires it, with no per-frame lock on the data handoff.
	class FTextureEgress final : public ITextureEgress
	{
	public:
		explicit FTextureEgress(FEgressUploader InUploader)
			: Uploader(InUploader ? MoveTemp(InUploader) : FEgressUploader(&ProductionUpload))
		{
		}

		// The WORKER-side emit (safe off the game thread): copy + store, ZERO UObject.
		virtual FSurfaceReadinessSeam GetSeam() override
		{
			return [this](const FSurfaceReadiness& Readiness) { WorkerEmit(Readiness); };
		}

		// The GAME-THREAD upload half: for each surface with pending copied bytes, create/find its texture
		// (UObject) + marshal the render-thread upload from the stored copies.
		virtual void PumpGameThreadUploads() override
		{
			// Snapshot the per-surface triple-buffers under the brief STRUCTURE lock (the worker may FindOrAdd a new
			// surface concurrently — the lock makes the snapshot safe vs a bucket rehash), then drain each LOCK-FREE OUTSIDE the
			// lock. The TSharedPtr snapshot refs keep the buffers alive even if a surface is Released mid-drain.
			// The per-frame data handoff (Acquire) is the lock-free atomic; the lock is only the cheap map-snapshot.
			TArray<TSharedPtr<FTripleBuffer<FPendingFrame>, ESPMode::ThreadSafe>> SurfaceBuffers;
			{
				FScopeLock Lock(&FramesLock);
				Frames.GenerateValueArray(SurfaceBuffers);
			}
			for (const TSharedPtr<FTripleBuffer<FPendingFrame>, ESPMode::ThreadSafe>& TripleBuf : SurfaceBuffers)
			{
				FPendingFrame Frame;
				if (!TripleBuf->Acquire(Frame))
				{
					continue; // no new frame for this surface since the last pump
				}
				const FSurfaceId Surface = Frame.Surface;
				if (Frame.Uploads.Num() == 0)
				{
					continue;
				}

				FEgressTexture& Tex = Textures.FindOrAdd(Surface);
				if (!Tex.Texture.IsValid())
				{
					UTexture2D* NewTexture = UTexture2D::CreateTransient(Frame.Width, Frame.Height, ToTexturePixelFormat(Frame.Format));
					if (NewTexture == nullptr)
					{
						Textures.Remove(Surface);
						continue; // skip this surface, drain the rest
					}
					// CLEAR the texture on create: zero the mip BEFORE UpdateResource so the create uploads a CLEAN
					// (0) texture. The dirty-rect upload then writes ONLY the rendered rects, leaving the MARGIN deterministic-0
					// — vs the uninitialized CreateTransient GPU memory (leftover from whatever used it before), which makes a
					// readback of the margin ORDER-SENSITIVE across tests.
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
					continue; // skip this surface, drain the rest
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
			Textures.Remove(Surface); // game-thread — drops the TStrongObjectPtr -> the texture is GC-collectable.
			// Also drop this surface's triple-buffer under the STRUCTURE lock. A TSharedPtr ref still held by an
			// in-flight worker publish or a game-thread snapshot keeps the buffer alive until that ref releases (no dangle);
			// the map entry is gone, so no future pump acquires it.
			{
				FScopeLock Lock(&FramesLock);
				Frames.Remove(Surface);
			}
		}

	private:
		// WORKER-side emit: COPY the dirty bytes (the borrow is valid only synchronously — copy HERE) into an
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

			// The deep-copy (above) is done; build the frame. Fetch (or lazily create) THIS surface's
			// triple-buffer under the brief STRUCTURE lock (the worker is the sole map-ADDER — a new surface's first publish),
			// then publish LOCK-FREE on the held ref OUTSIDE the lock (the triple-buffer atomic: a slot move + a release-exchange). The
			// TSharedPtr ref keeps the buffer alive through the publish even if the game thread Removes the surface meanwhile.
			FPendingFrame Pending;
			Pending.Surface = Readiness.Surface;
			Pending.Uploads = MoveTemp(Uploads);
			Pending.Width = Buffer.Width;
			Pending.Height = Buffer.Height;
			Pending.Format = Buffer.Format;

			TSharedPtr<FTripleBuffer<FPendingFrame>, ESPMode::ThreadSafe> TripleBuf;
			{
				FScopeLock Lock(&FramesLock);
				TSharedPtr<FTripleBuffer<FPendingFrame>, ESPMode::ThreadSafe>& Slot = Frames.FindOrAdd(Readiness.Surface);
				if (!Slot.IsValid())
				{
					Slot = MakeShared<FTripleBuffer<FPendingFrame>, ESPMode::ThreadSafe>();
				}
				TripleBuf = Slot;
			}
			TripleBuf->Publish(MoveTemp(Pending)); // LOCK-FREE (the triple-buffer atomic), outside the structure lock
		}

		FEgressUploader Uploader;
		TMap<FSurfaceId, FEgressTexture> Textures;     // GAME-thread — the UObject textures
		// The per-surface lock-free triple-buffers. The per-frame DATA handoff stays LOCK-FREE: each
		// FTripleBuffer's publish/acquire is atomic. FramesLock guards ONLY the MAP STRUCTURE (FindOrAdd /
		// GenerateValueArray / Remove) — a cheap pointer-fetch, NOT a per-frame deep-copy contention — because
		// the worker's FindOrAdd (a new surface's first publish) can REHASH the bucket array CONCURRENT with the game thread's
		// snapshot/find (UE TMap is unsafe under concurrent read+add; Find(X)-after-add(X) is ordered by the outbound release
		// edge, but Find(X) still overlaps add(Y)). A lock is robust for ANY surface count (vs Reserve's hidden capacity bound).
		// The buffers are TSharedPtr(ThreadSafe) so an in-flight worker publish-ref / game-thread snapshot-ref survives a
		// concurrent Remove (no dangling pointer).
		TMap<FSurfaceId, TSharedPtr<FTripleBuffer<FPendingFrame>, ESPMode::ThreadSafe>> Frames;
		FCriticalSection FramesLock;
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
