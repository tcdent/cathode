#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ExampleGameBridge.h"          // CathodeExample::FExampleGameBridge — the reference host bridge under test
#include "CathodeCompositionOwner.h"    // FCompositionSurfaceOwner / FCompositionSurfaceSpec — the registration point
#include "CathodeSurfaces.h"            // 031 S2 — ECathodeInputIntent (the Cathode-owned intent vocabulary)
#include "CathodeRuntimeModule.h"       // RestartWorkerForTesting (IN-SUITE)

#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIResources.h"

// 031 SLICE 1 — THE game.* BRIDGE EXAMPLE PROOF (mutate-and-observe + the stub-swap discriminator). The reference host
// FExampleGameBridge is REGISTERED into Cathode via the injecting Install; an author screen READS game.* (game.tickCount +
// game.getEntityName) and renders it; the proof asserts the SCREEN reflects THIS host's live state. The stub-swap bite: a
// DIFFERENT bridge value yields a DIFFERENT rendered result — a hardcoded/constant render passes a single-value check but REDs
// the swap.
//
// NOTE (SLICE 1 scope): the screen here is the TEST-only INLINE bundle (FCompositionSurfaceSpec.InlineBundleJs). The faithful
// SHIPPABLE reference screen is an AUTHORED .ts -> the 025 gate -> .iife.js read via BundlePath — the recommended integrator
// path, flagged as the SLICE-1 authored-screen follow-on (it needs the Node gate + content staging, heavier than this C++
// registration slice). The registration mechanism (the bridge + the injecting Install + the game.* projection) is faithful;
// the inline screen is the proof's observation harness.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeExampleGameBridgeProofTest,
	"Cathode.Example.GameBridgeReadsHostState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeExampleGameBridgeProofTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
	using namespace Cathode;

	FCathodeRuntimeModule* Module = FModuleManager::GetModulePtr<FCathodeRuntimeModule>(TEXT("CathodeRuntime"));
	if (Module == nullptr) { AddError(TEXT("[031-S1] CathodeRuntime module not loaded")); return false; }
	Module->RestartWorkerForTesting();

	const FString FontPath = []() -> FString
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
		return Plugin.IsValid() ? (Plugin->GetBaseDir() / TEXT("Source/CathodeText/Private/Tests/Fixtures/LiberationSans-Regular.ttf")) : FString();
	}();

	const int32 W = 64, H = 128;

	// The author screen READS game.* and renders it: game.tickCount -> a GREEN marker at Y=tickCount (left 0); and
	// game.getEntityName('player') === 'Hero' -> a MAGENTA marker (left 40). Both come from the host bridge, not constants.
	const FString Bundle = TEXT(
		"var t = game.tickCount;"
		"var m = document.createElement('div');"
		"m.setAttribute('style','position:absolute;left:0px;top:'+t+'px;width:8px;height:8px;background-color:#00ff00;');"
		"document.body.appendChild(m);"
		"var name = game.getEntityName('player');"
		"if (name === 'Hero') {"
		"  var n = document.createElement('div');"
		"  n.setAttribute('style','position:absolute;left:40px;top:4px;width:8px;height:8px;background-color:#ff00ff;');"
		"  document.body.appendChild(n);"
		"}");

	// THE REFERENCE WIRING — register the host bridge INTO Cathode via the injecting Install. Keep a raw ptr to mutate the
	// host state after handing ownership to the owner (the integrator's host owns + drives its bridge).
	TUniquePtr<CathodeExample::FExampleGameBridge> BridgeOwned = MakeUnique<CathodeExample::FExampleGameBridge>();
	CathodeExample::FExampleGameBridge* Bridge = BridgeOwned.Get();
	Bridge->SetTick(100);                         // the host's tick (seed); the screen reads it as game.tickCount
	Bridge->AddEntity(TEXT("player"), TEXT("Hero")); // the host's named entity; the screen reads game.getEntityName('player')

	FCompositionSurfaceOwner Owner;
	Owner.Install({ FCompositionSurfaceSpec{ FString(), Bundle, W, H } }, FontPath, MoveTemp(BridgeOwned));
	if (!Owner.IsInstalled()) { AddError(TEXT("[031-S1] the owner failed to install the seam with the example bridge")); return false; }

	auto Readback = [W, H](UTexture2D* Tex) -> TArray<FColor>
	{
		TArray<FColor> Out;
		if (Tex == nullptr) { return Out; }
		FTextureResource* Resource = Tex->GetResource();
		if (Resource == nullptr) { return Out; }
		ENQUEUE_RENDER_COMMAND(CathodeExampleReadback)(
			[Resource, W, H, &Out](FRHICommandListImmediate& RHICmdList)
			{
				FRHITexture* TexRHI = Resource->GetTexture2DRHI();
				if (TexRHI != nullptr) { RHICmdList.ReadSurfaceData(TexRHI, FIntRect(0, 0, W, H), Out, FReadSurfaceDataFlags(RCM_UNorm)); }
			});
		FlushRenderingCommands();
		return Out;
	};
	auto CountColor = [W, H](const TArray<FColor>& Px, int32 X0, int32 X1, int32 Y0, int32 Y1, const TFunction<bool(const FColor&)>& Pred) -> int32
	{
		if (Px.Num() < W * H) { return -1; }
		int32 N = 0;
		for (int32 Y = Y0; Y < Y1 && Y < H; ++Y) { for (int32 X = X0; X < X1 && X < W; ++X) { if (Pred(Px[Y * W + X])) { ++N; } } }
		return N;
	};
	auto IsGreen = [](const FColor& P) { return P.G > 120 && P.R < 100 && P.B < 100; };
	auto IsMagenta = [](const FColor& P) { return P.R > 120 && P.B > 120 && P.G < 100; };
	auto IsCyan = [](const FColor& P) { return P.G > 120 && P.B > 120 && P.R < 100; };
	auto Teardown = [&Owner]() { Owner.Dispose(); FPlatformProcess::Sleep(0.1f); };

	// Drive to the first composed frame.
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
	if (Tex == nullptr) { AddError(TEXT("[031-S1] TIMEOUT: the example surface never produced a first frame")); Teardown(); return false; }

	// READS HOST STATE: the screen reflected game.tickCount=100 (GREEN at Y~100) + game.getEntityName('player')==='Hero' (MAGENTA).
	{
		const TArray<FColor> Px = Readback(Owner.GetTexture(Owner.GetSurfaceId(0)));
		const int32 GreenAtTick = CountColor(Px, 0, 8, 96, 112, IsGreen);  // Y~100 (tickCount)
		const int32 GreenElsewhere = CountColor(Px, 0, 8, 0, 80, IsGreen); // NOT at a constant low Y
		const int32 Magenta = CountColor(Px, 40, 48, 0, 16, IsMagenta);
		AddInfo(FString::Printf(TEXT("[031-S1] reads host state: green@tick=%d (Y~100), green-low=%d, magenta(Hero)=%d"), GreenAtTick, GreenElsewhere, Magenta));
		TestTrue(TEXT("[031-S1] the screen read game.tickCount=100 -> GREEN marker at Y~100"), GreenAtTick > 0);
		TestTrue(TEXT("[031-S1] the screen read game.getEntityName('player')==='Hero' -> MAGENTA marker"), Magenta > 0);
	}

	// THE STUB-SWAP DISCRIMINATOR: mutate the HOST bridge (tick 100 -> 50) + re-read game.tickCount on the live wire -> the
	// render TRACKS the new host value (a CYAN marker at Y~50). A hardcoded/constant render would put it at Y~100 (the baked
	// value) -> NO cyan at Y~50 -> RED. This bites a render that ignores the host's live state.
	Bridge->SetTick(50);
	Owner.SetPendingMutationForTest(TEXT(
		"var t2 = game.tickCount;"
		"var m2 = document.createElement('div');"
		"m2.setAttribute('style','position:absolute;left:16px;top:'+t2+'px;width:8px;height:8px;background-color:#00ffff;');"
		"document.body.appendChild(m2);"), 0);
	{
		const double Start = FPlatformTime::Seconds();
		bool bSawCyan = false;
		while ((FPlatformTime::Seconds() - Start) < 20.0)
		{
			Owner.Pump();
			const TArray<FColor> Px = Readback(Owner.GetTexture(Owner.GetSurfaceId(0)));
			if (CountColor(Px, 16, 24, 46, 62, IsCyan) > 0) { bSawCyan = true; break; } // Y~50 (the mutated tick)
			FPlatformProcess::Sleep(0.02f);
		}
		const TArray<FColor> Px = Readback(Owner.GetTexture(Owner.GetSurfaceId(0)));
		const int32 CyanAt50 = CountColor(Px, 16, 24, 46, 62, IsCyan);
		const int32 CyanAt100 = CountColor(Px, 16, 24, 96, 112, IsCyan); // a baked render would land here (the old 100)
		AddInfo(FString::Printf(TEXT("[031-S1] stub-swap: after SetTick(50) the render tracks game.tickCount -> cyan@Y~50=%d, cyan@Y~100=%d"), CyanAt50, CyanAt100));
		TestTrue(TEXT("[031-S1] stub-swap discriminator: a DIFFERENT bridge tick (50) -> a DIFFERENT render (cyan at Y~50, not the baked 100)"), bSawCyan && CyanAt50 > 0);
		TestTrue(TEXT("[031-S1] the render did NOT bake the old tick (no cyan at Y~100)"), CyanAt100 == 0);
	}

	Teardown();
	return true;
