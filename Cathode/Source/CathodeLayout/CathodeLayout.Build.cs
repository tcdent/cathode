using System.IO;
using UnrealBuildTool;

public class CathodeLayout : ModuleRules
{
	public CathodeLayout(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Minimal Unreal deps: layout orchestration over our own neutral types (FBox2f/FColor/FString/
		// TArray). It does NOT depend on CathodeRuntime / CathodeRasterizer / CathodeText — the paint-op
		// output, the text-measurement input, and the DOM source are engine-neutral seams, wired across
		// modules later.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		// --- Bundled ThirdParty: litehtml (the layout/CSS engine) ---
		//
		// SCAFFOLD ONLY (sprint 005): the structure mirrors the project's native-library convention and the
		// 003/004 honest-stub. The lib-linking lines are intentionally STUBBED/COMMENTED: nothing pretends a
		// binary exists. litehtml is vendored as SOURCE under vendor/lib/cathode/litehtml/ (via install.sh)
		// and is BUILT FROM SOURCE into Source/ThirdParty/litehtml/ at the execution-approval sprint. The
		// document_container impl #includes litehtml; only when the execution sprint produces headers +
		// platform binaries do the lines below get uncommented.
		//
		// string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");
		// PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "litehtml", "include"));
		//
		// if (Target.Platform == UnrealTargetPlatform.Win64)
		// {
		//     // PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "litehtml", "Win64", "litehtml.lib"));
		// }
		// else if (Target.Platform == UnrealTargetPlatform.Linux)
		// {
		//     // PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "litehtml", "Linux", "liblitehtml.a"));
		// }
	}
}
