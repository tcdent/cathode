using UnrealBuildTool;

public class CathodeResolver : ModuleRules
{
	public CathodeResolver(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Core ONLY — the Resource Resolver is small + PURE-BESPOKE and COUPLES-TO-NONE: it DEFINES the
		// neutral seams (IBundleSource, IResourceResolver) and wraps no vendor; CathodeLayout/CathodeRuntime/CathodeText are the consumers
		// that import them. No ThirdParty block (no vendor). The UE-asset IBundleSource backend is built at
		// integration behind the neutral seam, so even the UE-asset coupling stays out of this module.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		// TEST-ONLY conditional dep (real-bytes proof): the production-backend test uses IPluginManager
		// (Projects module) to resolve the plugin dir for the committed on-disk fixture (the MountProof/Verify
		// convention). The use is WITH_DEV_AUTOMATION_TESTS-gated; non-Shipping so a Shipping CathodeResolver carries
		// no Projects dependency — the module stays Core-only / couples-to-none in production.
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.Add("Projects");
		}
	}
}
