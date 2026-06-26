#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "Templates/SharedPointer.h"
#include "CathodeSurfaces.h" // Cathode::FSurfaceId

class UTexture2D;

namespace Cathode
{
class ITextureEgress;
class ICathodeGameBridge; // 029 #27 — the injected game.* backing (the #28-designed hook; defaults to the stub)
struct FOwnerWorkerState;

// 029 #27 — one surface to fan: its author bundle (a root-relative path read via the resolver, OR an inline JS string for a
// test-only proof) + its pixel dimensions. The owner builds one FDomDocument + one ILayoutEngine + one egress texture per spec.
struct FCompositionSurfaceSpec
{
	FString BundlePath;     // root-relative author bundle, read via the production resolver over /Game/Cathode
	FString InlineBundleJs; // TEST-only: if non-empty, eval THIS string as the bundle instead of reading BundlePath
	int32 Width = 0;
	int32 Height = 0;
};

/**
 * 028 S5 / 029 #27 — the PRODUCTION surface-load owner. Drives the worker composition path. On Install it puts the Phase-5
 * relayout seam onto the auto-started worker; the worker (per tick) BUILDS each surface's FDomDocument over its own
 * ILayoutEngine, repoints globalThis.document to it (M1), READS+EVALUATES that surface's author bundle (the app self-mounts),
 * and COMPOSES. The owner holds the egress, PUMPS the game-thread upload half once per host frame, and DISPOSES the
 * per-surface docs ON THE WORKER at teardown.
 *
 * It lives in CathodeComposition (the composition root) so the CathodeRuntime/Egress/Binding deps are CONFINED here; the
 * device (actor + subsystem) holds ONE owner and drives Pump() from its game-thread Tick.
 *
 * 029 #27 — MULTI-SURFACE: the single relayout seam FANS to N surfaces (the founding's "one runtime, one global object, many
 * surfaces": ONE shared JS runtime + ONE global game.* singleton + PER-SURFACE documents/engines/textures). game.* is a
 * shared singleton (every surface reads the one world — intentional, NOT isolated); `document` is per-surface (the seam
 * repoints globalThis.document before each surface's eval). The egress holds a per-surface lock-free triple-buffer map (#24/#27).
 */
class CATHODECOMPOSITION_API FCompositionSurfaceOwner
{
public:
	FCompositionSurfaceOwner();
	~FCompositionSurfaceOwner();

	// Install the production relayout seam (game-thread; idempotent). The worker fans the build over Specs: per surface it
	// builds the doc/engine, repoints globalThis.document (M1), evals the bundle, and registers the surface.
	// The device / firewalled caller: the seam creates the default production STUB game.* bridge (no ICathodeGameBridge type
	// needed at THIS call site — keeps the device's source-dispositive firewall: it never #includes CathodeDom).
	void Install(const TArray<FCompositionSurfaceSpec>& Specs, const FString& FontFilePath);
	// The sim (later) / a test (now): INJECT the game.* backing, handed to InstallGameBridgeAsGlobal (the #28-designed hook).
	// The owner takes ownership; the bridge outlives the worker's use. Callers of THIS overload see the full ICathodeGameBridge.
	void Install(const TArray<FCompositionSurfaceSpec>& Specs, const FString& FontFilePath,
		TUniquePtr<ICathodeGameBridge> Bridge);

	// Game-thread per host frame: drain the worker's frame-ready + the egress game-thread upload half (per-surface). Returns
	// the number of frame-ready messages drained this call (the S6 two-frame test waits on the v2 re-compose's fresh count).
	int32 Pump();

	// The worker-assigned FSurfaceId for the spec at Index (the order Specs were registered). Default-constructed until the
	// first build completes (read after a Pump that drained a frame-ready). For the single-surface device: GetSurfaceId(0).
	FSurfaceId GetSurfaceId(int32 Index) const;

	// The surface's GPU texture (the device material samples it). Null until that surface's first composed frame arrives.
	UTexture2D* GetTexture(FSurfaceId Surface) const;

	// Teardown: uninstall the seam + POST the per-surface docs' Dispose onto the worker (quickjs-affine). Idempotent.
	void Dispose();

	// Is the production seam installed (the lead's lifetime/wire check).
	bool IsInstalled() const { return bInstalled; }

	// 030 S1-P2 — the input-delivery primitive: POST a keydown (the canonical e.key/e.code token) to a surface. The next
	// seam tick repoints globalThis.document to that surface + delivers it at the focused element / body, bubbling to
	// document.addEventListener('keydown'). Worker-affine (the doc is worker-owned) — never an inline dispatch. The
	// composition-root adapter (030 S2) maps an ESurfaceInputKind → the canonical token and calls this.
	void DispatchKeyToSurface(FSurfaceId Surface, const FString& Key, const FString& Code);

	// 031 S2 — THE CATHODE-OWNED INPUT-INTENT LAYER (the inversion: the host delivers a SEMANTIC intent; Cathode owns the
	// intent->key map [CathodeIntentToKey] + routes to the ACTIVE surface). The intent contract atop DispatchKeyToSurface
	// (which STAYS public — the raw-key layer, B-i both). A host designates which surface is the active input target, then
	// delivers intents; Cathode maps the intent -> the canonical keydown -> the worker-posted DispatchKeyToSurface. This is
	// "Input Routing routes to the active surface" (components-input-routing.md), now Cathode-owned. NO host port needed.
	void DesignateActiveSurface(FSurfaceId Surface); // the host designates the active input target (the owner is N-surface; one active)
	void ReleaseActiveSurface();                      // the host clears the active surface (delivery becomes a no-op)
	void DeliverIntent(ECathodeInputIntent Intent);   // map the intent -> the canonical key -> the active surface's keydown

#if WITH_DEV_AUTOMATION_TESTS
	// TEST-ONLY (027 realloc): release the old egress texture + POST a worker-side surface re-registration for surface Index.
	void ForceReacquireForTest(int32 SurfaceIndex = 0);
	// TEST-ONLY (028 S6 / 029 #27): queue a worker-side JS mutation for surface SurfaceIndex to eval on the NEXT seam tick
	// (the seam repoints globalThis.document to that surface's doc before evaling) -> MarkNodeDirty on that surface's engine
	// -> the same DriveAll re-composes (v2). Thread-safe (set game-thread, read worker-side).
	void SetPendingMutationForTest(const FString& JsMutation, int32 SurfaceIndex = 0);
	// TEST-ONLY (028 S6): the per-surface doc BUILD count — proves v2 is NOT a rebuild (the seam build runs ONCE total).
	int32 GetBuildCountForTest() const;
#endif

private:
	TUniquePtr<ITextureEgress> Egress;     // game-thread: Pump + GetTexture
	TSharedPtr<FOwnerWorkerState> State;    // the worker-side per-surface docs/registry/bundles (built on the worker in the seam)
	bool bInstalled = false;
	FSurfaceId ActiveSurface;               // 031 S2 — the designated input target; DeliverIntent routes here (invalid = none)
};

} // namespace Cathode
