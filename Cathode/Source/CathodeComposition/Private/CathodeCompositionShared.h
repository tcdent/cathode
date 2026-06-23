#pragma once

#include "CathodeSurfaces.h" // FSurfaceRegistration, ESurfacePixelFormat
#include "Templates/SharedPointer.h"

namespace Cathode
{
class ILayoutEngine;
class FShaperTextMeasurer;

// 028 S6 — the SHARED-ENGINE composition variant (internal to CathodeComposition). The surface-load owner builds ONE
// real DOM-layout engine (the doc's mutation-marked engine, over a measurer) and the COMPOSITION renders off that SAME
// engine instead of constructing its own — so a DOM mutation's MarkNodeDirty (which lands on this engine via the doc's
// MarkDirty) drives the composition re-compose. This UNIFIES the two engines the build-once path had (the DOM's +
// the composition's separate one); "each surface = ONE litehtml document" (architecture.md).
//
// Contract: SharedEngine is BORROWED — the owner constructs it, calls SetResourceResolver + (via CreateDomDocument)
// SetDocument BEFORE this call, and must outlive the returned surface. SharedAdapter is the SAME measurer the engine
// holds (the same-bytes anchor) — it also backs the rasterizer render target here.
FSurfaceRegistration MakeCompositionSurfaceRegistrationShared(
	ILayoutEngine& SharedEngine,
	TSharedRef<FShaperTextMeasurer> SharedAdapter,
	int32 Width,
	int32 Height,
	ESurfacePixelFormat Format);

} // namespace Cathode
