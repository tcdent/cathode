#include "CathodePaintRouting.h"

namespace Cathode
{

void EmitFillRect(TArray<FLayoutPaintOp>& Sink, const FBox2f& Rect, const FColor& Color)
{
	FLayoutPaintOp Op;
	Op.Kind = ELayoutPaintOpKind::FillRect;
	Op.Rect = Rect;
	Op.Color = Color;
	Sink.Add(MoveTemp(Op));
}

void EmitFillBox(TArray<FLayoutPaintOp>& Sink, const FBox2f& Rect, const FColor& Color)
{
	FLayoutPaintOp Op;
	Op.Kind = ELayoutPaintOpKind::FillBox;
	Op.Rect = Rect;
	Op.Color = Color;
	Sink.Add(MoveTemp(Op));
}

void EmitDrawImage(TArray<FLayoutPaintOp>& Sink, const FBox2f& DestRect, const FString& Source)
{
	FLayoutPaintOp Op;
	Op.Kind = ELayoutPaintOpKind::DrawImage;
	Op.Rect = DestRect;
	Op.ImageSource = Source;
	Sink.Add(MoveTemp(Op));
}

void EmitDrawText(TArray<FLayoutPaintOp>& Sink, const FString& Text, FLayoutFontRef Font,
	const FColor& Color, const FVector2f& Position, const FBox2f& Bounds)
{
	FLayoutPaintOp Op;
	Op.Kind = ELayoutPaintOpKind::DrawText;
	Op.Text = Text;
	Op.Font = Font;
	Op.Color = Color;
	Op.Position = Position;
	Op.Rect = Bounds;
	Sink.Add(MoveTemp(Op));
}

void EmitClipPush(TArray<FLayoutPaintOp>& Sink, const FBox2f& Rect)
{
	FLayoutPaintOp Op;
	Op.Kind = ELayoutPaintOpKind::ClipPush;
	Op.Rect = Rect;
	Sink.Add(MoveTemp(Op));
}

void EmitClipPop(TArray<FLayoutPaintOp>& Sink)
{
	FLayoutPaintOp Op;
	Op.Kind = ELayoutPaintOpKind::ClipPop;
	Sink.Add(MoveTemp(Op));
}

namespace
{
bool IsSideDrawn(const FBorderSide& Side)
{
	return Side.Width > 0.0f && Side.Style != EBorderStyle::None;
}

bool IsSideStyled(const FBorderSide& Side)
{
	// "Styled" = anything other than a plain solid edge (dashed/dotted/double need the stroke primitive).
	return Side.Style != EBorderStyle::None && Side.Style != EBorderStyle::Solid;
}

bool HasAnyRadius(const FCornerRadii& R)
{
	return R.TopLeft > 0.0f || R.TopRight > 0.0f || R.BottomRight > 0.0f || R.BottomLeft > 0.0f;
}
} // namespace

void DecomposeBorder(TArray<FLayoutPaintOp>& Sink, const FBorderStroke& Border)
{
	const bool bStyled = IsSideStyled(Border.Top) || IsSideStyled(Border.Right)
		|| IsSideStyled(Border.Bottom) || IsSideStyled(Border.Left);

	if (bStyled || HasAnyRadius(Border.Radii))
	{
		// Any styled side or any corner radius -> the neutral StrokeBorder op (Rasterizer draw = 003 #3).
		FLayoutPaintOp Op;
		Op.Kind = ELayoutPaintOpKind::StrokeBorder;
		Op.Border = Border;
		Sink.Add(MoveTemp(Op));
		return;
	}

	// Solid + square: 4 edge fills. Top/bottom span the full width; left/right span between them (no
	// corner double-cover), so per-side colors stay clean.
	const FVector2f Min = Border.Box.Min;
	const FVector2f Max = Border.Box.Max;
	const float TopW = Border.Top.Width;
	const float RightW = Border.Right.Width;
	const float BottomW = Border.Bottom.Width;
	const float LeftW = Border.Left.Width;

	if (IsSideDrawn(Border.Top))
	{
		EmitFillBox(Sink, FBox2f(FVector2f(Min.X, Min.Y), FVector2f(Max.X, Min.Y + TopW)), Border.Top.Color);
	}
	if (IsSideDrawn(Border.Bottom))
	{
		EmitFillBox(Sink, FBox2f(FVector2f(Min.X, Max.Y - BottomW), FVector2f(Max.X, Max.Y)), Border.Bottom.Color);
	}
	if (IsSideDrawn(Border.Left))
	{
		EmitFillBox(Sink, FBox2f(FVector2f(Min.X, Min.Y + TopW), FVector2f(Min.X + LeftW, Max.Y - BottomW)), Border.Left.Color);
	}
	if (IsSideDrawn(Border.Right))
	{
		EmitFillBox(Sink, FBox2f(FVector2f(Max.X - RightW, Min.Y + TopW), FVector2f(Max.X, Max.Y - BottomW)), Border.Right.Color);
	}
}

void OffsetPaintOps(TArrayView<FLayoutPaintOp> Ops, const FVector2f& Offset)
{
	for (FLayoutPaintOp& Op : Ops)
	{
		Op.Rect.Min += Offset;
		Op.Rect.Max += Offset;
		Op.Position += Offset;
		Op.Border.Box.Min += Offset;
		Op.Border.Box.Max += Offset;
	}
}

} // namespace Cathode
