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

// The worker-side composition state — built ON the worker (first seam call). 028 S6: ONE shared real DOM-layout engine
// (the doc's mutation-marked engine) renders the composition, so a DOM mutation re-composes. Member ORDER is a lifetime
// contract: Adapter precedes Engine (engine holds it by ref); BundleSource precedes Resolver; Resolver precedes Engine
// (the engine captures it at SetDocument); Engine precedes Document + SurfaceRegistry (both render off it).
struct FOwnerWorkerState
{
	FString BundlePath;
	int32 Width = 0;
	int32 Height = 0;
	FString FontPath;
	TSharedPtr<FShaperTextMeasurer> Adapter;  // the same-bytes anchor (engine + rasterizer)
	TUniquePtr<IBundleSource> BundleSource;
	TUniquePtr<IResourceResolver> Resolver;
	TUniquePtr<INativeClassRegistry> BindingRegistry;
	TUniquePtr<ILayoutEngine> Engine;         // the SHARED real DOM-layout engine (replaces the S5 FStubDomLayout)
	TUniquePtr<ISelectorService> Selector;    // 012 matcher — querySelector for the S6 mutation; the doc holds it by ptr (before Document)
	TUniquePtr<FDomDocument> Document;
	TUniquePtr<ISurfaceRegistry> SurfaceRegistry;
	FSurfaceId Surface;
	TAtomic<int32> BuildCount{ 0 };           // S6: the seam build runs ONCE (==1 across v1->v2 -> not a rebuild)
	FCriticalSection MutationLock;
	FString PendingMutation;                  // S6: a worker-side JS mutation (018 API) to eval next tick (test-queued)
	bool bInitialized = false;
};

FCompositionSurfaceOwner::FCompositionSurfaceOwner() = default;
FCompositionSurfaceOwner::~FCompositionSurfaceOwner() { Dispose(); }

void FCompositionSurfaceOwner::Install(const FString& BundlePath, int32 Width, int32 Height, const FString& FontFilePath)
{
	if (bInstalled) { return; }
	FCathodeRuntimeModule* const Module = GetRuntimeModule();
	if (Module == nullptr) { return; }

	Egress = CreateTextureEgress();
	if (!Egress.IsValid()) { return; }

	State = MakeShared<FOwnerWorkerState>();
	State->BundlePath = BundlePath;
	State->Width = Width;
	State->Height = Height;
	State->FontPath = FontFilePath;

	const FSurfaceReadinessSeam EgressSeam = Egress->GetSeam();
	TSharedPtr<FOwnerWorkerState> StateRef = State;

	// THE PRODUCTION SEAM (runs on the WORKER each tick): build (once) the per-surface FDomDocument over ONE shared real
	// DOM-layout engine + compose; then each tick, eval any queued worker-side mutation (-> MarkNodeDirty on the shared
	// engine) + re-compose (the dirty-mark drives v2).
	FSurfaceRelayoutSeam Seam = [StateRef, EgressSeam](ICathodeJsRuntime& Runtime, TArray<FHostOutboundMessage>& OutFrameReady)
	{
		FOwnerWorkerState& S = *StateRef;
		if (!S.bInitialized)
		{
			S.BindingRegistry = CreateNativeClassRegistry(Runtime);
			S.Adapter = MakeShared<FShaperTextMeasurer>(S.FontPath);
			S.BundleSource = CreateUEFileBundleSource(GetDefaultResourceRoot());
			S.Resolver = S.BundleSource.IsValid() ? CreateResourceResolver(*S.BundleSource) : nullptr;
			S.Engine = CreateLiteHtmlLayoutEngine(*S.Adapter);
			if (S.Engine.IsValid() && S.Resolver.IsValid())
			{
				S.Engine->SetResourceResolver(S.Resolver.Get()); // BEFORE CreateDomDocument (its SetDocument captures it)
			}
			// CreateDomDocument calls Engine->SetDocument(doc) — the SAME engine that receives the mutation MarkNodeDirty.
			// The matcher (012) is installed so the worker-side mutation's document.querySelector('div') resolves.
			S.Selector = CreateSelectorService();
			S.Document = S.Engine.IsValid() ? CreateDomDocument(Runtime, *S.BindingRegistry, *S.Engine, S.Selector.Get()) : nullptr;
			if (S.Document.IsValid())
			{
				S.Document->InstallAsGlobal();
				TArray<uint8> BundleBytes;
				if (S.BundleSource.IsValid() && S.BundleSource->Read(S.BundlePath, BundleBytes))
				{
					FString BundleStr;
					FFileHelper::BufferToString(BundleStr, BundleBytes.GetData(), BundleBytes.Num());
					Runtime.Evaluate(BundleStr, TEXT("<author-bundle>"), EEvalKind::Global); // v1 content
				}
				S.SurfaceRegistry = CreateSurfaceRegistry();
				if (S.SurfaceRegistry.IsValid())
				{
					// THE UNIFY: the composition renders off the SHARED engine (S.Engine), not its own.
					FSurfaceRegistration Reg = MakeCompositionSurfaceRegistrationShared(
						*S.Engine, S.Adapter.ToSharedRef(), S.Width, S.Height, ESurfacePixelFormat::PremultipliedBGRA8);
					S.Surface = S.SurfaceRegistry->Register(MoveTemp(Reg));
				}
			}
			++S.BuildCount; // the build runs ONCE (v2 is a dirty-mark re-compose on this SAME doc, not a rebuild)
			S.bInitialized = true;
		}

		// 028 S6 — ride the live wire: eval any queued worker-side mutation (the 018 API) on the worker runtime. The
		// mutation (e.g. setAttribute the bg) -> FDomDocument::MarkDirty -> Engine->MarkNodeDirty (the SHARED engine) ->
		// the DriveAll below re-composes (the engine's tracker is dirty -> re-feed -> v2 ops).
		{
			FString Mutation;
			{
				FScopeLock Lock(&S.MutationLock);
				Mutation = S.PendingMutation;
				S.PendingMutation.Empty();
			}
			if (!Mutation.IsEmpty())
			{
				Runtime.Evaluate(Mutation, TEXT("<s6-mutation>"), EEvalKind::Global);
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
		for (const FHostOutboundMessage& F : Frames)
		{
			Surface = FSurfaceId{ F.SurfaceTag };
		}
	}
	if (Egress.IsValid())
	{
		Egress->PumpGameThreadUploads();
	}
	return Drained;
}

UTexture2D* FCompositionSurfaceOwner::GetTexture() const
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
			if (StateRef.IsValid() && StateRef->Document.IsValid()) { StateRef->Document->Dispose(); }
		});
	}
	bInstalled = false;
}

