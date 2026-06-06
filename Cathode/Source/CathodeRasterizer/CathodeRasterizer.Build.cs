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
		// SCAFFOLD ONLY (sprint 003): the structure below mirrors the project's native-library convention
		// and the CathodeRuntime honest-stub. The lib-linking lines are intentionally STUBBED/COMMENTED:
		// nothing here pretends a binary exists. Blend2D + AsmJit are vendored as SOURCE under
		// vendor/lib/cathode/{blend2d,asmjit}/ (via the plugin's install.sh) and are BUILT FROM SOURCE
		// into Source/ThirdParty/{blend2d,asmjit}/ at the execution-approval sprint. AsmJit relocates to
		// blend2d/3rdparty/asmjit at that build (ship-with-JIT; the no-JIT path is incomplete). Only when
		// the execution sprint produces headers + platform binaries do the lines below get uncommented.
		//
		// string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");
		//
		// // Headers (populated at the execution sprint from the built libraries):
		// PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "blend2d", "include"));
		// PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "asmjit", "include"));
		//
		// if (Target.Platform == UnrealTargetPlatform.Win64)
		// {
		//     // Static/import libraries (execution-sprint build output):
		//     // PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "blend2d", "Win64", "blend2d.lib"));
		//     // Runtime DLLs + delay-load, staged via Git LFS:
		//     // RuntimeDependencies.Add(Path.Combine(ThirdParty, "blend2d", "Win64", "blend2d.dll"), StagedFileType.NonUFS);
		//     // PublicDelayLoadDLLs.Add("blend2d.dll");
		// }
		// else if (Target.Platform == UnrealTargetPlatform.Linux)
		// {
		//     // PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "blend2d", "Linux", "libblend2d.a"));
		// }
		//
		// // Blend2D bundles/links AsmJit statically (BL_STATIC); no separate AsmJit runtime dep when
		// // Blend2D is built static. Confirmed at the execution sprint.
	}
}
