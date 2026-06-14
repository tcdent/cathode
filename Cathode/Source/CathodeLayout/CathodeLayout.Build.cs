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
			"CathodeResolver", // sprint 009: the document_container's resource callbacks route to IResourceResolver
		});

		// --- Bundled ThirdParty: litehtml (the layout/CSS engine) ---
		//
		// ACTIVATED (sprint 010 bring-up): the document_container impl #includes litehtml and drives its
		// layout API, so this module links litehtml. Wired to the conventional ThirdParty layout —
		// Source/ThirdParty/litehtml/{include, Win64/<lib>.lib} — populated by the vendor-lib tooling build
		// from vendor/lib/cathode/litehtml/. litehtml statically embeds its HTML parser (gumbo) as a separate
		// CMake target, so the tooling emits BOTH litehtml.lib and gumbo.lib; both are linked here.
		// NOTE (tooling-gap recipe): build litehtml static (its CMake produces litehtml + gumbo); copy the
		// litehtml public headers (litehtml.h + litehtml/) into include/.
		string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");

		PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "litehtml", "include"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "litehtml", "Win64", "litehtml.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "litehtml", "Win64", "gumbo.lib"));
		}
	}
}
