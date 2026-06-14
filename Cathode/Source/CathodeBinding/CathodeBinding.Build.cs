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

		// --- Bundled ThirdParty: quickjs-ng (the JS engine — a SHARED DLL) ---
		//
		// CathodeBinding's PUBLIC header includes quickjs.h (RQ2 "expose the spec") and 6 TUs call JS_*
		// directly, so it LINKS the quickjs import lib itself. quickjs is a SHARED DLL (one stateful runtime
		// across Runtime/Binding/Dom), so all three link the ONE import lib -> one shared quickjs.dll. Importing
		// the same DLL from N modules is correct (each gets dllimport stubs to the single runtime), NOT a static
		// double-link. The DLL is staged by CathodeRuntime (the base module). See the vendor-lib-build addendum.
		string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");

		PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "quickjs-ng", "include"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "quickjs-ng", "Win64", "quickjs.lib"));
		}
	}
}
