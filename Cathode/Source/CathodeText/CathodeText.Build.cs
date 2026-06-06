using System.IO;
using UnrealBuildTool;

public class CathodeText : ModuleRules
{
	public CathodeText(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Minimal Unreal deps: shaping orchestration over our own types + font byte loading via Core's
		// FFileHelper (local FS / UE file APIs). It does NOT depend on CathodeRuntime or CathodeRasterizer
		// — the glyph-run / font seams are engine-neutral (bytes + handle), wired across modules later.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		// --- Bundled ThirdParty: HarfBuzz (the text shaper) ---
		//
		// SCAFFOLD ONLY (sprint 004): the structure below mirrors the project's native-library convention
		// and the CathodeRasterizer honest-stub. The lib-linking lines are intentionally STUBBED/COMMENTED:
		// nothing here pretends a binary exists. HarfBuzz is vendored as SOURCE under
		// vendor/lib/cathode/harfbuzz/ (via the plugin's install.sh) and is BUILT FROM SOURCE into
		// Source/ThirdParty/harfbuzz/ at the execution-approval sprint. The shaper (Slice 3) #includes the
		// HarfBuzz headers; only when the execution sprint produces headers + platform binaries do the
		// lines below get uncommented.
		//
		// string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");
		//
		// // Headers (populated at the execution sprint from the built library):
		// PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "harfbuzz", "include"));
		//
		// if (Target.Platform == UnrealTargetPlatform.Win64)
		// {
		//     // Static/import library (execution-sprint build output):
		//     // PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "harfbuzz", "Win64", "harfbuzz.lib"));
		// }
		// else if (Target.Platform == UnrealTargetPlatform.Linux)
		// {
		//     // PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "harfbuzz", "Linux", "libharfbuzz.a"));
		// }
	}
}
