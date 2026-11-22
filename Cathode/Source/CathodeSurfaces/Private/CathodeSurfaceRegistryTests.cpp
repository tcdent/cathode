#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeSurfaces.h"

#include "Misc/AutomationTest.h"

// Real-behavior specs for the BESPOKE registry + lifecycle + DriveAll — we DRIVE the registry and observe
// recorded bookkeeping (construct/destruct counts, Num(), emitted surface ids/refs), never a presence check
// and never vendor output. The document + render target are RECORDING/COUNTING sibling impls: the document
// returns a scripted opaque batch; the target reports a scripted change + a scripted opaque front-buffer ref
// (the buffer PIXELS are never produced or asserted — carries-it).

namespace
{
using namespace Cathode;

// Shared lifetime counters — the recording sources bump these in ctor/dtor so a test can assert no-leak by
// construct==destruct, and that exactly the right surfaces were freed.
struct FLifeCounters
{
	int32 DocCtor = 0;
	int32 DocDtor = 0;
	int32 TgtCtor = 0;
	int32 TgtDtor = 0;
};

// A recording document: returns a scripted opaque paint batch; Surfaces carries it, never interprets it.
class FRecordingDocument final : public ISurfaceDocument
{
public:
	FRecordingDocument(FLifeCounters& InCounters, const void* InBatchHandle)
		: Counters(InCounters), BatchHandle(InBatchHandle) { Counters.DocCtor++; }
	virtual ~FRecordingDocument() override { Counters.DocDtor++; }

	virtual FSurfacePaintBatch RenderFrame(int32 /*AvailableWidth*/) override
	{
		RenderCount++;
		return FSurfacePaintBatch{ BatchHandle };
	}

	int32 RenderCount = 0;

private:
	FLifeCounters& Counters;
	const void* BatchHandle;
};

// A recording render target: scripted change result + scripted opaque front-buffer ref + a carried dirty
// rect. Never produces pixels; GetReadyFrame hands back the scripted ref so the relay can be asserted by
// identity.
class FRecordingTarget final : public ISurfaceRenderTarget
{
public:
	FRecordingTarget(FLifeCounters& InCounters, int32 InW, int32 InH, bool bInWillChange, const void* InBufferHandle)
		: Counters(InCounters), W(InW), H(InH), bWillChange(bInWillChange), BufferHandle(InBufferHandle)
	{
		Counters.TgtCtor++;
		ReadyRects.Add(FIntRect(0, 0, InW, InH)); // a carried dirty rect (producer bookkeeping, opaque to us)
	}
	virtual ~FRecordingTarget() override { Counters.TgtDtor++; }

	virtual bool ProduceFrame(const FSurfacePaintBatch& /*Batch*/) override { return bWillChange; }
	virtual FSurfaceReadyFrame GetReadyFrame() const override
	{
		return FSurfaceReadyFrame{ FSurfaceBufferRef{ BufferHandle }, ReadyRects };
	}
	virtual int32 GetWidth() const override { return W; }
	virtual int32 GetHeight() const override { return H; }
	virtual ESurfacePixelFormat GetFormat() const override { return ESurfacePixelFormat::PremultipliedBGRA8; }

private:
	FLifeCounters& Counters;
	int32 W;
	int32 H;
	bool bWillChange;
	const void* BufferHandle;
	TArray<FIntRect> ReadyRects;
};

FSurfaceRegistration MakeRegistration(FLifeCounters& Counters, int32 W, int32 H,
	bool bWillChange, const void* BatchHandle, const void* BufferHandle)
{
	FSurfaceRegistration Reg;
	Reg.Width = W;
	Reg.Height = H;
	Reg.Format = ESurfacePixelFormat::PremultipliedBGRA8;
	Reg.Document = MakeUnique<FRecordingDocument>(Counters, BatchHandle);
	Reg.Target = MakeUnique<FRecordingTarget>(Counters, W, H, bWillChange, BufferHandle);
	return Reg;
}

// A recording Egress sink — captures each surface-addressed readiness the relay emits.
struct FRecordedEmit
{
	FSurfaceId Surface;
	FSurfaceBufferRef Buffer;
	int32 NumDirtyRects = 0;
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeSurfacesRegisterTest,
	"Cathode.Surfaces.Registry.RegisterLookupCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeSurfacesRegisterTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;
	FLifeCounters C;
	TUniquePtr<ISurfaceRegistry> Registry = CreateSurfaceRegistry();

