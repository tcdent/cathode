using System.IO;
using UnrealBuildTool;

public class CathodeRasterizer : ModuleRules
{
	public CathodeRasterizer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Minimal Unreal deps: the Rasterizer is pure CPU raster over our own types (FColor / FBox2f /
		// FVector2f / TArray / TUniquePtr — all Core). It does NOT depend on CathodeRuntime — the
		// input/output seams are generalizable, wired later, not a compile-time coupling.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		// --- Bundled ThirdParty: Blend2D (the paint backend) + AsmJit (its JIT pipeline compiler) ---
		//
		// ACTIVATED (sprint 010 bring-up): the Blend2D-backed raster impl #includes blend2d.h and calls the
		// BL* API, so this module links Blend2D. Wired to the conventional ThirdParty layout —
		// Source/ThirdParty/{blend2d,asmjit}/{include, Win64/<lib>.lib} — populated by the vendor-lib tooling
		// build. Built STATIC (BL_STATIC): AsmJit is compiled INTO blend2d.lib (it lives at
		// blend2d/3rdparty/asmjit in the build tree, ship-with-JIT), so there is no separate asmjit.lib to
		// link and no runtime DLL/delay-load. AsmJit's include path is still added because Blend2D's public
		// headers may reference it. C-string defines: consumers compiling against a static Blend2D set
		// BL_STATIC so the headers don't emit dllimport.
		// NOTE (tooling-gap recipe): emit blend2d.lib as a static lib with AsmJit embedded; copy the
		// blend2d/ and asmjit/ public headers into the respective include/ dirs.
		string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");

		PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "blend2d", "include"));
		PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "asmjit", "include"));

		// Static Blend2D: tell its headers not to expect a DLL import.
		PublicDefinitions.Add("BL_STATIC=1");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "blend2d", "Win64", "blend2d.lib"));
		}
	}
}
