#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CathodeExampleDeviceActor.h"  // ACathodeExampleDeviceActor — the targetable example actor (ICathodeTargetableSurface)
#include "CathodeCompositionOwner.h"    // FCompositionSurfaceOwner — Install / Designate / DeliverIntent / GetTexture
#include "CathodeSurfaces.h"            // ICathodeTargetableSurface / ECathodeInputIntent / FSurfaceId
#include "CathodeRuntimeModule.h"       // RestartWorkerForTesting (IN-SUITE)

#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "UObject/Package.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIResources.h"

// THE TARGETABLE DEVICE ACTOR PROOF (the joined bridge + intent + targetable-actor chain, Cathode-owned end-to-end). The host's targeting
// resolves the example actor's surface BY Cathode::ICathodeTargetableSurface (GetSurfaceId — NOT by naming the actor), then
// DesignateActiveSurface + DeliverIntent through the owner -> the surface document keydown fires with the canonical e.keys.
// Proven DIRECT: resolve the interface + designate + deliver — NO host-specific player-ray stub (the inversion's bite is "the
// host resolves the Cathode surface BY Cathode's interface," not the ray). The plugin actor names NO host type (no
// IPlayerInteractable — that's the integrator's host-side half). Host-agnostic: ZERO Uptime*.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeExampleTargetableDeviceProofTest,
	"Cathode.Example.TargetableDeviceDelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeExampleTargetableDeviceProofTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
	using namespace Cathode;

	FCathodeRuntimeModule* Module = FModuleManager::GetModulePtr<FCathodeRuntimeModule>(TEXT("CathodeRuntime"));
	if (Module == nullptr) { AddError(TEXT("[031-S3] CathodeRuntime module not loaded")); return false; }
	Module->RestartWorkerForTesting();

	const FString FontPath = []() -> FString
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
		return Plugin.IsValid() ? (Plugin->GetBaseDir() / TEXT("Source/CathodeText/Private/Tests/Fixtures/LiberationSans-Regular.ttf")) : FString();
	}();

	const int32 W = 64, H = 128;

	// A keydown-listener screen: a green marker at a Y that ENCODES e.key.
	const FString Bundle = TEXT(
		"var a=document.createElement('div'); a.setAttribute('style','position:absolute;left:30px;top:0px;width:4px;height:4px;background-color:#0000ff;'); document.body.appendChild(a);"
		"document.addEventListener('keydown', function(e){"
		"  var y = 0;"
		"  if (e.key === 'ArrowUp') y = 12; else if (e.key === 'ArrowDown') y = 24; else if (e.key === 'ArrowLeft') y = 36;"
		"  else if (e.key === 'ArrowRight') y = 48; else if (e.key === 'Enter') y = 60; else if (e.key === 'Escape') y = 72;"
		"  if (y > 0) { var m=document.createElement('div'); m.setAttribute('style','position:absolute;left:0px;top:'+y+'px;width:8px;height:8px;background-color:#00ff00;'); document.body.appendChild(m); }"
		"});");

	FCompositionSurfaceOwner Owner;
	Owner.Install({ FCompositionSurfaceSpec{ FString(), Bundle, W, H } }, FontPath);
	if (!Owner.IsInstalled()) { AddError(TEXT("[031-S3] the owner failed to install the seam")); return false; }

	auto Readback = [W, H](UTexture2D* Tex) -> TArray<FColor>
	{
		TArray<FColor> Out;
		if (Tex == nullptr) { return Out; }
		FTextureResource* Resource = Tex->GetResource();
		if (Resource == nullptr) { return Out; }
		ENQUEUE_RENDER_COMMAND(CathodeExampleTargetReadback)(
			[Resource, W, H, &Out](FRHICommandListImmediate& RHICmdList)
			{
				FRHITexture* TexRHI = Resource->GetTexture2DRHI();
				if (TexRHI != nullptr) { RHICmdList.ReadSurfaceData(TexRHI, FIntRect(0, 0, W, H), Out, FReadSurfaceDataFlags(RCM_UNorm)); }
			});
		FlushRenderingCommands();
		return Out;
	};
	auto GreenAt = [W, H](const TArray<FColor>& Px, int32 Y) -> int32
	{
		if (Px.Num() < W * H) { return -1; }
		int32 N = 0;
		for (int32 yy = Y; yy < Y + 8 && yy < H; ++yy) { for (int32 xx = 0; xx < 8; ++xx) { const FColor& P = Px[yy * W + xx]; if (P.G > 120 && P.R < 100 && P.B < 100) { ++N; } } }
		return N;
	};
	auto Teardown = [&Owner]() { Owner.Dispose(); FPlatformProcess::Sleep(0.1f); };

	// Drive to the first composed frame (so GetSurfaceId(0) is stable).
	UTexture2D* Tex = nullptr;
	{
		const double Start = FPlatformTime::Seconds();
		while ((FPlatformTime::Seconds() - Start) < 20.0)
		{
			Owner.Pump();
			Tex = Owner.GetTexture(Owner.GetSurfaceId(0));
			if (Tex != nullptr) { break; }
			FPlatformProcess::Sleep(0.01f);
		}
	}
	if (Tex == nullptr) { AddError(TEXT("[031-S3] TIMEOUT: the surface never produced a first frame")); Teardown(); return false; }

	// Wire the example actor to its Cathode surface (the integrator's setup: BindSurface(owner, surface id)).
	ACathodeExampleDeviceActor* const Actor = NewObject<ACathodeExampleDeviceActor>(GetTransientPackage());
	if (Actor == nullptr) { AddError(TEXT("[031-S3] failed to construct the example device actor")); Teardown(); return false; }
	Actor->BindSurface(&Owner, Owner.GetSurfaceId(0));

	// THE TARGETING INVERSION (DIRECT): the host's targeting resolves the actor's Cathode surface BY ICathodeTargetableSurface
	// — NOT by naming the actor type, NOT via a player-ray stub. Then designate + deliver through the owner.
	ICathodeTargetableSurface* const Target = Actor; // the host targets the Cathode surface through the interface
	const FSurfaceId TargetSurface = Target->GetSurfaceId();
	TestTrue(TEXT("[031-S3] the targetable actor exposes its Cathode surface via ICathodeTargetableSurface"), TargetSurface.IsValid());
	Owner.DesignateActiveSurface(TargetSurface);
	const ECathodeInputIntent Intents[6] = {
		ECathodeInputIntent::Up, ECathodeInputIntent::Down, ECathodeInputIntent::Left,
		ECathodeInputIntent::Right, ECathodeInputIntent::Accept, ECathodeInputIntent::Back
	};
	for (const ECathodeInputIntent Intent : Intents) { Owner.DeliverIntent(Intent); }

	// Drive until all six markers render (resolve-by-interface -> Designate -> DeliverIntent -> the canonical-e.key keydown).
	const int32 ExpectedYs[6] = { 12, 24, 36, 48, 60, 72 };
	const TCHAR* const Tokens[6] = { TEXT("ArrowUp"), TEXT("ArrowDown"), TEXT("ArrowLeft"), TEXT("ArrowRight"), TEXT("Enter"), TEXT("Escape") };
	auto AllSixPresent = [&](const TArray<FColor>& Px) -> bool
	{
		for (int32 i = 0; i < 6; ++i) { if (GreenAt(Px, ExpectedYs[i]) <= 0) { return false; } }
		return true;
	};
	TArray<FColor> Px;
	{
		const double Start = FPlatformTime::Seconds();
		while ((FPlatformTime::Seconds() - Start) < 20.0)
		{
			Owner.Pump();
			Px = Readback(Owner.GetTexture(Owner.GetSurfaceId(0)));
			if (AllSixPresent(Px)) { break; }
			FPlatformProcess::Sleep(0.02f);
		}
	}

	int32 Found = 0;
	for (int32 i = 0; i < 6; ++i)
	{
		const int32 G = GreenAt(Px, ExpectedYs[i]);
		if (G > 0) { ++Found; }
		TestTrue(FString::Printf(TEXT("[031-S3] resolve-by-ICathodeTargetableSurface -> Designate -> DeliverIntent[%d] -> keydown e.key==='%s' (Y=%d, px=%d)"),
			i, Tokens[i], ExpectedYs[i], G), G > 0);
	}
	AddInfo(FString::Printf(TEXT("[031-S3] targetable device: %d/6 intents via resolve-by-interface -> Designate -> DeliverIntent reached the surface keydown (the joined Cathode-owned chain)"), Found));

	Teardown();
	return true;
#else
	AddInfo(TEXT("[031-S3] the targetable device proof is Win64-gated (the GPU egress readback)"));
	return true;
#endif
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
