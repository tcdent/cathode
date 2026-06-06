#include "CathodeSurfaces.h"

namespace Cathode
{

namespace
{

// The runtime->surfaces registry — the many-surfaces model: one runtime holds many surfaces, same global
// state. Surfaces are owned here (TUniquePtr); Unregister and teardown free them (and, transitively, their
// document + render-target handles) — no leak. Ids are monotonic so an unregistered id is never reused
// (stable addressing for the readiness relay). The class is private to this TU; only CreateSurfaceRegistry
// is exposed (mirrors 005's engine factory).
class FSurfaceRegistry final : public ISurfaceRegistry
{
public:
	virtual FSurfaceId Register(FSurfaceRegistration&& Registration) override
	{
		const FSurfaceId Id{ NextId++ };
		Surfaces.Add(Id.Value, MakeUnique<FSurface>(Id, MoveTemp(Registration)));
		return Id;
	}

	virtual void Unregister(FSurfaceId Surface) override
	{
		// Remove => the TUniquePtr<FSurface> dtor frees the surface + its owned handles. Unknown id = no-op.
		Surfaces.Remove(Surface.Value);
	}

	virtual FSurface* Find(FSurfaceId Surface) override
	{
		const TUniquePtr<FSurface>* Found = Surfaces.Find(Surface.Value);
		return Found ? Found->Get() : nullptr;
	}

	virtual int32 Num() const override
	{
		return Surfaces.Num();
	}

	virtual void DriveAll(const FSurfaceReadinessSeam& Egress) override
	{
		// One runtime drives each LIVE surface; RenderAndRelay gates the emit on a real change (idle stay
		// silent). Iterating the live map means an unregistered surface is no longer driven.
		for (const TPair<int32, TUniquePtr<FSurface>>& Pair : Surfaces)
		{
			if (Pair.Value.IsValid())
			{
				Pair.Value->RenderAndRelay(Egress);
			}
		}
	}

private:
	TMap<int32, TUniquePtr<FSurface>> Surfaces;
	int32 NextId = 0; // monotonic; FSurfaceId default is INDEX_NONE, so 0 is the first valid id
};

} // namespace

TUniquePtr<ISurfaceRegistry> CreateSurfaceRegistry()
{
	return MakeUnique<FSurfaceRegistry>();
}

} // namespace Cathode
