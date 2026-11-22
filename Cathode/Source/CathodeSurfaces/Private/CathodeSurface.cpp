#include "CathodeSurfaces.h"

namespace Cathode
{

FSurface::FSurface(FSurfaceId InId, FSurfaceRegistration&& Registration)
	: Id(InId)
	, Width(Registration.Width)
	, Height(Registration.Height)
	, Format(Registration.Format)
	, Document(MoveTemp(Registration.Document))
	, Target(MoveTemp(Registration.Target))
{
	// Dims/format are captured here and never mutated — no resize path, by design. The document + target
	// handles transfer ownership into the surface (freed when the surface is destroyed; no leak).
}

bool FSurface::RenderAndRelay(const FSurfaceReadinessSeam& Egress)
{
	if (!Document.IsValid() || !Target.IsValid())
	{
		return false;
	}

	// Render the surface's document at its fixed available width, hand the resulting paint batch to the
	// render target (the Rasterizer's producer), and let it rasterize + swap-if-changed. The batch is carried opaque;
	// Surfaces never interprets it.
	const FSurfacePaintBatch Batch = Document->RenderFrame(Width);
	const bool bChanged = Target->ProduceFrame(Batch);

	// Event-driven, only-on-change: no real change => no readiness emitted (idle surfaces stay silent).
	if (!bChanged)
	{
		return false;
	}

	// Relay: stamp the producer's frame with THIS surface's identity (the address the Rasterizer's per-producer seam
	// lacks) and emit to Egress. The engine references the buffer; Egress copies on the event.
	if (Egress)
	{
		FSurfaceReadiness Readiness;
		Readiness.Surface = Id;
		Readiness.Frame = Target->GetReadyFrame();
		Egress(Readiness);
	}
	return true;
}

} // namespace Cathode