#else
	AddInfo(TEXT("[031-S1] the game.* bridge example proof is Win64-gated (the GPU egress readback)"));
	return true;
#endif
}

// 031 SLICE 2 — THE INPUT-INTENT INVERSION PROOF (Cathode owns the contract). The example HOST delivers SEMANTIC intents into
// Cathode (Owner.DesignateActiveSurface + Owner.DeliverIntent) — NOT a host port, NOT raw keys. Cathode owns the intent->key
// map (CathodeIntentToKey) + routes to the active surface; the six intents fire the surface document keydown with the six
// CANONICAL e.keys. The per-intent-map discriminator: each intent maps to its OWN canonical key (a constant/wrong map -> the
// wrong/missing markers -> RED). Host-agnostic: this example names NO Uptime* type; it calls Cathode's intent contract.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeExampleIntentDeliveryProofTest,
	"Cathode.Example.IntentDeliveryToSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeExampleIntentDeliveryProofTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
	using namespace Cathode;

	FCathodeRuntimeModule* Module = FModuleManager::GetModulePtr<FCathodeRuntimeModule>(TEXT("CathodeRuntime"));
	if (Module == nullptr) { AddError(TEXT("[031-S2] CathodeRuntime module not loaded")); return false; }
	Module->RestartWorkerForTesting();

	const FString FontPath = []() -> FString
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
		return Plugin.IsValid() ? (Plugin->GetBaseDir() / TEXT("Source/CathodeText/Private/Tests/Fixtures/LiberationSans-Regular.ttf")) : FString();
	}();

	const int32 W = 64, H = 128;

	// A keydown-listener screen: a green marker at a Y that ENCODES e.key (so a marker at Y proves that intent's canonical key).
	const FString Bundle = TEXT(
		"var a=document.createElement('div'); a.setAttribute('style','position:absolute;left:30px;top:0px;width:4px;height:4px;background-color:#0000ff;'); document.body.appendChild(a);"
		"document.addEventListener('keydown', function(e){"
		"  var y = 0;"
		"  if (e.key === 'ArrowUp') y = 12; else if (e.key === 'ArrowDown') y = 24; else if (e.key === 'ArrowLeft') y = 36;"
		"  else if (e.key === 'ArrowRight') y = 48; else if (e.key === 'Enter') y = 60; else if (e.key === 'Escape') y = 72;"
		"  if (y > 0) { var m=document.createElement('div'); m.setAttribute('style','position:absolute;left:0px;top:'+y+'px;width:8px;height:8px;background-color:#00ff00;'); document.body.appendChild(m); }"
		"});");

	// The example needs no game.* for input — the bridge-less Install (the seam makes the default stub).
	FCompositionSurfaceOwner Owner;
	Owner.Install({ FCompositionSurfaceSpec{ FString(), Bundle, W, H } }, FontPath);
	if (!Owner.IsInstalled()) { AddError(TEXT("[031-S2] the owner failed to install the seam")); return false; }

	auto Readback = [W, H](UTexture2D* Tex) -> TArray<FColor>
	{
		TArray<FColor> Out;
		if (Tex == nullptr) { return Out; }
		FTextureResource* Resource = Tex->GetResource();
		if (Resource == nullptr) { return Out; }
		ENQUEUE_RENDER_COMMAND(CathodeExampleIntentReadback)(
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

	// Drive to the first composed frame.
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
	if (Tex == nullptr) { AddError(TEXT("[031-S2] TIMEOUT: the surface never produced a first frame")); Teardown(); return false; }

	// THE INVERSION: the host DESIGNATES the active surface + DELIVERS the six SEMANTIC intents into Cathode (not a host port,
	// not raw keys). Cathode maps each intent -> its canonical key -> the active surface's keydown.
	Owner.DesignateActiveSurface(Owner.GetSurfaceId(0));
	const ECathodeInputIntent Intents[6] = {
		ECathodeInputIntent::Up, ECathodeInputIntent::Down, ECathodeInputIntent::Left,
		ECathodeInputIntent::Right, ECathodeInputIntent::Accept, ECathodeInputIntent::Back
	};
	for (const ECathodeInputIntent Intent : Intents) { Owner.DeliverIntent(Intent); }

	// Drive until all six markers render (each intent -> its canonical e.key -> the listener's marker at that Y).
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
		TestTrue(FString::Printf(TEXT("[031-S2] intent[%d] -> Cathode DeliverIntent -> document keydown e.key==='%s' (marker Y=%d, px=%d)"),
			i, Tokens[i], ExpectedYs[i], G), G > 0);
	}
	AddInfo(FString::Printf(TEXT("[031-S2] input-intent inversion: %d/6 intents through Cathode's DeliverIntent -> the active surface's canonical-e.key keydown"), Found));

	// Exercise the release path: after ReleaseActiveSurface there is no active input target, so DeliverIntent is a safe no-op
	// (nothing dispatched, no crash). The headline is the 6/6 canonical delivery above.
	Owner.ReleaseActiveSurface();
	Owner.DeliverIntent(ECathodeInputIntent::Up); // no active surface -> no-op

	Teardown();
	return true;
#else
	AddInfo(TEXT("[031-S2] the input-intent inversion proof is Win64-gated (the GPU egress readback)"));
	return true;
#endif
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
