#pragma once

#include "CoreMinimal.h"

#include "CathodeLayout.h" // FLayoutPaintOp, FBorderStroke, FLayoutFontRef

namespace Cathode
{

// Paint-routing translation, border decomposition, and the splice offset — OUR logic, entirely
// litehtml-FREE (operates on neutral params/types). The document_container converts litehtml types to
// these neutral inputs at the thin vendor edge, then calls these emit/decompose helpers. These are the
// bespoke surface the specs assert (which op + our-mapped params), never litehtml's computed boxes.

// --- Emit: one neutral paint op per draw intent ---
void EmitFillRect(TArray<FLayoutPaintOp>& Sink, const FBox2f& Rect, const FColor& Color);
void EmitFillBox(TArray<FLayoutPaintOp>& Sink, const FBox2f& Rect, const FColor& Color); // box / border edge
void EmitDrawImage(TArray<FLayoutPaintOp>& Sink, const FBox2f& DestRect, const FString& Source);
void EmitDrawText(TArray<FLayoutPaintOp>& Sink, const FString& Text, FLayoutFontRef Font,
	const FColor& Color, const FVector2f& Position, const FBox2f& Bounds);
void EmitClipPush(TArray<FLayoutPaintOp>& Sink, const FBox2f& Rect);
void EmitClipPop(TArray<FLayoutPaintOp>& Sink);

// --- Border decomposition: solid + square -> 4 edge FillBox ops; any styled side OR any corner radius
//     -> a single StrokeBorder op (the neutral primitive; the Rasterizer-side draw is the 003 #3 defer). ---
void DecomposeBorder(TArray<FLayoutPaintOp>& Sink, const FBorderStroke& Border);

// --- Splice offset: translate a subtree relayout's paint ops (relative) into document coordinates by the
//     boundary's offset. Built + tested here; the offset VALUE comes from litehtml (vendor edge). ---
void OffsetPaintOps(TArrayView<FLayoutPaintOp> Ops, const FVector2f& Offset);

} // namespace Cathode
