using System.IO;
using UnrealBuildTool;

public class CathodeRuntime : ModuleRules
{
	public CathodeRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The runtime module's Unreal dependency set: Core/CoreUObject are the base object/runtime
		// layer; Engine + Slate host the runtime's surface/device objects. The Texture Egress — the
		// RHI texture upload (RHIUpdateTexture2D) — is its OWN module (CathodeEgress,
		// the sole RHI touchpoint), so RHI/RenderCore live THERE with it, not here; CathodeRuntime does
		// not touch the RHI/render path.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
		});

		// --- Bundled ThirdParty: quickjs-ng (the JS engine — a SHARED DLL) ---
		//
		// quickjs-ng is the ONE shared vendor lib: a STATEFUL JS runtime linked by THREE modules
		// (CathodeRuntime/Binding/Dom) that call its C API on ONE shared JSRuntime, so it is built as a DLL and
		// all three link the ONE import lib -> a single shared runtime (NOT static-per-module — see
		// docs/browse/vendor-lib-build.md addendum: it must never be linked twice). Here
		// `quickjs.lib` is the IMPORT lib backing `quickjs.dll`. This (base) module STAGES the DLL next to the
		// binaries — standard import-lib linkage, no delay-load; the loader resolves quickjs.dll from
		// Binaries/Win64, where every dependent Cathode module also loads. The other 3 vendor libs stay static.
		string ThirdParty = Path.Combine(ModuleDirectory, "..", "ThirdParty");

		PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "quickjs-ng", "include"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "quickjs-ng", "Win64", "quickjs.lib"));
			// The runtime DLL's embedded import name (soname, carried by quickjs.lib) is `qjs.dll` — quickjs-ng's
			// native `qjs` target output. The vendored file on disk is quickjs.dll (the tooling renamed the FILE
			// but not the soname), so the linked binaries import `qjs.dll` (GetLastError=126 otherwise). Stage the
			// runtime DLL UNDER THE IMPORTED NAME `qjs.dll` so the loader resolves it. (If the tooling later
			// rebuilds quickjs with OUTPUT_NAME=quickjs so the soname is quickjs.dll, change this target back to
			// quickjs.dll — see the vendor-lib-build.md addendum's naming note.)
			RuntimeDependencies.Add("$(BinaryOutputDir)/qjs.dll", Path.Combine(ThirdParty, "quickjs-ng", "Win64", "quickjs.dll"));
		}
	}
}
