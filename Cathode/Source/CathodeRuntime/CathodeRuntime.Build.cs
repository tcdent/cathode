using System.IO;
using UnrealBuildTool;

public class CathodeRuntime : ModuleRules
{
	public CathodeRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The runtime module's complete Unreal dependency set. Per the architecture doc it
		// depends only on Unreal modules and the bundled libraries: Core/CoreUObject are the
		// base object/runtime layer, Engine owns UTexture2D egress, RHI/RenderCore drive the
		// texture upload (regional RHIUpdateTexture2D), and Slate hosts the surface/texture.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RHI",
			"RenderCore",
			"Slate",
		});

		// --- Bundled ThirdParty libraries (Pattern-B structure, wired from this module) ---
		//
		// SCAFFOLD ONLY (sprint 001): the structure below mirrors the project's native-library
		// convention (JoystickPlugin/SDL2 — include path + additional library + runtime dep +
		// delay-load, wired from the consuming module's Build.cs). The lib-linking lines are
		// intentionally STUBBED/COMMENTED: nothing here pretends a binary exists. The five
		// libraries are vendored as SOURCE under vendor/lib/cathode/<lib>/ (via the plugin's
		// install.sh) and are BUILT FROM SOURCE into Source/ThirdParty/<lib>/ at 002. Only when
		// 002 produces headers + platform binaries do the lines below get uncommented and filled.
		//
		// string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");
		//
		// foreach (string lib in new[] { "quickjs-ng", "litehtml", "harfbuzz", "blend2d", "asmjit" })
		// {
		//     // Headers (populated at 002 from the built library):
		//     PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, lib, "include"));
		// }
		//
		// if (Target.Platform == UnrealTargetPlatform.Win64)
		// {
		//     // Static/import libraries (002 build output):
		//     // PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "<lib>", "Win64", "<lib>.lib"));
		//     // Runtime DLLs + delay-load, staged via Git LFS (002):
		//     // RuntimeDependencies.Add(Path.Combine(ThirdParty, "<lib>", "Win64", "<lib>.dll"), StagedFileType.NonUFS);
		//     // PublicDelayLoadDLLs.Add("<lib>.dll");
		// }
		// else if (Target.Platform == UnrealTargetPlatform.Linux)
		// {
		//     // PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "<lib>", "Linux", "lib<lib>.a"));
		// }
	}
}
