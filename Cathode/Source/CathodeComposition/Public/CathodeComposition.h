#pragma once

#include "CoreMinimal.h"

#include "CathodeSurfaces.h" // FSurfaceRegistration, ESurfacePixelFormat

namespace Cathode
{

class ILayoutDocumentSource; // CathodeLayout — the document to lay out (the real DOM at integration)

// The PRODUCTION content root: FPaths::ProjectContentDir()/Cathode.
// The cooked content location for Cathode's HTML/CSS/JS/font resources; the production caller passes this as the
// resolver root. (The first production ProjectContentDir() use in the plugin; staged via
// +DirectoriesToAlwaysStageAsUFS=/Game/Cathode.)
CATHODECOMPOSITION_API FString GetDefaultResourceRoot();

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
// registration is bound into. `FontFilePath` sources the measurer's font: production injects its staged font;
// the tick proofs inject the committed OFL fixture.
// bWireResourceResolver: when true (production default), the document backer constructs the
// PRODUCTION resource resolver — CreateResourceResolver(CreateUEFileBundleSource(GetDefaultResourceRoot())) — and
// injects it into the Layout engine before SetDocument, so linked resources (@import/<link>) resolve from the
// production default-root /Game/Cathode (= GetDefaultResourceRoot()). false = no resolver (the null-resolver
// control: linked rules never load -> defaults). The root is always the production default-root, never a test
// override.
CATHODECOMPOSITION_API FSurfaceRegistration MakeCompositionSurfaceRegistration(
	const ILayoutDocumentSource& Document,
	int32 Width,
	int32 Height,
	ESurfacePixelFormat Format,
	const FString& FontFilePath,
	bool bWireResourceResolver = true);

} // namespace Cathode
