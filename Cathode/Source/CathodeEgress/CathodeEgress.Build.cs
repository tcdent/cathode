using UnrealBuildTool;

public class CathodeEgress : ModuleRules
{
	public CathodeEgress(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// TEXTURE EGRESS — the SOLE RHI touchpoint: it consumes CathodeSurfaces' NEUTRAL readiness
		// relay (the vendor-neutral FSurfaceBufferRef view), owns a UTexture2D per surface, copies the dirty
		// bytes at the readiness callback, and uploads them to the GPU on the render thread. It has NO dependents
		// (top of the dep graph, like CathodeComposition) — the RHI stays confined here.
		//
		// It does NOT dep CathodeRasterizer (it reads the neutral Pixels/Stride/Format VIEW, never the void* /
		// FBitmapData) and NOT CathodeComposition (the engines stay confined there; the egress is source-agnostic
		// — any producer's neutral readiness drives it). Only CathodeSurfaces + the UE RHI/render stack.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",     // UTexture2D is a UObject (CreateTransient, TStrongObjectPtr)
			"Engine",          // UTexture2D, FTextureResource
			"RHI",             // FRHITexture, RHIUpdateTexture2D, FUpdateTextureRegion2D, ReadSurfaceData
			"RenderCore",      // ENQUEUE_RENDER_COMMAND, FlushRenderingCommands
			"CathodeSurfaces", // the neutral readiness seam: FSurfaceReadiness, FSurfaceBufferRef, ISurfaceRegistry
		});
	}
}
