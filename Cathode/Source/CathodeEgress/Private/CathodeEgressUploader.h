#pragma once

#include "CoreMinimal.h"
#include "CathodeEgress.h"     // ITextureEgress, FSurfaceId
#include "RHIResources.h"      // FRHITexture
#include "RHICommandList.h"    // FUpdateTextureRegion2D
#include "Templates/Function.h"

// The INJECTABLE upload seam (Private — not part of the public API; only the egress impl + its in-module tests see
// it). Production wires the real RHIUpdateTexture2D; a test injects a SPY that captures the egress's assembled
// values (region/pitch/source) to bind the marshaling and on-change behavior the on-GPU readback cannot see.

namespace Cathode
{

// One assembled upload, handed to the uploader ON THE RENDER THREAD (inside the egress's ENQUEUE_RENDER_COMMAND).
// The egress fills this from the readiness it received: the destination RHI texture (resolved on the render
// thread), the dirty region, the source pitch (== the neutral view's Stride), and a pointer into the
// COMMAND-OWNED copy (valid for the duration of the upload — never the borrowed front buffer).
struct FEgressUpload
{
	FRHITexture* Texture = nullptr;
	FUpdateTextureRegion2D Region{ 0u, 0u, 0, 0, 0u, 0u };
	uint32 SourcePitch = 0;            // bytes per row — the source Stride (NOT Width*4 on a padded buffer)
	const uint8* SourceData = nullptr; // into the command-owned copy
	int32 SourceNumBytes = 0;
	FSurfaceId Surface;
};

// The uploader: runs the upload on the render thread. Production = RHIUpdateTexture2D; test = a capturing spy.
using FEgressUploader = TFunction<void(const FEgressUpload&)>;

// Test-only factory: construct the egress with an injected uploader (production uses the real RHIUpdateTexture2D).
// Same module as the egress impl — no _API export needed; the in-module tests call it directly.
TUniquePtr<ITextureEgress> CreateTextureEgressWithUploader(FEgressUploader Uploader);

} // namespace Cathode
