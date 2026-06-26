using UnrealBuildTool;

public class CathodeExample : ModuleRules
{
	public CathodeExample(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 031 — the REFERENCE EXAMPLE INTEGRATION (host-agnostic; ships with the standalone Cathode library). The canonical
		// "how a game wires into Cathode": a host object implementing ICathodeGameBridge (CathodeDom) registered via the
		// owner's injecting Install (CathodeComposition). The dependency arrow is game -> Cathode ONLY — this module names
		// NO host (Uptime*) type and (in production) NO Engine; it is a neutral REGISTRANT, the same inversion as every other
		// Cathode boundary. An integrator copies this module's shape to wire their own game in.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CathodeDom",         // ICathodeGameBridge — the host-registered bridge interface
			"CathodeComposition", // FCompositionSurfaceOwner::Install (the injecting overload) — the registration point
		});

		// TEST-ONLY (non-Shipping): the mutate-and-observe proof drives the real worker + GPU-reads the composed surface.
		// The production reference code (the bridge + the Install wiring) names NONE of these.
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"CoreUObject", "Engine", // UTexture2D for the readback
				"RHI", "RenderCore",     // the GPU egress readback
				"Projects",              // IPluginManager — the proof font fixture
				"CathodeRuntime",        // RestartWorkerForTesting (the IN-SUITE worker)
			});
		}
	}
}