#if WITH_DEV_AUTOMATION_TESTS
void FCompositionSurfaceOwner::ForceReacquireForTest()
{
	if (!bInstalled) { return; }
	FCathodeRuntimeModule* const Module = GetRuntimeModule();
	if (Module == nullptr) { return; }
	if (Egress.IsValid()) { Egress->Release(Surface); }
	TSharedPtr<FOwnerWorkerState> StateRef = State;
	Module->PostWorkerJob([StateRef]()
	{
		FOwnerWorkerState& S = *StateRef;
		if (S.SurfaceRegistry.IsValid() && S.Document.IsValid())
		{
			S.SurfaceRegistry->Unregister(S.Surface);
			// 027 realloc uses a FRESH-engine surface (the non-shared variant): a new engine re-feeds the CURRENT doc ->
			// the new surface renders -> a NEW texture. (A shared-engine re-register would find the engine's tracker
			// clean post-v1 -> no ops -> a null texture; 027 doesn't mutate, so the fresh engine is correct here.)
			FSurfaceRegistration Reg = MakeCompositionSurfaceRegistration(
				*S.Document, S.Width, S.Height, ESurfacePixelFormat::PremultipliedBGRA8, S.FontPath);
			S.Surface = S.SurfaceRegistry->Register(MoveTemp(Reg));
		}
	});
}

void FCompositionSurfaceOwner::SetPendingMutationForTest(const FString& JsMutation)
{
	if (!State.IsValid()) { return; }
	{
		FScopeLock Lock(&State->MutationLock);
		State->PendingMutation = JsMutation;
	}
	// WAKE the worker: after v1 it idles (WakeEvent->Wait — no rAF/timers pending), so the queued mutation would never
	// be evaled. An empty inbound job triggers the wake event -> the next tick runs the seam -> evals the mutation ->
	// MarkNodeDirty on the shared engine -> re-compose (v2).
	if (FCathodeRuntimeModule* const Module = GetRuntimeModule()) { Module->PostWorkerJob([](){}); }
}

int32 FCompositionSurfaceOwner::GetBuildCountForTest() const
{
	return State.IsValid() ? State->BuildCount.Load() : 0;
}
#endif

} // namespace Cathode