	TestEqual(TEXT("empty registry"), Registry->Num(), 0);

	const int32 BatchA = 0; const int32 BufA = 0; // distinct addresses as opaque handles
	const int32 BatchB = 0; const int32 BufB = 0;
	const FSurfaceId IdA = Registry->Register(MakeRegistration(C, 256, 128, true, &BatchA, &BufA));
	const FSurfaceId IdB = Registry->Register(MakeRegistration(C, 64, 64, true, &BatchB, &BufB));

	TestTrue(TEXT("ids are valid + distinct"), IdA.IsValid() && IdB.IsValid() && !(IdA == IdB));
	TestEqual(TEXT("two surfaces live"), Registry->Num(), 2);
	TestEqual(TEXT("two documents + two targets constructed"), C.DocCtor, 2);
	TestEqual(TEXT("nothing destroyed yet"), C.DocDtor + C.TgtDtor, 0);

	// Lookup returns the live surface with ITS registered (fixed) dims.
	FSurface* A = Registry->Find(IdA);
	FSurface* B = Registry->Find(IdB);
	TestTrue(TEXT("both found"), A != nullptr && B != nullptr);
	if (A) { TestTrue(TEXT("A keeps its registered dims"), A->GetWidth() == 256 && A->GetHeight() == 128); }
	if (B) { TestTrue(TEXT("B keeps its registered dims"), B->GetWidth() == 64 && B->GetHeight() == 64); }
	TestEqual(TEXT("unknown id -> null"), Registry->Find(FSurfaceId{ 9999 }), static_cast<FSurface*>(nullptr));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeSurfacesLifecycleTest,
	"Cathode.Surfaces.Registry.LifecycleNoLeak",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeSurfacesLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;
	FLifeCounters C;
	const int32 Anchor = 0;

	{
		TUniquePtr<ISurfaceRegistry> Registry = CreateSurfaceRegistry();
		const FSurfaceId IdA = Registry->Register(MakeRegistration(C, 100, 100, true, &Anchor, &Anchor));
		const FSurfaceId IdB = Registry->Register(MakeRegistration(C, 100, 100, true, &Anchor, &Anchor));
		TestEqual(TEXT("two constructed"), C.DocCtor + C.TgtCtor, 4);

		// Unregister A -> exactly A's document + target are freed; B untouched.
		Registry->Unregister(IdA);
		TestEqual(TEXT("one surface left"), Registry->Num(), 1);
		TestEqual(TEXT("A gone"), Registry->Find(IdA), static_cast<FSurface*>(nullptr));
		TestTrue(TEXT("B still live"), Registry->Find(IdB) != nullptr);
		TestEqual(TEXT("exactly A's handles freed"), C.DocDtor, 1);
		TestEqual(TEXT("exactly A's target freed"), C.TgtDtor, 1);

		// Unregister an unknown id -> no-op (no spurious frees, count unchanged).
		Registry->Unregister(FSurfaceId{ 4242 });
		TestEqual(TEXT("unknown unregister is a no-op"), Registry->Num(), 1);
		TestEqual(TEXT("no extra frees"), C.DocDtor + C.TgtDtor, 2);
	} // registry teardown here

