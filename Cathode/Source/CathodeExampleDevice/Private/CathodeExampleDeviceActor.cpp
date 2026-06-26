#include "CathodeExampleDeviceActor.h"

#include "CathodeCompositionOwner.h" // Cathode::FCompositionSurfaceOwner::GetTexture
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"

ACathodeExampleDeviceActor::ACathodeExampleDeviceActor()
{
	PrimaryActorTick.bCanEverTick = true;
	ScreenMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScreenMesh"));
	RootComponent = ScreenMesh;
}

void ACathodeExampleDeviceActor::BindSurface(Cathode::FCompositionSurfaceOwner* InOwner, Cathode::FSurfaceId InSurface)
{
	Owner = InOwner;   // non-owning — the integrator's subsystem owns the composition + pumps it
	Surface = InSurface;
}

void ACathodeExampleDeviceActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// THE IN-WORLD DEVICE PATTERN: bind the Cathode surface's egress texture onto the screen MID. Cathode provides the
	// TEXTURE (FCompositionSurfaceOwner::GetTexture); the screen MESH + its base MATERIAL (with a "SurfaceTexture" param) are
	// the INTEGRATOR's, assigned on the actor. Null-safe: if not yet bound / no texture / no material, this is a no-op (the
	// targeting still works — GetSurfaceId is independent of the render).
	if (Owner == nullptr || ScreenMesh == nullptr) { return; }
	UTexture2D* const SurfaceTex = Owner->GetTexture(Surface);
	if (SurfaceTex == nullptr) { return; }

	if (ScreenMID == nullptr)
	{
		if (UMaterialInterface* const Base = ScreenMesh->GetMaterial(0)) // the integrator's assigned screen material
		{
			ScreenMID = UMaterialInstanceDynamic::Create(Base, this);
			ScreenMesh->SetMaterial(0, ScreenMID);
		}
	}
	if (ScreenMID != nullptr)
	{
		ScreenMID->SetTextureParameterValue(TEXT("SurfaceTexture"), SurfaceTex);
	}
}
