#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "Templates/SharedPointer.h"
#include "CathodeSurfaces.h" // Cathode::FSurfaceId

class UTexture2D;

namespace Cathode
{
class ITextureEgress;
struct FOwnerWorkerState;

/**
 * 028 S5 — the PRODUCTION surface-load owner. The first production code to drive the worker composition path (S2-S4
 * proved it only test-locally via a gate-test seam install). On Install it puts the Phase-5 relayout seam onto the
 * auto-started worker; the worker (per tick) BUILDS the per-surface FDomDocument, READS+EVALUATES the author bundle (the
 * app self-mounts), and COMPOSES — the production realization of the S4 gate-test seam body. The owner holds the egress,
 * PUMPS the game-thread upload half (DrainOutbound + PumpGameThreadUploads) once per host frame, and DISPOSES the
 * per-surface doc ON THE WORKER at teardown (no leak).
 *
 * It lives in CathodeComposition (the composition root, RQ2) so the CathodeRuntime/Egress/Binding deps are CONFINED
 * here; the device (actor + subsystem) holds ONE owner and drives Pump() from its game-thread Tick, staying narrow
 * (it never directly deps CathodeRuntime/Dom/Resolver — those flow through this owner).
 *
 * SCOPE: ONE surface (028's "one surface, one shared runtime" non-goal). Multi-surface (the single relayout seam
 * fanning to N surfaces) is a NAMED residual. The per-surface DOM uses a deferred no-op layout (the real DOM-layout is
 * S6's live two-frame). The egress holder's lock/deep-copy is correct as-is; the lock-free triple-buffer is a named
 * deferred optimization (an architecture follow-on), NOT owed here.
 */
class CATHODECOMPOSITION_API FCompositionSurfaceOwner
{
public:
	FCompositionSurfaceOwner();
	~FCompositionSurfaceOwner();

	// Install the production relayout seam (game-thread; idempotent). The worker builds+reads+evals+composes the bundle
	// at surface-load. BundlePath is the root-relative author bundle, read via the production resolver over /Game/Cathode.
	void Install(const FString& BundlePath, int32 Width, int32 Height, const FString& FontFilePath);

	// Game-thread per host frame: drain the worker's frame-ready (learn the worker-assigned surface id) + the egress
	// game-thread upload half. This is the production frame-ready cadence (no test poll). Returns the number of
	// frame-ready messages drained this call (production ignores it; the S6 two-frame test waits on the v2 re-compose's
	// fresh frame-ready via this count).
	int32 Pump();

	// The surface's GPU texture (the device material samples it). Null until the first composed frame arrives.
	UTexture2D* GetTexture() const;

	// Teardown: uninstall the seam + POST the per-surface doc Dispose onto the worker (quickjs-affine). Idempotent.
	void Dispose();

	// Is the production seam installed (the lead's lifetime/wire check).
	bool IsInstalled() const { return bInstalled; }

#if WITH_DEV_AUTOMATION_TESTS
	// TEST-ONLY (027 realloc): release the old egress texture + POST a worker-side surface re-registration (a NEW
	// FSurfaceId -> "changed" -> the next composed frame is a NEW egress texture ptr), so a consumer's rebind-on-realloc
	// is exercised against the production wire. NOT a production path. (No per-method API macro — the CLASS is already
	// CATHODECOMPOSITION_API-exported, so a per-method macro is a double-export, C2487.)
	void ForceReacquireForTest();
	// TEST-ONLY (028 S6): queue a worker-side JS mutation (the 018 API) to eval on the NEXT seam tick — it rides the
	// SAME installed seam (no re-install) -> MarkNodeDirty on the shared engine -> the same DriveAll re-composes (v2).
	// Thread-safe (the test sets it game-thread; the seam reads it worker-side).
	void SetPendingMutationForTest(const FString& JsMutation);
	// TEST-ONLY (028 S6): the per-surface doc BUILD count — proves v2 is NOT a rebuild (the seam build runs ONCE; the
	// count stays 1 across v1->v2; the mutation drives the dirty-mark on the SAME doc, not a re-CreateDomDocument/re-eval).
	int32 GetBuildCountForTest() const;
#endif

private:
	TUniquePtr<ITextureEgress> Egress;     // game-thread: Pump + GetTexture
	TSharedPtr<FOwnerWorkerState> State;   // the worker-side doc/registry/bundle (built on the worker in the seam)
	FSurfaceId Surface;                    // worker-assigned, learned via the frame-ready in Pump
	bool bInstalled = false;
};

} // namespace Cathode
