#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace Cathode
{

class ICathodeJsRuntime; // the seam receives the worker's runtime by reference (forward-decl suffices)

// A worker -> host outbound notification ("frame ready"). Carries ONLY the surface id (the NOTIFICATION); the composed
// pixels live in the egress's per-surface holder (the double-buffer), NOT the message (architecture.md:102
// — keeping the runtime message decoupled from egress data). Public so the surface-load owner +
// CathodeEgress can produce/consume it across module boundaries.
struct FHostOutboundMessage
{
	int32 SurfaceTag = 0;
};

// The Phase-5 worker-side relayout seam: the body the worker runs each tick (ON the worker thread) to relayout/raster
// dirtied surfaces and append one frame-ready per surface that produced a frame. Runtime Core only SEQUENCES the call +
// EMITS frame-ready. The seam receives the worker's `ICathodeJsRuntime&` (the tick passes its own
// Runtime): the only way worker-side code can reach the worker-thread-bound runtime, so the seam can BUILD a real
// per-surface FDomDocument (CreateDomDocument + Evaluate the JS DOM-build) ON the worker before composing it. Installed
// via FCathodeRuntimeModule::InstallSurfaceRelayoutSeam.
using FSurfaceRelayoutSeam = TFunction<void(ICathodeJsRuntime& /*Runtime*/, TArray<FHostOutboundMessage>& /*OutFrameReady*/)>;

} // namespace Cathode
