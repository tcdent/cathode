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

	// The readiness seam to hand to ISurfaceRegistry::DriveAll. On each surface CHANGE (only-on-change is
	// Surfaces' contract — an idle surface emits nothing, so the egress uploads nothing): create/find the
	// surface's UTexture2D, COPY the dirty bytes at the callback (the borrow is valid only here), and marshal
	// the upload onto the render thread.
	virtual FSurfaceReadinessSeam GetSeam() = 0;

	// The surface's GPU texture (the device material samples it; tests read it back). Null until the surface's
	// first frame has been seen.
	virtual UTexture2D* GetTexture(FSurfaceId Surface) const = 0;

	// Drop a surface's texture (on unregister) — releases the GC hold so it can be collected.
	virtual void Release(FSurfaceId Surface) = 0;
};

// Construct the egress with the PRODUCTION uploader (the real RHIUpdateTexture2D). The single construction point;
// the device-spawn path (integration) creates one and wires GetSeam() into the registry's DriveAll.
CATHODEEGRESS_API TUniquePtr<ITextureEgress> CreateTextureEgress();

} // namespace Cathode
