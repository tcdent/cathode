#pragma once

#include "CoreMinimal.h"

#include "CathodeSurfaces.h" // FSurfaceRegistration, ESurfacePixelFormat

namespace Cathode
{

class ILayoutDocumentSource; // CathodeLayout — the document to lay out (the real DOM at integration)

// Build an FSurfaceRegistration whose neutral seams are backed by the REAL engines (the composition root's
// single construction point; the device-spawn path Register's the result into an ISurfaceRegistry and DriveAll's
// it):
//   - ISurfaceDocument  <- the real CathodeLayout ILayoutEngine over `Document`, constructed with a real
//                          ITextMeasurer adapter on CathodeText's HarfBuzz shaper.
//   - ISurfaceRenderTarget <- the real CathodeRasterizer producer; it maps the layout's FLayoutPaintOp stream
//                          to the Rasterizer's FPaintOp stream and rasterizes into the Blend2D bitmap.
// The engines + CathodeSurfaces stay neutral — these backers live ONLY here (the composition root).
//
// LIFETIME (load-bearing): the returned registration's document backer holds `Document` by REFERENCE — the
// document source (the live DOM at integration) is owned by the CALLER and MUST outlive the surface the
// registration is bound into. `FontFilePath` sources the measurer's font: production injects the A2-staged font
// (slice 3); the slice-1 tick proof injects the committed OFL fixture. (Slice 1: the text path is WIRED but not
// exercised — the measurer constructs Layout; glyph fill is slice 2.)
CATHODECOMPOSITION_API FSurfaceRegistration MakeCompositionSurfaceRegistration(
	const ILayoutDocumentSource& Document,
	int32 Width,
	int32 Height,
	ESurfacePixelFormat Format,
	const FString& FontFilePath);

} // namespace Cathode
