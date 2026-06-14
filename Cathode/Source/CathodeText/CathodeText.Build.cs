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
		// ACTIVATED (sprint 010 bring-up): the shaper impl #includes the HarfBuzz headers (hb.h) and calls
		// the hb_* C API, so this module links HarfBuzz. Wired to the conventional ThirdParty layout —
		// Source/ThirdParty/harfbuzz/{include, Win64/harfbuzz.lib} — populated by the vendor-lib tooling
		// build from vendor/lib/cathode/harfbuzz/.
		// NOTE (tooling-gap recipe): build HarfBuzz static; copy the hb*.h public headers into include/.
		// HarfBuzz needs no ICU/FreeType for our use (built standalone with its own UCD tables).
		string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");

		PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "harfbuzz", "include"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "harfbuzz", "Win64", "harfbuzz.lib"));
		}
	}
}
