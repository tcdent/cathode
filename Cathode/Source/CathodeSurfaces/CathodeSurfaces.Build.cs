using UnrealBuildTool;

public class CathodeSurfaces : ModuleRules
{
	public CathodeSurfaces(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Core ONLY. Surfaces is the first PURE-BESPOKE Cathode component — it wraps NO vendor library, so
		// there is deliberately NO ThirdParty block (not even a commented/stubbed one, unlike 003/004/005).
		// It does NOT depend on CathodeRuntime / CathodeRasterizer / CathodeLayout / CathodeText: the
		// render-target, the document, and the readiness relay are engine-neutral seams wired across modules
		// at integration. "Couples to none" must hold at the Build.cs too.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});
	}
}
