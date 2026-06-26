#include "CathodeCompositionOwner.h"

#include "CathodeComposition.h"        // GetDefaultResourceRoot
#include "CathodeCompositionShared.h"  // 028 S6 — MakeCompositionSurfaceRegistrationShared (the shared-engine variant)
#include "CathodeShaperTextMeasurer.h" // FShaperTextMeasurer (the measurer the shared engine + rasterizer share)
#include "CathodeEgress.h"             // ITextureEgress, CreateTextureEgress, FSurfaceReadinessSeam
#include "CathodeRuntimeModule.h"      // FCathodeRuntimeModule (Install/Drain/PostWorkerJob)
#include "CathodeRuntimeSeam.h"        // FSurfaceRelayoutSeam, FHostOutboundMessage
#include "CathodeJsRuntime.h"          // ICathodeJsRuntime, Evaluate, EEvalKind
#include "CathodeBinding.h"            // INativeClassRegistry, CreateNativeClassRegistry
#include "CathodeDom.h"                // FDomDocument, CreateDomDocument
#include "CathodeGameBridge.h"         // 029 #28 — ICathodeGameBridge, CreateStubGameBridge, InstallGameBridgeAsGlobal
#include "CathodeLayout.h"             // ILayoutEngine, CreateLiteHtmlLayoutEngine
#include "CathodeSelector.h"           // ISelectorService, CreateSelectorService (012 matcher — querySelector for the mutation)
#include "CathodeResolver.h"           // IBundleSource, IResourceResolver, CreateUEFileBundleSource, CreateResourceResolver
#include "CathodeSurfaces.h"           // ISurfaceRegistry, CreateSurfaceRegistry, FSurfaceRegistration, FSurfaceReadiness

#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "HAL/CriticalSection.h"
#include "Templates/Atomic.h"
#include "Engine/Texture2D.h"

