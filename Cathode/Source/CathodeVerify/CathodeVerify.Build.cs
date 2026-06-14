using System.IO;
using UnrealBuildTool;

public class CathodeVerify : ModuleRules
{
	public CathodeVerify(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The runtime-verification faculty's in-engine home (sprint 011 [b]) — a Layer-5 runtime capability
		// (one-module-per-component). It DRIVES the faculty JS in a Cathode runtime and reports the structured
		// self-report; depends on CathodeRuntime for the runtime + Evaluate (and, under automation, the
		// FScopedTestRuntime seam that yields the real production runtime on the test thread). The introspection
		// itself is JS (the Cathode-authored surface walk); this module hosts + drives it.
		// See docs/browse/components-runtime-verification.md.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CathodeRuntime",
		});

		// Private: Json parses the faculty's self-report for the gate assertions; Projects (IPluginManager)
		// resolves the faculty JS files relative to the plugin dir at runtime.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Json",
			"Projects",
		});

		// quickjs-ng (the SHARED DLL): the driver installs the native emit-sink (JS_NewCFunction) and injects
		// the harness inputs onto the live JSContext recovered from the runtime, so it calls JS_* directly and
		// must LINK the ONE shared quickjs import lib (same model as CathodeBinding/CathodeDom — the DLL is
		// staged by CathodeRuntime). No new vendor; quickjs is the runtime CathodeRuntime already owns.
		string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");

		PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "quickjs-ng", "include"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "quickjs-ng", "Win64", "quickjs.lib"));
		}
	}
}
