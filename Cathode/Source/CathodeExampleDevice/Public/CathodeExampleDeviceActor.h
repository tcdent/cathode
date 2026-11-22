#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CathodeSurfaces.h" // Cathode::FSurfaceId, Cathode::ICathodeTargetableSurface
#include "CathodeExampleDeviceActor.generated.h"

namespace Cathode { class FCompositionSurfaceOwner; }
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;

/**
 * ACathodeExampleDeviceActor — the REFERENCE in-world TARGETABLE device actor (host-agnostic; ships with the plugin).
 *
 * The canonical "a Cathode surface on a 3D object, targetable by the host." It does two things:
 *  - SAMPLES the Cathode surface's egress texture onto a MID on its screen mesh each tick (the in-world device pattern). The
 *    mesh + its material are the INTEGRATOR's (assigned on the actor); Cathode provides only the surface TEXTURE.
 *  - implements Cathode::ICathodeTargetableSurface (exposes its FSurfaceId) so the host's TARGETING resolves this actor's
 *    Cathode surface BY the interface, then DesignateActiveSurface + DeliverIntent via the owner.
 *
 * The dependency arrow is game -> Cathode only: this actor names a Cathode type (the interface + the owner), never a host
 * type. It does NOT implement IPlayerInteractable — the player-ray targeting is the INTEGRATOR's HOST-SIDE half (the host's
 * IPlayerInteractable resolves Cathode surfaces by ICathodeTargetableSurface; see the example docs). So the plugin stays
 * host-agnostic (ZERO Uptime*), and the inversion holds.
 */
UCLASS()
class CATHODEEXAMPLEDEVICE_API ACathodeExampleDeviceActor : public AActor, public Cathode::ICathodeTargetableSurface
{
	GENERATED_BODY()

public:
	ACathodeExampleDeviceActor();
	virtual void Tick(float DeltaTime) override;

	// --- Cathode::ICathodeTargetableSurface (the Cathode-owned targetability contract) ---
	virtual Cathode::FSurfaceId GetSurfaceId() const override { return Surface; }

	// The integrator wires the actor to its Cathode composition surface (the owner [held NON-OWNING — the integrator's
	// subsystem owns the composition] + the surface id). The actor then binds the surface's egress texture onto its screen
	// MID each tick.
	void BindSurface(Cathode::FCompositionSurfaceOwner* InOwner, Cathode::FSurfaceId InSurface);

private:
	UPROPERTY(VisibleAnywhere, Category = "Cathode") TObjectPtr<UStaticMeshComponent> ScreenMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ScreenMID;
	Cathode::FCompositionSurfaceOwner* Owner = nullptr; // non-owning (the integrator's subsystem owns the composition)
	Cathode::FSurfaceId Surface;
};