namespace Cathode
{
namespace
{
	FCathodeRuntimeModule* GetRuntimeModule()
	{
		return FModuleManager::GetModulePtr<FCathodeRuntimeModule>(TEXT("CathodeRuntime"));
	}
}

// 029 #27 — ONE surface's worker-side state. PER-SURFACE: its own ILayoutEngine (stateful per-document — holds the bound
// source + dirty controller + litehtml render tree), its own FDomDocument, its worker-assigned FSurfaceId, its bundle, and
// its pending mutation. Member ORDER is a lifetime contract: Engine precedes Document (the doc renders off it). Held by
// TUniquePtr in the owner array (FCriticalSection is non-movable).
// 030 S1-P2 — a worker-posted keydown (the canonical e.key/e.code token), queued by DispatchKeyToSurface, dispatched in the
// seam at the surface's focused element / body, bubbling to its document.
struct FPendingKey
{
	FString Key;
	FString Code;
};

struct FPerSurfaceState
{
	FString BundlePath;
	FString InlineBundleJs;
	int32 Width = 0;
	int32 Height = 0;
	TUniquePtr<ILayoutEngine> Engine;   // per-surface (stateful per-document)
	TUniquePtr<FDomDocument> Document;   // per-surface (its own root + DOM tree)
	FSurfaceId Surface;                  // worker-assigned at Register
	FCriticalSection MutationLock;       // guards PendingMutation + PendingKeys (set game-thread, drained worker-side)
	FString PendingMutation;             // a worker-side JS mutation (018 API) to eval next tick (test-queued)
	TArray<FPendingKey> PendingKeys;     // 030 S1-P2 — worker-posted keydowns to dispatch next tick (production)
};

// The worker-side composition state — built ON the worker (first seam call). 029 #27: the founding's "one runtime, one global
// object, many surfaces". SHARED (one): the runtime [passed in], the binding registry, the game.* singleton (every surface
// reads the one world), the surface registry (holds N), the stateless shaper adapter, and the bundle source/resolver.
// PER-SURFACE (N): the array below. Member ORDER: Adapter precedes the engines (each holds it by ref); Resolver precedes them
// (captured at SetDocument).
struct FOwnerWorkerState
{
	FString FontPath;
	TSharedPtr<FShaperTextMeasurer> Adapter;  // the same-bytes anchor (engine + rasterizer) — stateless, SHARED across surfaces
	TUniquePtr<IBundleSource> BundleSource;
	TUniquePtr<IResourceResolver> Resolver;
	TUniquePtr<INativeClassRegistry> BindingRegistry;
	TUniquePtr<ISelectorService> Selector;    // 012 matcher — querySelector for the mutation; the docs hold it by ptr
	TUniquePtr<ICathodeGameBridge> GameBridge; // 029 #28 — the game.* singleton backing (injected or the stub); installed ONCE
	TUniquePtr<ISurfaceRegistry> SurfaceRegistry;
	TArray<TUniquePtr<FPerSurfaceState>> Surfaces; // 029 #27 — the per-surface docs/engines/ids
	TAtomic<int32> BuildCount{ 0 };           // the seam build runs ONCE total (==1; v2 is a dirty-mark re-compose)
	bool bInitialized = false;
};

FCompositionSurfaceOwner::FCompositionSurfaceOwner() = default;
FCompositionSurfaceOwner::~FCompositionSurfaceOwner() { Dispose(); }

void FCompositionSurfaceOwner::Install(const TArray<FCompositionSurfaceSpec>& Specs, const FString& FontFilePath)
{
	// Default: the production stub game.* bridge (constructed in the seam). The nullptr TUniquePtr<ICathodeGameBridge> is
	// built HERE (this TU has the full type via CathodeGameBridge.h), so the firewalled device caller never sees it.
	Install(Specs, FontFilePath, nullptr);
}

void FCompositionSurfaceOwner::Install(const TArray<FCompositionSurfaceSpec>& Specs, const FString& FontFilePath,
	TUniquePtr<ICathodeGameBridge> Bridge)
{
	if (bInstalled) { return; }
	if (Specs.Num() == 0) { return; }
	FCathodeRuntimeModule* const Module = GetRuntimeModule();
	if (Module == nullptr) { return; }

	Egress = CreateTextureEgress();
	if (!Egress.IsValid()) { return; }

	State = MakeShared<FOwnerWorkerState>();
	State->FontPath = FontFilePath;
	State->GameBridge = MoveTemp(Bridge); // the injected backing (or null -> the seam creates the stub)
	for (const FCompositionSurfaceSpec& Spec : Specs)
	{
		TUniquePtr<FPerSurfaceState> PS = MakeUnique<FPerSurfaceState>();
		PS->BundlePath = Spec.BundlePath;
		PS->InlineBundleJs = Spec.InlineBundleJs;
		PS->Width = Spec.Width;
		PS->Height = Spec.Height;
		State->Surfaces.Add(MoveTemp(PS));
	}

	const FSurfaceReadinessSeam EgressSeam = Egress->GetSeam();
	TSharedPtr<FOwnerWorkerState> StateRef = State;

	// THE PRODUCTION SEAM (runs on the WORKER each tick): build (once) the SHARED runtime/registry/game.* + each surface's
	// FDomDocument over its OWN engine, repoint globalThis.document to it (M1) + eval its bundle; then each tick, eval any
	// queued per-surface mutation (-> MarkNodeDirty on THAT surface's engine) + DriveAll re-composes the dirty surfaces.
	FSurfaceRelayoutSeam Seam = [StateRef, EgressSeam](ICathodeJsRuntime& Runtime, TArray<FHostOutboundMessage>& OutFrameReady)
	{
		FOwnerWorkerState& S = *StateRef;
		if (!S.bInitialized)
		{
			// THE SHARED build (one runtime, one global object).
			S.BindingRegistry = CreateNativeClassRegistry(Runtime);
			S.Adapter = MakeShared<FShaperTextMeasurer>(S.FontPath);
			S.BundleSource = CreateUEFileBundleSource(GetDefaultResourceRoot());
			S.Resolver = S.BundleSource.IsValid() ? CreateResourceResolver(*S.BundleSource) : nullptr;
			S.Selector = CreateSelectorService();

			// 029 #28/#27 — install game.* ONCE as a DOCUMENTED-KNOWN global (the SHARED singleton every surface reads). Use
			// the INJECTED backing (the sim's real bridge later, or a test bridge now) or default to the production stub.
			if (!S.GameBridge.IsValid()) { S.GameBridge = CreateStubGameBridge(); }
			if (JSContext* GameCtx = static_cast<JSContext*>(Runtime.GetNativeContextHandle()))
			{
				InstallGameBridgeAsGlobal(GameCtx, *S.GameBridge);
			}

			S.SurfaceRegistry = CreateSurfaceRegistry();

			// THE PER-SURFACE fan: each surface = its own engine + document + egress texture; the bundle mounts PER-SURFACE.
			for (const TUniquePtr<FPerSurfaceState>& PSPtr : S.Surfaces)
			{
				FPerSurfaceState& PS = *PSPtr;
				PS.Engine = CreateLiteHtmlLayoutEngine(*S.Adapter);
				if (PS.Engine.IsValid() && S.Resolver.IsValid())
				{
					PS.Engine->SetResourceResolver(S.Resolver.Get()); // BEFORE CreateDomDocument (its SetDocument captures it)
				}
				PS.Document = PS.Engine.IsValid()
					? CreateDomDocument(Runtime, *S.BindingRegistry, *PS.Engine, S.Selector.Get())
					: nullptr;
				if (!PS.Document.IsValid()) { continue; }

				// M1 — repoint globalThis.document to THIS surface's doc, then eval its bundle: the bundle reads `document` as
				// a free global (re-resolved per op) -> mounts to THIS doc's body. game.* stays the shared singleton (untouched).
				PS.Document->InstallAsGlobal();
				FString BundleStr;
				if (!PS.InlineBundleJs.IsEmpty())
				{
					BundleStr = PS.InlineBundleJs; // TEST: the inline bundle (no file read)
				}
				else if (S.BundleSource.IsValid())
				{
					TArray<uint8> BundleBytes;
					if (S.BundleSource->Read(PS.BundlePath, BundleBytes))
					{
						FFileHelper::BufferToString(BundleStr, BundleBytes.GetData(), BundleBytes.Num());
					}
				}
				if (!BundleStr.IsEmpty())
				{
					Runtime.Evaluate(BundleStr, TEXT("<author-bundle>"), EEvalKind::Global); // v1 content for THIS surface
				}

				// Register THIS surface off its OWN engine (the shared-engine-WITHIN-a-surface variant: the doc's
				// mutation-marked engine renders the composition, so a DOM mutation re-composes).
				if (S.SurfaceRegistry.IsValid())
				{
					FSurfaceRegistration Reg = MakeCompositionSurfaceRegistrationShared(
						*PS.Engine, S.Adapter.ToSharedRef(), PS.Width, PS.Height, ESurfacePixelFormat::PremultipliedBGRA8);
					PS.Surface = S.SurfaceRegistry->Register(MoveTemp(Reg));
				}
			}
			++S.BuildCount; // the build runs ONCE (v2 is a dirty-mark re-compose, not a rebuild)
			S.bInitialized = true;
		}

		// 028 S6 / 029 #27 — ride the live wire: eval each surface's queued mutation. Repoint globalThis.document to THAT
		// surface's doc first so the mutation's querySelector resolves against it -> setAttribute -> MarkDirty -> THAT
		// surface's engine MarkNodeDirty -> the DriveAll below re-composes it (v2). game.* is shared, so a mutation reading
		// game.tickCount sees the SAME host-set value on every surface.
		for (const TUniquePtr<FPerSurfaceState>& PSPtr : S.Surfaces)
		{
			FPerSurfaceState& PS = *PSPtr;
			FString Mutation;
			TArray<FPendingKey> Keys;
			{
				FScopeLock Lock(&PS.MutationLock);
				Mutation = PS.PendingMutation;
				PS.PendingMutation.Empty();
				Keys = MoveTemp(PS.PendingKeys);
				PS.PendingKeys.Reset();
			}
			if ((!Mutation.IsEmpty() || Keys.Num() > 0) && PS.Document.IsValid())
			{
				PS.Document->InstallAsGlobal(); // M1 — point `document` at THIS surface before its mutation / key delivery
				if (!Mutation.IsEmpty())
				{
					Runtime.Evaluate(Mutation, TEXT("<s6-mutation>"), EEvalKind::Global);
				}
				// 030 S1-P2 — deliver each worker-posted keydown at the focused element (re-resolved per key, since a handler
				// may move focus), falling back to <body> then the root, bubbling up to document.addEventListener('keydown').
				for (const FPendingKey& K : Keys)
				{
					FDomNodeId TargetId = PS.Document->GetFocusedNode();
					if (TargetId == INDEX_NONE) { TargetId = PS.Document->FindBody(); }
					if (TargetId == INDEX_NONE) { TargetId = PS.Document->GetRootNodeId(); }
					if (FDomNode* Target = PS.Document->FindNode(TargetId))
					{
						FDomKeyboardEvent Kbd;
						Kbd.Event.Type = TEXT("keydown");
						Kbd.Event.bBubbles = true;
						Kbd.Key = K.Key;
						Kbd.Code = K.Code;
						Target->DispatchKeyboardEvent(Kbd);
					}
				}
			}
		}

		if (S.SurfaceRegistry.IsValid())
		{
			S.SurfaceRegistry->DriveAll([&EgressSeam, &OutFrameReady](const FSurfaceReadiness& R)
			{
				EgressSeam(R);
				OutFrameReady.Add(FHostOutboundMessage{ R.Surface.Value });
			});
		}
	};
	Module->InstallSurfaceRelayoutSeam(MoveTemp(Seam));
	bInstalled = true;
}

int32 FCompositionSurfaceOwner::Pump()
{
	int32 Drained = 0;
	if (FCathodeRuntimeModule* const Module = GetRuntimeModule())
	{
		TArray<FHostOutboundMessage> Frames;
		Module->DrainOutbound(Frames);
		Drained = Frames.Num();
	}
	if (Egress.IsValid())
	{
		Egress->PumpGameThreadUploads(); // 029 #27 — drains ALL per-surface triple-buffers
	}
	return Drained;
}

FSurfaceId FCompositionSurfaceOwner::GetSurfaceId(int32 Index) const
{
	// Read the worker-assigned id. After a Pump that drained a frame-ready, the build is complete and PS.Surface is stable
	// (the outbound-message dequeue is the happens-before edge over the worker's build writes).
	if (State.IsValid() && State->Surfaces.IsValidIndex(Index) && State->Surfaces[Index].IsValid())
	{
		return State->Surfaces[Index]->Surface;
	}
	return FSurfaceId{};
}

UTexture2D* FCompositionSurfaceOwner::GetTexture(FSurfaceId Surface) const
{
	return Egress.IsValid() ? Egress->GetTexture(Surface) : nullptr;
}

void FCompositionSurfaceOwner::Dispose()
{
	if (!bInstalled) { return; }
	if (FCathodeRuntimeModule* const Module = GetRuntimeModule())
	{
		Module->InstallSurfaceRelayoutSeam(FSurfaceRelayoutSeam());
		TSharedPtr<FOwnerWorkerState> StateRef = State;
		Module->PostWorkerJob([StateRef]()
		{
			if (StateRef.IsValid())
			{
				for (const TUniquePtr<FPerSurfaceState>& PSPtr : StateRef->Surfaces)
				{
					if (PSPtr.IsValid() && PSPtr->Document.IsValid()) { PSPtr->Document->Dispose(); }
				}
			}
		});
	}
	bInstalled = false;
}

void FCompositionSurfaceOwner::DispatchKeyToSurface(FSurfaceId Surface, const FString& Key, const FString& Code)
{
	// 030 S1-P2 — the production input-delivery primitive. Queue a keydown on the matching surface (PS.Surface is
	// worker-assigned at Register, stable after the build), then WAKE the worker: the next seam tick repoints
	// globalThis.document to that surface, delivers the keydown at its focused element / body, and it bubbles to
	// document.addEventListener('keydown'). NOT an inline dispatch — it rides the worker-affine seam (the doc is worker-owned).
	if (!State.IsValid()) { return; }
	bool bQueued = false;
	for (const TUniquePtr<FPerSurfaceState>& PSPtr : State->Surfaces)
	{
		if (PSPtr.IsValid() && PSPtr->Surface == Surface)
		{
			FScopeLock Lock(&PSPtr->MutationLock);
			PSPtr->PendingKeys.Add(FPendingKey{ Key, Code });
			bQueued = true;
			break;
		}
	}
	if (bQueued)
	{
		if (FCathodeRuntimeModule* const Module = GetRuntimeModule()) { Module->PostWorkerJob([](){}); }
	}
}

void FCompositionSurfaceOwner::DesignateActiveSurface(FSurfaceId Surface)
{
	// 031 S2 — the host designates which surface receives delivered intents (the active input target). The owner is
	// N-surface; one is active at a time (components-input-routing.md: route to the ACTIVE surface).
	ActiveSurface = Surface;
}

void FCompositionSurfaceOwner::ReleaseActiveSurface()
{
	ActiveSurface = FSurfaceId(); // invalid -> DeliverIntent becomes a no-op (no active input target)
}

void FCompositionSurfaceOwner::DeliverIntent(ECathodeInputIntent Intent)
{
	// 031 S2 — THE CATHODE-OWNED INTENT LAYER (the inversion): map the SEMANTIC intent -> the canonical DOM key (Cathode owns
	// the map) -> the ACTIVE surface's worker-posted keydown. The host delivers an intent; Cathode maps + routes it. No-op if
	// no active surface (or an out-of-range intent). The raw DispatchKeyToSurface stays public beneath this (B-i both).
	if (!ActiveSurface.IsValid()) { return; }
	const FString Key = CathodeIntentToKey(Intent);
	if (Key.IsEmpty()) { return; }
	DispatchKeyToSurface(ActiveSurface, Key, Key); // code == key for the six canonical tokens
}

#if WITH_DEV_AUTOMATION_TESTS
void FCompositionSurfaceOwner::ForceReacquireForTest(int32 SurfaceIndex)
{
	if (!bInstalled) { return; }
	FCathodeRuntimeModule* const Module = GetRuntimeModule();
	if (Module == nullptr) { return; }
	if (!State.IsValid() || !State->Surfaces.IsValidIndex(SurfaceIndex)) { return; }
	if (Egress.IsValid()) { Egress->Release(GetSurfaceId(SurfaceIndex)); }
	TSharedPtr<FOwnerWorkerState> StateRef = State;
	Module->PostWorkerJob([StateRef, SurfaceIndex]()
	{
		FOwnerWorkerState& S = *StateRef;
		if (!S.Surfaces.IsValidIndex(SurfaceIndex) || !S.Surfaces[SurfaceIndex].IsValid()) { return; }
		FPerSurfaceState& PS = *S.Surfaces[SurfaceIndex];
		if (S.SurfaceRegistry.IsValid() && PS.Document.IsValid())
		{
			S.SurfaceRegistry->Unregister(PS.Surface);
			// 027 realloc uses a FRESH-engine surface (the non-shared variant): a new engine re-feeds the CURRENT doc -> a
			// NEW texture. (No mutation here, so the fresh engine is correct — a shared-engine re-register would find the
			// engine clean post-v1 -> no ops -> a null texture.)
			FSurfaceRegistration Reg = MakeCompositionSurfaceRegistration(
				*PS.Document, PS.Width, PS.Height, ESurfacePixelFormat::PremultipliedBGRA8, S.FontPath);
			PS.Surface = S.SurfaceRegistry->Register(MoveTemp(Reg));
		}
	});
}

void FCompositionSurfaceOwner::SetPendingMutationForTest(const FString& JsMutation, int32 SurfaceIndex)
{
	if (!State.IsValid() || !State->Surfaces.IsValidIndex(SurfaceIndex) || !State->Surfaces[SurfaceIndex].IsValid()) { return; }
	{
		FPerSurfaceState& PS = *State->Surfaces[SurfaceIndex];
		FScopeLock Lock(&PS.MutationLock);
		PS.PendingMutation = JsMutation;
	}
	// WAKE the worker: after v1 it idles (WakeEvent->Wait — no rAF/timers pending). An empty inbound job triggers the wake
	// -> the next tick runs the seam -> evals the mutation -> MarkNodeDirty -> re-compose (v2).
	if (FCathodeRuntimeModule* const Module = GetRuntimeModule()) { Module->PostWorkerJob([](){}); }
}

int32 FCompositionSurfaceOwner::GetBuildCountForTest() const
{
	return State.IsValid() ? State->BuildCount.Load() : 0;
}
#endif

} // namespace Cathode
