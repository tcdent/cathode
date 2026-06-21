# Cathode Rasterizer — component coverage & accountable deferrals

Durable accountability record for the Rasterizer (sprint 003, no-build/no-run/no-pixels scaffold). It
maps every responsibility in [`docs/browse/components-rasterizer.md`](../../../../../docs/browse/components-rasterizer.md)
to its status: **COVERED** (slice + mechanism) or **DEFERRED** (reason · dependency · what unblocks it ·
first-execution risk). Nothing in the component doc is silently missing.

This file is the accountability of record — code comments that mention an "accountable gap" point here.

## Covered

| Responsibility | Slice | Mechanism |
|---|---|---|
| Paint backend — backgrounds / solid boxes | S4 dispatch + S5 impl | `FillRect` / `FillBox` → `BLContext::fill_rect`/`fill_box` |
| Paint backend — images | S4 + S5 | `DrawImage` → `BLContext::blit_image` |
| Paint backend — **borders (solid rectangular)** | S4 + S5 | Layout composes them from `FillBox`/`FillRect` edge rects; the Rasterizer provides the fill primitives, Layout owns CSS per-side border semantics. (Styled/rounded → deferred below.) |
| Draw shaped text — **dispatch + fractional positioning** | S4 | `DrawGlyphRun` routed by `DispatchPaintOps`; origins passed unsnapped per `bFractionalGlyphPositioning`. |
| Draw shaped text — **vendor glyph fill (real pixels)** | 022 | `DrawGlyphRun` maps the neutral `FPositionedGlyphRun` (bytes+ids+positions) → `BLFont` (`createFromData` external-data; the face cache owns its byte copy) + `BLGlyphRun` → real `BLContext::fill_glyph_run` → real pixels. Proven by `Cathode.Rasterizer.Glyph.RealRasterPixels` (ink-in-box=637 / margin-ink=0 on 021's real shaped 'M', one `FFontHandle`); leaf-preserved (bytes injected via the seam); Win64-executed; the box survived (the `assign_data` overflow bypassed). |
| Rasterize **dirty regions only** | S5 | `FSurfaceRasterizer::RasterizeFrame` clips to each dirty rect (`ClipToRect` → `BLContext::clip_to_rect`) and redraws only within it — never the whole surface |
| Produce the CPU bitmap | S2 / S5 | `FDoubleBuffer` front buffer → `FBitmapData` via `IRasterBitmap::GetData` |
| Hand bitmap + dirty-rect to Surfaces | S5 | `FFrameReadySeam` output seam (Core-only); produced + handed off, frame-ready-only-on-change |
| Bitmap pixel format matched to egress | S2 | DECIDED `PremultipliedBGRA8` sRGB (`DefaultSurfaceFormat`; RQ2 closed) — lines up with `UTexture2D` sRGB=true + unlit/emissive |
| Dirty-region granularity | S3 | DECIDED `PerRect` default; config knob `FRasterConfig::DirtyRegionGranularity` |
| Does not touch the GPU | by design | hands off a CPU bitmap; Texture Egress owns the upload |

## Deferred — with accountability

### 1. Vendor glyph rasterization (`fill_glyph_run`) — RETIRED (022)
- **Retired by:** sprint 022. `FBlend2DContext::DrawGlyphRun` maps the grown neutral `FPositionedGlyphRun` (bytes + ids + positions) → `BLFont` (`createFromData` external-data ownership form — the face cache owns its byte copy via a destroy callback) + `BLGlyphRun` → real `BLContext::fill_glyph_run` → real pixels.
- **Proof:** `Cathode.Rasterizer.Glyph.RealRasterPixels` — the EXISTS-ink-in-box (637) + ALL-blank-margin (0) pair on 021's real shaped 'M' (one `FFontHandle`; the same bytes shaped and drawn); real pixel-read via the Blend2D bitmap `GetData`; leaf-preserved (no production CathodeText dep — bytes injected via the seam); Win64-executed; the box survived.
- **Vendor-lib follow-on (tracked, separate, non-blocking):** 022 shipped on the **bypass** (the external-data form, which never calls the buggy path) over the **unpatched** Blend2D lib. The root cause — `bl_array_assign_data` under-allocates (sizes the new impl from the array's old size, not the new count → a heap-buffer-overflow; asan-grounded off-box, live on upstream master 0.21.2 / `6dbc2ce`) — is fixed by `patches/blend2d-assign-data-underalloc.patch`, applied via a `git apply` hook **inside** the `setup.ps1` vendor step (after `install.sh` checkout, before the blend2d build; there is no build-without-vendor variant). Sequenced by tcdent.
- **Layout drive stays integration:** 022 proves the FILL on 021's real shaper output; the Layout → paint-op → raster production drive remains integration (Deferred #5).

### 2. Glyph-only frames do not dirty / do not render
- **Reason:** glyph paint ops carry no bounds (`FPaintOp::Rect` is empty for `DrawGlyphRun`), so a frame containing only glyph ops produces an empty dirty region and `RasterizeFrame` returns without rendering.
- **Dependency:** Text & Fonts supplies glyph-run extents (bounds) so glyph draws can mark their dirty area.
- **Unblocks:** when Text & Fonts provides glyph extents.
- **022 note (still deferred):** 022's proof is a DIRECT `DrawGlyphRun` → bitmap, bypassing the frame/dirty path — so this remains deferred. Wiring glyph-run extents into the dirty/frame path is the integration add (the real `fill_glyph_run` that exercises it now exists).
- **First-execution risk:** a text-only change (no box/image op in the same frame) does not re-rasterize or hand off a frame.

### 3. Styled / rounded borders
- **Reason:** the op-set has only solid fill primitives (`FillRect`/`FillBox`) — no stroke / rounded-rect / path op.
- **Dependency:** a stroke / rounded-rect / path op added to the boundary (`IRasterContext`).
- **Unblocks:** when Layout & Style needs CSS border styles / `border-radius`.
- **First-execution risk:** only solid rectangular borders (fill-composed) render; rounded or styled borders render square or missing until the op is added.

### 4. 4K AA fidelity proof
- **Reason:** visual quality cannot be asserted without a render; no pixels this sprint.
- **Dependency:** an execution-sprint render + human eyes + the human's fidelity mitigation plan.
- **Unblocks:** the execution-approval sprint.
- **First-execution risk:** AA / quality at 4K may need tuning; the `FRasterConfig` defaults (grayscale analytic AA, no supersample, sRGB) are decided starting points, not proven values.

### 5. Live cross-module producer wiring
- **Reason:** who *calls* `RasterizeFrame`, who *feeds* `FPaintOp`, and who *consumes* `FFrameReadySeam` are future components; this sprint keeps the module Core-only (no CathodeRuntime coupling) by directive.
- **Dependency:** Runtime Core's 002 phase-5 relayout seam (caller), Layout & Style (`FPaintOp` source), Surfaces / Texture Egress (`FFrameReadySeam` consumer) — and an integration sprint.
- **Unblocks:** the integration sprint.
- **First-execution risk:** nothing rasterizes or uploads until the seams are wired across modules. (All three seams exist and are generalizable now; only the cross-module wiring is future.)