	// Teardown freed the remaining surface (B): every constructed handle is destructed -> no leak.
	TestEqual(TEXT("all documents destructed on teardown"), C.DocDtor, C.DocCtor);
	TestEqual(TEXT("all targets destructed on teardown"), C.TgtDtor, C.TgtCtor);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeSurfacesDriveEmitTest,
	"Cathode.Surfaces.Registry.DriveAllEmitsOnlyChanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeSurfacesDriveEmitTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;
	FLifeCounters C;
	TUniquePtr<ISurfaceRegistry> Registry = CreateSurfaceRegistry();

	// Distinct buffer-handle anchors so each emit's ref identifies its source surface.
	const int32 BufA = 0, BufB = 0, BufC = 0;
	const int32 Batch = 0;
	const FSurfaceId IdA = Registry->Register(MakeRegistration(C, 32, 32, /*change*/ true,  &Batch, &BufA));
	const FSurfaceId IdB = Registry->Register(MakeRegistration(C, 32, 32, /*change*/ false, &Batch, &BufB)); // idle
	const FSurfaceId IdC = Registry->Register(MakeRegistration(C, 32, 32, /*change*/ true,  &Batch, &BufC));

	TArray<FRecordedEmit> Emits;
	FSurfaceReadinessSeam Sink = [&Emits](const FSurfaceReadiness& R)
	{
		Emits.Add(FRecordedEmit{ R.Surface, R.Frame.Buffer, R.Frame.DirtyRects.Num() });
	};

	Registry->DriveAll(Sink);

	// Only the two changed surfaces (A, C) emit; the idle one (B) stays silent.
	TestEqual(TEXT("only changed surfaces emit"), Emits.Num(), 2);

	auto EmitFor = [&Emits](FSurfaceId Id) -> const FRecordedEmit*
	{
		for (const FRecordedEmit& E : Emits) { if (E.Surface == Id) { return &E; } }
		return nullptr;
	};
	const FRecordedEmit* EA = EmitFor(IdA);
	const FRecordedEmit* EC = EmitFor(IdC);
	TestTrue(TEXT("A emitted (changed)"), EA != nullptr);
	TestTrue(TEXT("C emitted (changed)"), EC != nullptr);
	TestTrue(TEXT("B did NOT emit (idle)"), EmitFor(IdB) == nullptr);

	// The relay stamped the CORRECT surface identity onto each producer frame (buffer ref by identity —
	// never pixel values), and carried the producer's dirty rect.
	if (EA) { TestTrue(TEXT("A's emit carries A's buffer ref"), EA->Buffer == FSurfaceBufferRef{ &BufA } && EA->NumDirtyRects == 1); }
	if (EC) { TestTrue(TEXT("C's emit carries C's buffer ref"), EC->Buffer == FSurfaceBufferRef{ &BufC } && EC->NumDirtyRects == 1); }

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeSurfacesDriveLiveSetTest,
	"Cathode.Surfaces.Registry.DriveAllVisitsLiveSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeSurfacesDriveLiveSetTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;
	FLifeCounters C;
	TUniquePtr<ISurfaceRegistry> Registry = CreateSurfaceRegistry();

	const int32 BufA = 0, BufB = 0, Batch = 0;
	const FSurfaceId IdA = Registry->Register(MakeRegistration(C, 16, 16, true, &Batch, &BufA));
	const FSurfaceId IdB = Registry->Register(MakeRegistration(C, 16, 16, true, &Batch, &BufB));

	TArray<FSurfaceId> Emitted;
	FSurfaceReadinessSeam Sink = [&Emitted](const FSurfaceReadiness& R) { Emitted.Add(R.Surface); };

	// Both live + changed -> the drive visits each.
	Registry->DriveAll(Sink);
	TestEqual(TEXT("both live surfaces driven"), Emitted.Num(), 2);

	// Unregister A; the drive now visits only the live set (B).
	Emitted.Reset();
	Registry->Unregister(IdA);
	Registry->DriveAll(Sink);
	TestEqual(TEXT("only the remaining surface driven"), Emitted.Num(), 1);
	TestTrue(TEXT("the survivor is B"), Emitted.Num() == 1 && Emitted[0] == IdB);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
