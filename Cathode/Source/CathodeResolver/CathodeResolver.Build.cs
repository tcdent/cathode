using UnrealBuildTool;

public class CathodeResolver : ModuleRules
{
	public CathodeResolver(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Core ONLY — the Resource Resolver is small + PURE-BESPOKE and COUPLES-TO-NONE: it DEFINES the
		// neutral seams (IBundleSource, IResourceResolver) and wraps no vendor; 005/002/004 are the consumers
		// that import them. No ThirdParty block (no vendor). The UE-asset IBundleSource backend is built at
		// integration behind the neutral seam, so even the UE-asset coupling stays out of this module.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});
	}
}
