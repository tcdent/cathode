#pragma once

#include "CoreMinimal.h"
#include "CathodeGameBridge.h" // Cathode::ICathodeGameBridge — the host-registered bridge interface (CathodeDom)

namespace CathodeExample
{

/**
 * FExampleGameBridge — 031 SLICE 1: the REFERENCE host object an integrator registers INTO Cathode's game.* bridge.
 *
 * This is the canonical "register a game object into Cathode": a host class implements Cathode::ICathodeGameBridge with its
 * OWN state (here a tick counter + a small entity-name table — a stand-in for a real game's frame clock + named entities),
 * and is handed to FCompositionSurfaceOwner::Install(..., MakeUnique<FExampleGameBridge>()). Cathode then exposes the
 * bridge as the `game.*` global (game.tickCount / game.getEntityName) to author screens — from the single generate-from-
 * interface projection, so the runtime bindings, the .d.ts, and the gate stay in lock-step.
 *
 * The dependency arrow is game -> Cathode ONLY: this class names a Cathode type (the interface it implements), never the
 * reverse. An integrator copies this shape, swapping the stand-in state for their real game state (a real tick, a real
 * entity registry). Cathode has no knowledge of this class — it calls it only through ICathodeGameBridge.
 */
class FExampleGameBridge final : public Cathode::ICathodeGameBridge
{
public:
	// --- Cathode::ICathodeGameBridge (the registered surface) ---
	virtual int32 GetTickCount() const override { return Tick; }
	virtual bool GetEntityName(const FString& Id, FString& OutName) const override
	{
		if (const FString* const Found = Entities.Find(Id))
		{
			OutName = *Found;
			return true;
		}
		return false; // unknown id -> game.getEntityName(id) is null (the integrator's lookup miss)
	}

	// --- Host-side controls (the reference host OWNS its state; Cathode never sets these) ---
	// The tick: a settable seed + an advance, so the example shows a LIVE tick (AdvanceTick per host frame) AND the proof
	// asserts an exact value (SetTick) — the integrator backs GetTickCount with their real frame clock.
	void SetTick(int32 InTick) { Tick = InTick; }
	void AdvanceTick(int32 By = 1) { Tick += By; }
	// Name an entity the screen can resolve (game.getEntityName(id)) — the integrator backs this with their real registry.
	void AddEntity(const FString& Id, const FString& Name) { Entities.Add(Id, Name); }

private:
	int32 Tick = 0;
	TMap<FString, FString> Entities;
};

} // namespace CathodeExample
