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
			"CathodeResolver", // document_container resource callbacks route to IResourceResolver
		});

		// PRIVATE: CathodeRasterizer is used ONLY in the .cpp (CathodeResourceRouting -> DecodeImageSize, the
		// Blend2D image-info decode for get_image_size), never in a public CathodeLayout header — so it is a
		// private dep: it does NOT leak to CathodeLayout's consumers (e.g. CathodeDom).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CathodeRasterizer",
		});

		// --- Bundled ThirdParty: litehtml (the layout/CSS engine) ---
		//
		// The document_container impl #includes litehtml and drives its
		// layout API, so this module links litehtml. Wired to the conventional ThirdParty layout —
		// Source/ThirdParty/litehtml/{include, Win64/<lib>.lib} — populated by the vendor library build
		// from vendor/lib/cathode/litehtml/. litehtml statically embeds its HTML parser (gumbo) as a separate
		// CMake target, so the build emits BOTH litehtml.lib and gumbo.lib; both are linked here.
		// NOTE (build recipe): build litehtml static (its CMake produces litehtml + gumbo); copy the
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
