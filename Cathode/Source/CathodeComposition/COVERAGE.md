# Cathode Composition — component coverage & accountable deferrals

Durable accountability record for **CathodeComposition** (sprint 023, the integration phase). The composition
root: the SOLE site that deps the engines together and wires them into one driven pipeline. It backs
CathodeSurfaces' neutral seams with the real engines (injected into `FSurface`), owns all the cross-engine glue
(the `FLayoutPaintOp`→`FPaintOp` mapper, the `ITextMeasurer` adapter, the production resource resolver wiring),
and has **no dependents** — so depping the engines confines them here; the engines + CathodeSurfaces + CathodeDom
stay neutral leaves (the dep-confinement that decided this dedicated module over extending CathodeDom).

Deps (Build.cs): Core + CathodeSurfaces + CathodeLayout + CathodeRasterizer + CathodeText + CathodeResolver +
CathodeDom. No dependents. This is the only new cross-engine dep site in the plugin.

## Covered

| Responsibility | Slice | Mechanism |
|---|---|---|
| **Back ISurfaceDocument with the real Layout** | 023 s1/s3 | `FLayoutSurfaceDocument` — the real CathodeLayout `ILayoutEngine` (`Relayout` → `FLayoutResult` paint ops, exposed as the opaque `FSurfacePaintBatch`); the production resource resolver injected before `SetDocument` (s3). |
| **Back ISurfaceRenderTarget with the real Rasterizer** | 023 s1 | `FRasterizerSurfaceRenderTarget` — the real CathodeRasterizer producer via the `ISurfaceProducer`/`CreateSurfaceRasterizer` factory (the minimal promotion of the private `FSurfaceRasterizer`); captures the push `FFrameReadySeam` `FBitmapData` and re-exposes it via `GetReadyFrame`. |
| **The FLayoutPaintOp → FPaintOp mapper** | 023 s1/s2 | non-text (FillRect/FillBox/ClearRect) + the TEXT case (s2): shape `FLayoutPaintOp.Text` via the shared adapter → the 022 `FShapedGlyph`→`FRasterGlyph` map → `FPositionedGlyphRun` → DrawGlyphRun, with the top-left→baseline bridge. (DrawImage / StrokeBorder / Clip → deferred.) |
| **The ITextMeasurer adapter (the same-bytes anchor)** | 023 s1/s2 | `FShaperTextMeasurer` over CathodeText's HarfBuzz shaper + font registry, **SHARED** between the document + render-target backers so **one `FFontHandle`** threads measure (Layout) + shape (the mapper) + raster (DrawGlyphRun). `ShapeText`/`GetFontBytes` is the raster-side shaping service. |
| **The per-frame tick (the composed pipeline)** | 023 s1 | `ISurfaceRegistry::DriveAll` → `FSurface::RenderAndRelay` → the real Layout → the mapper → the real Rasterizer → `GetReadyFrame` → the bitmap. The first composed frame end-to-end. |
| **The A2 production resource resolver wiring** | 023 s3 | `FLayoutSurfaceDocument` constructs `CreateResourceResolver(CreateUEFileBundleSource(GetDefaultResourceRoot()))` and injects it (the production CALLER — 020 had only the receiver hook). `GetDefaultResourceRoot()` = `ProjectContentDir()/Cathode` == the `+DirectoriesToAlwaysStageAsUFS=/Game/Cathode` staging entry (the in-tree literal match). |
| **The composition-surface factory** | 023 | `MakeCompositionSurfaceRegistration(Document, W, H, Format, FontFilePath, bWireResourceResolver)` — the single construction point; the device-spawn path Registers the result and DriveAll's it. |

**Proofs (Win64-content-gated, the composed tick, full Cathode 119/119):**
- `Cathode.Composition.Tick.ComposedFrameRendersDocumentBox` (s1) — a div's color at its Layout-computed box (ink-in=637, ink-outside=0).
- `Cathode.Composition.Tick.ComposedFrameRendersTextAtMeasuredWidth` (s2) — the adapter's measured width (blue shrink-to-fit bg) + the shaper's red glyphs, two-color discriminated.
- `Cathode.Composition.Tick.ComposedFrameRendersLinkedResource` (s3) — a linked CSS's green renders via the production resolver (green=1600) vs the null-resolver control (0).

## Integration-found bug fixes (the composed render exposed two assumptions the unit-tested engines never exercised)

1. **Uninitialized surface buffer → `FBlend2DBitmap::Create` clears to transparent.** The dirty-region model
   (`FSurfaceRasterizer::RasterizeFrame` redraws only dirty rects) ACCUMULATES, so it assumes a defined initial
   buffer — but `BLImage::create` doesn't zero, and nothing cleared it. The first composed render showed
   uninitialized garbage outside the drawn region (non-deterministic 34→635 across builds). Fix: the real bitmap
   owner clears `clear_all` on creation (CathodeRasterizer). Browse-design ruled the owner-clears invariant.
2. **Top-left → baseline coordinate bridge (the mapper).** Layout's DrawText `Position` is the text-box top-left
   (litehtml `pos.y`); Blend2D's DrawGlyphRun origin is the **baseline**. Passing the top-left straight put the
   glyphs above the box → clipped → zero ink. Fix: the mapper bridges the conventions — `GlyphOrigin.Y =
   Position.Y + ascent`. Each engine is correct in its own convention; the integration glue bridges them.

## Deferred / boundaries (the residuals 023 leaves)

- **(a) Cooked-pak staging — corroborated-inference; deferred packaged-build pass.** `+DirectoriesToAlwaysStageAsUFS=/Game/Cathode`
  + the default-root↔staging literal match are in-tree-validated; that `/Game/Cathode` actually stages into the
  cooked pak is INFERRED (config + match). The EMPIRICAL packaged read needs a cook/package run — **env's domain**,
  sequenced by the lead/tcdent. The editor test reads on-disk `Content/Cathode/`, not the staged pak.
- **(b) The face-name → FFontHandle font catalog** — deferred. Slice 2 used a single default OFL font (no
  font-family); the catalog (CSS font-family → font file) lives here (the composition glue) when needed.
- **(c) Glyph-op empty-Rect / glyph-only-frame dirty** — CathodeRasterizer Deferred #2 (still). The composed proofs
  pair the glyph with a bg/box whose dirty rect covers it; a text-ONLY frame's glyph extents into the dirty path
  is the remaining add.
- **(d) Texture Egress + the device/world spawn path + the author-gate tooling** — the next epics, out of 023. The
  composition root produces a Surface holding the CPU bitmap; the GPU copy (Egress) and who calls `Register`
  (the spawn path) are future.
- **(e) The slice-1 vendor-lib patch** — `patches/blend2d-assign-data-underalloc.patch` + the git-apply-inside-
  the-vendor-step wiring (the 022 `assign_data` upstream-bug fix) remains the tracked follow-on, sequenced by
  tcdent. 023 ships on the bypass (the external-data form), which never calls the buggy path.
