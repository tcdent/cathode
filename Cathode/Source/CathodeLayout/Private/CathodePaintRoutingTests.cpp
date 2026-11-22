#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeLayout.h"
#include "CathodePaintRouting.h"

#include "Misc/AutomationTest.h"

// Specs for the BESPOKE paint-routing translation, border decomposition, and splice offset — all
// litehtml-FREE (the helpers take neutral params; the container does the thin litehtml->neutral edge
// conversion elsewhere). They assert WHICH neutral op + OUR-mapped params; the inputs are scripted/opaque
// (never litehtml's computed boxes).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeLayoutPaintEmitTest,
	"Cathode.Layout.Paint.EmitOps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutPaintEmitTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	TArray<FLayoutPaintOp> Sink;
	EmitFillRect(Sink, FBox2f(FVector2f(1, 2), FVector2f(11, 12)), FColor(10, 20, 30, 40));
	const TArray<uint8> ImgBytes{ 0xDE, 0xAD, 0xBE, 0xEF }; // the layout-carried resolved bytes
	EmitDrawImage(Sink, FBox2f(FVector2f(0, 0), FVector2f(64, 64)), TEXT("logo.png"), ImgBytes);
	EmitDrawText(Sink, TEXT("hi"), FLayoutFontRef{ 7 }, FColor(1, 2, 3, 4), FVector2f(5, 6), FBox2f(FVector2f(5, 6), FVector2f(25, 18)));
	EmitClipPush(Sink, FBox2f(FVector2f(0, 0), FVector2f(50, 50)));
	EmitClipPop(Sink);

	TestEqual(TEXT("five ops emitted"), Sink.Num(), 5);
	if (Sink.Num() == 5)
	{
		TestTrue(TEXT("FillRect kind + our rect/color"), Sink[0].Kind == ELayoutPaintOpKind::FillRect
			&& Sink[0].Rect == FBox2f(FVector2f(1, 2), FVector2f(11, 12)) && Sink[0].Color == FColor(10, 20, 30, 40));
		TestTrue(TEXT("DrawImage kind + dest + source + carried bytes"), Sink[1].Kind == ELayoutPaintOpKind::DrawImage
			&& Sink[1].Rect == FBox2f(FVector2f(0, 0), FVector2f(64, 64)) && Sink[1].ImageSource == FString(TEXT("logo.png"))
			&& Sink[1].ImageBytes == ImgBytes);
		TestTrue(TEXT("DrawText kind + text/font/color/pos"), Sink[2].Kind == ELayoutPaintOpKind::DrawText
			&& Sink[2].Text == FString(TEXT("hi")) && Sink[2].Font == FLayoutFontRef{ 7 }
			&& Sink[2].Color == FColor(1, 2, 3, 4) && Sink[2].Position == FVector2f(5, 6));
		TestTrue(TEXT("ClipPush kind + rect"), Sink[3].Kind == ELayoutPaintOpKind::ClipPush
			&& Sink[3].Rect == FBox2f(FVector2f(0, 0), FVector2f(50, 50)));
		TestTrue(TEXT("ClipPop kind"), Sink[4].Kind == ELayoutPaintOpKind::ClipPop);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeLayoutBorderDecompositionTest,
	"Cathode.Layout.Paint.BorderDecomposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutBorderDecompositionTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// A solid, square border with distinct per-side widths/colors -> 4 edge FillBox ops (our computed rects).
	{
		FBorderStroke B;
		B.Box = FBox2f(FVector2f(0, 0), FVector2f(100, 50));
		B.Top = FBorderSide{ 2.0f, EBorderStyle::Solid, FColor(1, 0, 0, 255) };
		B.Right = FBorderSide{ 3.0f, EBorderStyle::Solid, FColor(0, 1, 0, 255) };
		B.Bottom = FBorderSide{ 4.0f, EBorderStyle::Solid, FColor(0, 0, 1, 255) };
		B.Left = FBorderSide{ 5.0f, EBorderStyle::Solid, FColor(1, 1, 1, 255) };

		TArray<FLayoutPaintOp> Sink;
		DecomposeBorder(Sink, B);

		TestEqual(TEXT("solid square border -> 4 edge fills"), Sink.Num(), 4);
		if (Sink.Num() == 4)
		{
			TestTrue(TEXT("all edges are FillBox"),
				Sink[0].Kind == ELayoutPaintOpKind::FillBox && Sink[1].Kind == ELayoutPaintOpKind::FillBox
				&& Sink[2].Kind == ELayoutPaintOpKind::FillBox && Sink[3].Kind == ELayoutPaintOpKind::FillBox);
			TestTrue(TEXT("top edge rect + color"), Sink[0].Rect == FBox2f(FVector2f(0, 0), FVector2f(100, 2)) && Sink[0].Color == FColor(1, 0, 0, 255));
			TestTrue(TEXT("bottom edge rect + color"), Sink[1].Rect == FBox2f(FVector2f(0, 46), FVector2f(100, 50)) && Sink[1].Color == FColor(0, 0, 1, 255));
			TestTrue(TEXT("left edge between top+bottom"), Sink[2].Rect == FBox2f(FVector2f(0, 2), FVector2f(5, 46)) && Sink[2].Color == FColor(1, 1, 1, 255));
			TestTrue(TEXT("right edge between top+bottom"), Sink[3].Rect == FBox2f(FVector2f(97, 2), FVector2f(100, 46)) && Sink[3].Color == FColor(0, 1, 0, 255));
		}
	}

	// A styled side (dashed) -> a single StrokeBorder op carrying our border params.
	{
		FBorderStroke B;
		B.Box = FBox2f(FVector2f(0, 0), FVector2f(100, 50));
		B.Top = FBorderSide{ 2.0f, EBorderStyle::Dashed, FColor(1, 0, 0, 255) };
		TArray<FLayoutPaintOp> Sink;
		DecomposeBorder(Sink, B);
		TestEqual(TEXT("styled border -> one StrokeBorder op"), Sink.Num(), 1);
		if (Sink.Num() == 1)
		{
			TestTrue(TEXT("kind is StrokeBorder"), Sink[0].Kind == ELayoutPaintOpKind::StrokeBorder);
			TestTrue(TEXT("carries the box + a dashed top side"),
				Sink[0].Border.Box == B.Box && Sink[0].Border.Top.Style == EBorderStyle::Dashed);
		}
	}

	// A corner radius (solid sides) -> a single StrokeBorder op.
	{
		FBorderStroke B;
		B.Box = FBox2f(FVector2f(0, 0), FVector2f(100, 50));
		B.Top = FBorderSide{ 2.0f, EBorderStyle::Solid, FColor(1, 0, 0, 255) };
		B.Radii.TopLeft = 5.0f;
		TArray<FLayoutPaintOp> Sink;
		DecomposeBorder(Sink, B);
		TestEqual(TEXT("rounded border -> one StrokeBorder op"), Sink.Num(), 1);
		if (Sink.Num() == 1)
		{
			TestTrue(TEXT("kind is StrokeBorder"), Sink[0].Kind == ELayoutPaintOpKind::StrokeBorder);
		}
	}

	// A border with only one non-zero side -> only that edge.
	{
		FBorderStroke B;
		B.Box = FBox2f(FVector2f(0, 0), FVector2f(100, 50));
		B.Top = FBorderSide{ 2.0f, EBorderStyle::Solid, FColor(1, 0, 0, 255) };
		// Right/Bottom/Left default to width 0 / None.
		TArray<FLayoutPaintOp> Sink;
		DecomposeBorder(Sink, B);
		TestEqual(TEXT("only the non-zero side is emitted"), Sink.Num(), 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeLayoutSpliceOffsetTest,
	"Cathode.Layout.Paint.SpliceOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutSpliceOffsetTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	TArray<FLayoutPaintOp> Ops;
	EmitFillRect(Ops, FBox2f(FVector2f(10, 10), FVector2f(20, 20)), FColor::White);
	EmitDrawText(Ops, TEXT("x"), FLayoutFontRef{ 1 }, FColor::White, FVector2f(5, 5), FBox2f(FVector2f(5, 5), FVector2f(15, 15)));
	{
		FBorderStroke B; B.Box = FBox2f(FVector2f(0, 0), FVector2f(10, 10)); B.Top = FBorderSide{ 1, EBorderStyle::Dashed, FColor::Black };
		DecomposeBorder(Ops, B); // a StrokeBorder op
	}

	// Splice the subtree ops into document coords by the boundary offset (100, 200).
	OffsetPaintOps(TArrayView<FLayoutPaintOp>(Ops.GetData(), Ops.Num()), FVector2f(100, 200));

	TestEqual(TEXT("three ops"), Ops.Num(), 3);
	if (Ops.Num() == 3)
	{
		TestTrue(TEXT("FillRect translated to doc coords"), Ops[0].Rect == FBox2f(FVector2f(110, 210), FVector2f(120, 220)));
		TestTrue(TEXT("DrawText position + bounds translated"),
			Ops[1].Position == FVector2f(105, 205) && Ops[1].Rect == FBox2f(FVector2f(105, 205), FVector2f(115, 215)));
		TestTrue(TEXT("StrokeBorder box translated"), Ops[2].Border.Box == FBox2f(FVector2f(100, 200), FVector2f(110, 210)));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
