#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeSurfaces.h"

#include "Misc/AutomationTest.h"

// Thin dedicated proof for the component's DEFINING behavior — the event-driven readiness relay — exercised
// DIRECTLY through FSurface::RenderAndRelay in isolation (not via the registry's DriveAll, which Slice 3
// already proved for the emit-on-change / correct-Id / buffer-ref-by-identity / rect-count path). This adds
// only the genuine residual: the isolated production path, the invalid-handle guard, multi-rect payload
// carry-through, and buffer-ref identity STABLE across successive frames. Carries-it structural (opaque
// handles, identity only — never pixels). Authored, not run this sprint.

namespace
{
using namespace Cathode;

// A fixed document: returns a scripted opaque batch (carried, never interpreted).
class FFixedDocument final : public ISurfaceDocument
{
public:
	explicit FFixedDocument(const void* InBatch) : Batch(InBatch) {}
	virtual FSurfacePaintBatch RenderFrame(int32 /*AvailableWidth*/) override { return FSurfacePaintBatch{ Batch }; }
private:
	const void* Batch;
};

// A scripted target: a fixed change-result, a fixed opaque front-buffer ref, and a configurable number of
// carried dirty rects. Never produces pixels; GetReadyFrame hands back the scripted ref + rects.
class FScriptedTarget final : public ISurfaceRenderTarget
{
public:
	FScriptedTarget(bool bInChange, const void* InBuffer, int32 InW, int32 InH, int32 NumRects)
		: bChange(bInChange), Buffer(InBuffer), W(InW), H(InH)
	{
		for (int32 i = 0; i < NumRects; ++i) { Rects.Add(FIntRect(i, i, i + 1, i + 1)); }
	}
	virtual bool ProduceFrame(const FSurfacePaintBatch& /*Batch*/) override { return bChange; }
	virtual FSurfaceReadyFrame GetReadyFrame() const override { return FSurfaceReadyFrame{ FSurfaceBufferRef{ Buffer }, Rects }; }
	virtual int32 GetWidth() const override { return W; }
	virtual int32 GetHeight() const override { return H; }
	virtual ESurfacePixelFormat GetFormat() const override { return ESurfacePixelFormat::PremultipliedBGRA8; }
private:
	bool bChange;
	const void* Buffer;
	int32 W;
	int32 H;
	TArray<FIntRect> Rects;
};

// Build a registration with a scripted document + target. (Returned by value: a move-only prvalue, elided.)
FSurfaceRegistration MakeReg(bool bChange, const void* Batch, const void* Buffer, int32 NumRects)
{
	FSurfaceRegistration Reg;
	Reg.Width = 32;
	Reg.Height = 32;
	Reg.Document = MakeUnique<FFixedDocument>(Batch);
	Reg.Target = MakeUnique<FScriptedTarget>(bChange, Buffer, 32, 32, NumRects);
	return Reg;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeSurfacesRelayIsolationTest,
	"Cathode.Surfaces.Relay.RenderAndRelayIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeSurfacesRelayIsolationTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;
	const int32 Batch = 0, Buf = 0;

	TArray<FSurfaceReadiness> Emits;
	FSurfaceReadinessSeam Sink = [&Emits](const FSurfaceReadiness& R) { Emits.Add(R); };

	// (a) A real change -> exactly one emit, this surface's Id + this target's buffer ref by identity.
	{
		Emits.Reset();
		FSurface S(FSurfaceId{ 1 }, MakeReg(/*change*/ true, &Batch, &Buf, /*rects*/ 1));
		const bool bRelayed = S.RenderAndRelay(Sink);
		TestTrue(TEXT("change -> RenderAndRelay returns true"), bRelayed);
		TestEqual(TEXT("change -> exactly one emit"), Emits.Num(), 1);
		if (Emits.Num() == 1)
		{
			TestTrue(TEXT("emit is addressed to THIS surface"), Emits[0].Surface == FSurfaceId{ 1 });
			TestTrue(TEXT("emit carries this target's buffer ref by identity"), Emits[0].Frame.Buffer == FSurfaceBufferRef{ &Buf });
		}
	}

	// (b) Idle (no change) -> no emit, returns false.
	{
		Emits.Reset();
		FSurface S(FSurfaceId{ 2 }, MakeReg(/*change*/ false, &Batch, &Buf, 1));
		const bool bRelayed = S.RenderAndRelay(Sink);
		TestFalse(TEXT("idle -> returns false"), bRelayed);
		TestEqual(TEXT("idle -> zero emit"), Emits.Num(), 0);
	}

	// (c) Invalid handles -> no emit, returns false (guarded). Null target.
	{
		Emits.Reset();
		FSurfaceRegistration Reg;
		Reg.Width = 32; Reg.Height = 32;
		Reg.Document = MakeUnique<FFixedDocument>(&Batch); // Target left null
		FSurface S(FSurfaceId{ 3 }, MoveTemp(Reg));
		const bool bRelayed = S.RenderAndRelay(Sink);
		TestFalse(TEXT("null target -> returns false"), bRelayed);
		TestEqual(TEXT("null target -> zero emit"), Emits.Num(), 0);
	}
	// Null document, valid target -> also guarded.
	{
		Emits.Reset();
		FSurfaceRegistration Reg;
		Reg.Width = 32; Reg.Height = 32;
		Reg.Target = MakeUnique<FScriptedTarget>(true, &Buf, 32, 32, 1); // Document left null
		FSurface S(FSurfaceId{ 4 }, MoveTemp(Reg));
		const bool bRelayed = S.RenderAndRelay(Sink);
		TestFalse(TEXT("null document -> returns false"), bRelayed);
		TestEqual(TEXT("null document -> zero emit"), Emits.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeSurfacesRelayPayloadTest,
	"Cathode.Surfaces.Relay.PayloadAssembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeSurfacesRelayPayloadTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;
	const int32 Batch = 0, Buf = 0;

	TArray<FSurfaceReadiness> Emits;
	FSurfaceReadinessSeam Sink = [&Emits](const FSurfaceReadiness& R) { Emits.Add(R); };

	// Multi-rect: a frame carrying >1 dirty rect -> the readiness carries that rect COUNT through (our
	// bookkeeping; the rects themselves are the producer's, carried opaque).
	{
		Emits.Reset();
		FSurface S(FSurfaceId{ 10 }, MakeReg(/*change*/ true, &Batch, &Buf, /*rects*/ 3));
		S.RenderAndRelay(Sink);
		TestEqual(TEXT("one emit"), Emits.Num(), 1);
		if (Emits.Num() == 1)
		{
			TestEqual(TEXT("multi-rect count carried through"), Emits[0].Frame.DirtyRects.Num(), 3);
		}
	}

	// Buffer-ref identity STABLE across successive change-frames: driving the same surface frame after frame
	// carries the surface's own front-buffer ref each time (the relay carries the target's current ref
	// verbatim; identity, never pixels).
	{
		Emits.Reset();
		FSurface S(FSurfaceId{ 11 }, MakeReg(/*change*/ true, &Batch, &Buf, 1));
		S.RenderAndRelay(Sink);
		S.RenderAndRelay(Sink);
		S.RenderAndRelay(Sink);
		TestEqual(TEXT("three successive change-frames -> three emits"), Emits.Num(), 3);
		bool bAllThisSurface = true;
		bool bRefStable = true;
		for (const FSurfaceReadiness& E : Emits)
		{
			bAllThisSurface &= (E.Surface == FSurfaceId{ 11 });
			bRefStable &= (E.Frame.Buffer == FSurfaceBufferRef{ &Buf });
		}
		TestTrue(TEXT("every frame addressed to this surface"), bAllThisSurface);
		TestTrue(TEXT("buffer ref is the surface's own, stable frame to frame (by identity)"), bRefStable);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
