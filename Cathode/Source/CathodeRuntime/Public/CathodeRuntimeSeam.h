#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace Cathode
{

// A worker -> host outbound notification ("frame ready"). Carries ONLY the surface id (the NOTIFICATION); the composed
// pixels live in the egress's per-surface holder (the double-buffer), NOT the message (architecture.md:102; 028 RQ2 (b)
// — keeping the runtime message decoupled from egress data). Moved to Public (028 S2) so the surface-load owner +
// CathodeEgress can produce/consume it across module boundaries.
struct FHostOutboundMessage
{
	int32 SurfaceTag = 0;
};

// The Phase-5 worker-side relayout seam: the body the worker runs each tick to relayout/raster dirtied surfaces and
// append one frame-ready per surface that produced a frame. Runtime Core only SEQUENCES the call + EMITS frame-ready.
// Installed via FCathodeRuntimeModule::InstallSurfaceRelayoutSeam (028 S2) — moved to Public so the surface-load owner
// (and the S2 gate test) can install it onto the auto-started worker.
using FSurfaceRelayoutSeam = TFunction<void(TArray<FHostOutboundMessage>& /*OutFrameReady*/)>;

} // namespace Cathode
