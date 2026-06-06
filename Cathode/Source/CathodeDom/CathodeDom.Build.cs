using UnrealBuildTool;

public class CathodeDom : ModuleRules
{
	public CathodeDom(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The DOM is the COMPOSER: it wraps NO new vendor and builds no binding/layout/surface of its own — it
		// drives the three built siblings. It is quickjs-type-coupled BY DESIGN (via CathodeBinding's public
		// API — 007's "expose the spec" inheritance). No ThirdParty block: the only vendor (quickjs-ng) arrives
		// transitively through CathodeBinding; the DOM adds none.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CathodeBinding", // 007: node-class binding, FJsValue (+Mark/FreeRT), FCallableHandle, marshaling
			"CathodeLayout",  // 005: ILayoutDocumentSource (implemented) + ILayoutEngine (driven)
			"CathodeSurfaces" // 006: ISurfaceDocument — the surface<->DOM binding resolved at integration
		});
	}
}
