using System.IO;
using UnrealBuildTool;

public class CathodeLayout : ModuleRules
{
	public CathodeLayout(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Layout orchestration over our own neutral types (FBox2f/FColor/FString/TArray). Still NOT coupled to
		// CathodeRuntime / CathodeText — the paint-op output and text-measurement input are engine-neutral seams.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CathodeResolver", // sprint 009: document_container resource callbacks route to IResourceResolver
		});

		// PRIVATE: CathodeRasterizer is used ONLY in the .cpp (CathodeResourceRouting -> DecodeImageSize, the
		// Blend2D image-info decode for get_image_size), never in a public CathodeLayout header — so it is a
		// private dep: it does NOT leak to CathodeLayout's consumers (e.g. CathodeDom). (sprint 011 [c])
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CathodeRasterizer",
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
