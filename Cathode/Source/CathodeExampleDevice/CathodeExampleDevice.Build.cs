using UnrealBuildTool;

public class CathodeExampleDevice : ModuleRules
{
	public CathodeExampleDevice(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 031 S3 — the REFERENCE in-world TARGETABLE device actor (host-agnostic). A screen-mesh example actor samples the
		// Cathode surface's egress texture onto a MID, and implements ICathodeTargetableSurface (exposes its FSurfaceId) so a
		// host's targeting resolves it BY the interface. The plugin names NO host type: NO IPlayerInteractable here — the
		// player-ray is the INTEGRATOR's host-side half (the corrected shape). Engine-linking (an AActor + a MID); ZERO Uptime*.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",        // AActor / UObject / UStaticMeshComponent / UMaterialInstanceDynamic
			"Engine",             // the actor + the MID + UTexture2D
			"CathodeComposition", // FCompositionSurfaceOwner::GetTexture (the egress texture the MID samples) + Designate/DeliverIntent
			"CathodeSurfaces",    // FSurfaceId + ICathodeTargetableSurface (the targetable-surface contract)
		});

		// TEST-ONLY (non-Shipping): the targeting proof drives the worker + GPU-reads the surface keydown result.
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"RHI", "RenderCore", // the GPU readback for the targeting proof
				"Projects",          // IPluginManager — the proof font fixture
				"CathodeRuntime",    // RestartWorkerForTesting (the IN-SUITE worker)
			});
		}
	}
}
