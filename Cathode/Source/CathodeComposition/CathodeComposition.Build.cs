using UnrealBuildTool;

public class CathodeComposition : ModuleRules
{
	public CathodeComposition(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The integration COMPOSITION ROOT (sprint 023, browse-design ruling B). It is the SOLE site that deps
		// the engines together: it backs CathodeSurfaces' neutral seams with the real Layout/Rasterizer and owns
		// ALL the cross-engine glue (the FLayoutPaintOp->FPaintOp mapper, the ITextMeasurer adapter, the glyph
		// map). It has NO dependents (top of the dep graph), so depping the engines CONFINES them here — they do
		// NOT leak transitively up (e.g. to CathodeVerify) the way extending CathodeDom would have; that
		// dep-confinement is what decided B. The engines + CathodeSurfaces + CathodeDom stay NEUTRAL (no
		// cross-engine production dep in any of them).
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CathodeSurfaces",   // 006: ISurfaceDocument/ISurfaceRenderTarget seams, FSurface, the per-frame tick
			"CathodeLayout",     // 005: ILayoutEngine (backs ISurfaceDocument), FLayoutPaintOp, ITextMeasurer
			"CathodeRasterizer", // 003: ISurfaceProducer/CreateSurfaceRasterizer (backs ISurfaceRenderTarget), FPaintOp
			"CathodeText",       // 004: the shaper + font registry (the ITextMeasurer adapter; the glyph map, slice 2)
			"CathodeResolver",   // 009: the resource resolver (the A2 production wiring, slice 3)
			"CathodeDom",        // the real DOM document source (ILayoutDocumentSource, wired beyond slice 1)
		});

		// 028 S5 — the PRODUCTION owner-wire (FCompositionSurfaceOwner). PRIVATE: only the owner's .cpp uses these; the
		// owner's public header forward-decls ITextureEgress + UTexture2D, so consumers (the device) DON'T transitively
		// link them — the device deps ONLY CathodeComposition (the runtime/egress/binding stay confined to this root).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",     // UTexture2D (the owner's GetTexture return; complete in the .cpp)
			"Engine",          // Engine/Texture2D.h
			"CathodeRuntime",  // FCathodeRuntimeModule: InstallSurfaceRelayoutSeam / DrainOutbound / PostWorkerJob
			"CathodeEgress",   // CreateTextureEgress, ITextureEgress (the egress holder + the game-thread pump)
			"CathodeBinding",  // CreateNativeClassRegistry (the worker-side JS class registry for the per-surface doc)
		});

		// TEST-ONLY (non-Shipping): Projects for IPluginManager — the slice-1 tick proof resolves the committed
		// OFL font fixture via the plugin base dir. A Shipping CathodeComposition carries neither.
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.Add("Projects");
		}
	}
}
