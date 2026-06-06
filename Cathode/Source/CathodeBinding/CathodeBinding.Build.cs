using System.IO;
using UnrealBuildTool;

public class CathodeBinding : ModuleRules
{
	public CathodeBinding(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// VENDOR-CONFINED (not couples-to-none): this layer includes quickjs.h and calls quickjs-ng binding
		// primitives, and it needs 002's live JSContext. It depends on CathodeRuntime for ICathodeJsRuntime
		// (the opaque-context accessor). The SWAP BOUNDARY is the MODULE boundary — consumers (DOM, Host
		// Bridge) go through CathodeBinding's API; per the RQ2 decision the public header exposes quickjs-ng's
		// own conventions directly, so quickjs is a PUBLIC include (it propagates to consumers — the accepted
		// cost of "expose the spec").
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CathodeRuntime",
		});

		// --- Bundled ThirdParty: quickjs-ng (the JS engine) ---
		//
		// SCAFFOLD ONLY (sprint 007): mirrors the 002/003/004/005 honest-stub. quickjs-ng is vendored as
		// SOURCE under vendor/lib/cathode/quickjs-ng/ and BUILT FROM SOURCE into Source/ThirdParty/quickjs-ng/
		// at the execution-approval sprint; the include/link lines stay COMMENTED until then (no build this
		// sprint). The include path is PUBLIC by the RQ2 decision (the public header includes quickjs.h).
		// Note: 002 (CathodeRuntime) also includes quickjs.h in its impl — both modules confine the vendor.
		//
		// string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");
		// PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "quickjs-ng", "include"));
		//
		// if (Target.Platform == UnrealTargetPlatform.Win64)
		// {
		//     // PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "quickjs-ng", "Win64", "quickjs.lib"));
		// }
		// else if (Target.Platform == UnrealTargetPlatform.Linux)
		// {
		//     // PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "quickjs-ng", "Linux", "libquickjs.a"));
		// }
	}
}
