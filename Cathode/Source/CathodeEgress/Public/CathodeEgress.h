#pragma once

#include "CoreMinimal.h"
#include "CathodeSurfaces.h"          // FSurfaceReadinessSeam, FSurfaceId
#include "Templates/UniquePtr.h"

class UTexture2D;

// Cathode Texture Egress (sprint 024) — the SOLE RHI touchpoint. It subscribes to CathodeSurfaces' readiness
// relay (FSurfaceReadinessSeam), owns one UTexture2D per surface, and on each change COPIES the surface's dirty
// bytes (the 1a neutral view) and uploads them to the GPU on the render thread. It never reads a vendor type:
// it consumes only the neutral FSurfaceBufferRef (Pixels/Stride/dims/format).
//
// ALTITUDE: subscribe, create-the-texture, copy, upload — NOT a texture-streaming system, NOT a material/sampler
// layer (the device-spawn caller binds the UTexture2D to its material; that wiring is integration). Slice 1b is
// the first FULL-TEXTURE upload; the dirty-rect regional upload is slice 2.

namespace Cathode
{

// The Texture Egress façade. Construct one, hand GetSeam() to ISurfaceRegistry::DriveAll, and read the per-surface
// UTexture2D for the device material / readback.
class ITextureEgress
{
public:
	virtual ~ITextureEgress() = default;

	// The readiness seam to hand to ISurfaceRegistry::DriveAll. 028 S2 thread-split — this is the WORKER-side EMIT:
	// on each surface CHANGE it COPIES the dirty bytes at the callback (the borrow is valid only here) and STORES the
	// owned copy in a thread-safe per-surface holder (the stubbed double-buffer). It does ZERO UObject work — texture
	// creation + the RHI upload are DEFERRED to PumpGameThreadUploads() on the game thread. Safe to drive from the
	// worker thread (no UObject / no RHI off the game thread).
	virtual FSurfaceReadinessSeam GetSeam() = 0;

	// 028 S2 — the GAME-THREAD upload half: for each surface with pending copied bytes (from GetSeam's worker-emit),
	// create/find its UTexture2D (CreateTransient/UpdateResource — UObject, game-thread only) and marshal the upload
	// onto the render thread (the existing RHIUpdateTexture2D). MUST run on the game thread. The host drains the
	// worker's frame-ready outbound and calls this to pump the uploads.
	virtual void PumpGameThreadUploads() = 0;

	// The surface's GPU texture (the device material samples it; tests read it back). Null until the surface's
	// first frame has been pumped.
	virtual UTexture2D* GetTexture(FSurfaceId Surface) const = 0;

	// Drop a surface's texture (on unregister) — releases the GC hold so it can be collected.
	virtual void Release(FSurfaceId Surface) = 0;
};

// Construct the egress with the PRODUCTION uploader (the real RHIUpdateTexture2D). The single construction point;
// the device-spawn path (integration) creates one and wires GetSeam() into the registry's DriveAll.
CATHODEEGRESS_API TUniquePtr<ITextureEgress> CreateTextureEgress();

} // namespace Cathode
