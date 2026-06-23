#include "CathodeCompositionOwner.h"

#include "CathodeComposition.h"     // MakeCompositionSurfaceRegistration, GetDefaultResourceRoot
#include "CathodeEgress.h"          // ITextureEgress, CreateTextureEgress, FSurfaceReadinessSeam
#include "CathodeRuntimeModule.h"   // FCathodeRuntimeModule (Install/Drain/PostWorkerJob)
#include "CathodeRuntimeSeam.h"     // FSurfaceRelayoutSeam, FHostOutboundMessage
#include "CathodeJsRuntime.h"       // ICathodeJsRuntime, Evaluate, EEvalKind
#include "CathodeBinding.h"         // INativeClassRegistry, CreateNativeClassRegistry
#include "CathodeDom.h"             // FDomDocument, CreateDomDocument
#include "CathodeLayout.h"          // ILayoutEngine, ILayoutDocumentSource, EDirtyKind, FLayoutResult
#include "CathodeResolver.h"        // IBundleSource, CreateUEFileBundleSource
#include "CathodeSurfaces.h"        // ISurfaceRegistry, CreateSurfaceRegistry, FSurfaceRegistration, FSurfaceReadiness

#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Engine/Texture2D.h"

namespace Cathode
{
namespace
{
	// 028 S5 — the per-surface DOM's MarkNodeDirty handshake layout: a NO-OP. S5 is BUILD-ONCE (no mutation -> no
	// dirty-mark to exercise); the COMPOSITION builds its own litehtml that reads the doc's GetDocumentHtml. The REAL
	// DOM-layout (mutation -> MarkNodeDirty -> re-serialize -> re-compose) is S6's live two-frame — a NAMED S6 residual.
	class FDeferredDomLayout final : public ILayoutEngine
	{
	public:
		virtual void SetDocument(const ILayoutDocumentSource&) override {}
		virtual void MarkNodeDirty(int32, EDirtyKind) override {}
		virtual FLayoutResult Relayout(int32) override { return FLayoutResult(); }
	};

	FCathodeRuntimeModule* GetRuntimeModule()
	{
		return FModuleManager::GetModulePtr<FCathodeRuntimeModule>(TEXT("CathodeRuntime"));
	}
}

// The worker-side composition state — built ON the worker (first seam call). The Document outlives the surface (the
// MakeCompositionSurfaceRegistration by-ref lifetime); a TSharedPtr keeps it alive across the seam + the Dispose job.
struct FOwnerWorkerState
{
	FString BundlePath;
	int32 Width = 0;
	int32 Height = 0;
	FString FontPath;
	TUniquePtr<INativeClassRegistry> BindingRegistry;
	TUniquePtr<ILayoutEngine> DomLayout;
	TUniquePtr<FDomDocument> Document;
	TUniquePtr<ISurfaceRegistry> SurfaceRegistry;
	FSurfaceId Surface;
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

	// THE PRODUCTION SEAM (runs on the WORKER each tick): build the per-surface FDomDocument, read+eval the author
	// bundle (the app's own IIFE self-mounts), then compose. The production realization of the S4 gate-test seam body.
	FSurfaceRelayoutSeam Seam = [StateRef, EgressSeam](ICathodeJsRuntime& Runtime, TArray<FHostOutboundMessage>& OutFrameReady)
	{
		FOwnerWorkerState& S = *StateRef;
		if (!S.bInitialized)
		{
			S.BindingRegistry = CreateNativeClassRegistry(Runtime);
			S.DomLayout = MakeUnique<FDeferredDomLayout>();
			S.Document = CreateDomDocument(Runtime, *S.BindingRegistry, *S.DomLayout);
			if (S.Document.IsValid())
			{
				S.Document->InstallAsGlobal();
				TUniquePtr<IBundleSource> BundleSource = CreateUEFileBundleSource(GetDefaultResourceRoot());
				TArray<uint8> BundleBytes;
				if (BundleSource.IsValid() && BundleSource->Read(S.BundlePath, BundleBytes))
				{
					FString BundleStr;
					FFileHelper::BufferToString(BundleStr, BundleBytes.GetData(), BundleBytes.Num());
					Runtime.Evaluate(BundleStr, TEXT("<author-bundle>"), EEvalKind::Global);
				}
				S.SurfaceRegistry = CreateSurfaceRegistry();
				if (S.SurfaceRegistry.IsValid())
				{
					FSurfaceRegistration Reg = MakeCompositionSurfaceRegistration(
						*S.Document, S.Width, S.Height, ESurfacePixelFormat::PremultipliedBGRA8, S.FontPath);
					S.Surface = S.SurfaceRegistry->Register(MoveTemp(Reg));
				}
			}
			S.bInitialized = true;
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

void FCompositionSurfaceOwner::Pump()
{
	if (FCathodeRuntimeModule* const Module = GetRuntimeModule())
	{
		TArray<FHostOutboundMessage> Frames;
		Module->DrainOutbound(Frames);
		for (const FHostOutboundMessage& F : Frames)
		{
			Surface = FSurfaceId{ F.SurfaceTag }; // learn the worker-assigned surface id from the frame-ready
		}
	}
	if (Egress.IsValid())
	{
		Egress->PumpGameThreadUploads();
	}
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
		// Uninstall the seam first (applied between ticks -> the worker stops driving it), THEN post the doc Dispose
		// (also between ticks, after the uninstall) so the Dispose can't race a tick that's reading the doc.
		Module->InstallSurfaceRelayoutSeam(FSurfaceRelayoutSeam());
		TSharedPtr<FOwnerWorkerState> StateRef = State;
		Module->PostWorkerJob([StateRef]()
		{
			// ON THE WORKER: drop the doc's owned root ref while the runtime is alive (the reclaim; no leak).
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
	if (Egress.IsValid()) { Egress->Release(Surface); } // drop the old egress texture (game-thread)
	TSharedPtr<FOwnerWorkerState> StateRef = State;
	Module->PostWorkerJob([StateRef]()
	{
		// ON THE WORKER: unregister the old surface + register a FRESH one. The NEW FSurfaceId is "changed", so the next
		// seam DriveAll re-renders it -> the egress creates a NEW texture ptr -> the consumer rebinds.
		FOwnerWorkerState& S = *StateRef;
		if (S.SurfaceRegistry.IsValid() && S.Document.IsValid())
		{
			S.SurfaceRegistry->Unregister(S.Surface);
			FSurfaceRegistration Reg = MakeCompositionSurfaceRegistration(
				*S.Document, S.Width, S.Height, ESurfacePixelFormat::PremultipliedBGRA8, S.FontPath);
			S.Surface = S.SurfaceRegistry->Register(MoveTemp(Reg));
		}
	});
}
#endif

} // namespace Cathode
