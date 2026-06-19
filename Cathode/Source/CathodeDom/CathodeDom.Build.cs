using System.IO;
using UnrealBuildTool;

public class CathodeDom : ModuleRules
{
	public CathodeDom(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The DOM is the COMPOSER: it wraps NO new vendor and builds no binding/layout/surface of its own — it
		// drives the three built siblings. It is quickjs-type-coupled BY DESIGN (via CathodeBinding's public
		// API — 007's "expose the spec" inheritance) and calls JS_* DIRECTLY in 5 TUs.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CathodeRuntime", // 002: ICathodeJsRuntime (FDomDocument's public ctor + member) + CreateJsRuntime (tests) — DIRECT use, declared (was only transitive via CathodeBinding, which gave headers but not the import-lib link -> CreateJsRuntime LNK1120)
			"CathodeBinding", // 007: node-class binding, FJsValue (+Mark/FreeRT), FCallableHandle, marshaling
			"CathodeLayout",  // 005: ILayoutDocumentSource (implemented) + ILayoutEngine (driven)
			"CathodeSurfaces" // 006: ISurfaceDocument — the surface<->DOM binding resolved at integration
		});

		// TEST-ONLY conditional deps (non-Shipping; a Shipping CathodeDom carries neither):
		//  - Projects (sprint 012): IPluginManager, to resolve the plugin dir for runtime-loaded fixtures (the
		//    vendored Preact bundle + the panel; sprint 020's linked-CSS fixture).
		//  - CathodeResolver (sprint 020, A1): the linked-CSS round-trip test wires the real resolver
		//    (CreateUEFileBundleSource/CreateResourceResolver) onto the real FDomDocument's litehtml engine. A DIRECT
		//    test-only dep for the IMPORT LIB (transitive via CathodeLayout gives headers, not the link). No
		//    production change — SetResourceResolver is reused (the production wiring stays integration-deferred).
		// All WITH_DEV_AUTOMATION_TESTS-gated.
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.Add("Projects");
			PrivateDependencyModuleNames.Add("CathodeResolver");
		}

		// quickjs-ng (the SHARED DLL): the DOM calls JS_* directly, so it must LINK the quickjs import lib
		// itself. The public INCLUDE propagates from CathodeBinding, but the import lib does NOT reliably
		// propagate across the 2-hop chain (the first cross-module link proved it — JS_* were unresolved in
		// CathodeDom.dll), and the shared-runtime model is that ALL quickjs-calling modules link the ONE shared
		// import lib -> the single quickjs.dll. The DLL is staged by CathodeRuntime. See the vendor-lib-build
		// addendum. (No new vendor — quickjs is the shared runtime the DOM already depends on.)
		string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");

		PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "quickjs-ng", "include"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "quickjs-ng", "Win64", "quickjs.lib"));
		}
	}
}
