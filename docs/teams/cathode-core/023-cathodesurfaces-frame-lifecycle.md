# Sprint 023 — The integration phase: the composition-root module drives the full pipeline to a Surface

Stand up the dedicated **composition-root module** (browse-design ruled B, 2026-06-21) that backs
CathodeSurfaces' neutral seams with the real engines and drives the per-frame tick through the **full
pipeline** — DOM → Layout → Text → Raster → Surface — landing the deferred integration glue (the
`ITextMeasurer` adapter [021], the `FGlyphRun`→`FPositionedGlyphRun` mapper [022], the A2 production resolver
wiring [019/020]). Prove: **a frame tick produces a Surface holding the rasterized bitmap.** The engines wired
into one driven pipeline.

> Scope from the PM (tcdent-approved): "CathodeSurfaces — bitmap holder and frame lifecycle. Wire the
> per-frame tick that drives the full pipeline: Layout->paint-op->raster->Surface, including the ITextMeasurer
> adapter (Text+Layout), the glyph-run->FPositionedGlyphRun map, and the A2 production resolver wiring. Prove:
> a frame tick produces a Surface holding the rasterized bitmap."

## Scope Boundary

**This sprint owns** the dedicated composition-root module (working name `CathodeComposition` — dev confirms
the final name at slice 1, per the `CathodeX` convention) that:
- deps **every engine** (Surfaces + Layout + Rasterizer + Text + Resolver) **and** CathodeDom; has **NO
  dependents** (a composition root at the top of the dep graph);
- backs `ISurfaceDocument` with the real Layout and `ISurfaceRenderTarget` with the real Rasterizer (the seam
  impls **injected** into `FSurface` — it holds the handles; the composition root provides them);
- owns the `FLayoutPaintOp`→`FPaintOp` mapper (resolve images + **shape text**), the `ITextMeasurer` adapter,
  the glyph-run map, and the per-frame tick drive;
- proves a frame tick produces a Surface holding the rasterized bitmap.

**The composition home — RESOLVED (browse-design ruled B, components.md:136):** the cross-engine glue lives in
this dedicated composition-root module, **NOT** bolted onto CathodeDom. **Dep-confinement is the decisive
ground** — extending CathodeDom would leak the render engines (Blend2D/HarfBuzz via Rasterizer+Text)
transitively up to everything depping CathodeDom, including CathodeVerify; the composition root (no
dependents) confines the all-engines coupling to one site. **Every engine AND CathodeSurfaces AND CathodeDom
stay neutral leaves** (the 021/022 leaf-preservation invariant, extended to the composition layer).

**Premise (the 015–022 discipline — dev grounded + reviewer independently confirmed, citations):** the engine
integrations are **complete** (016–022). CathodeSurfaces is **built** — `FSurface` (the handles
`ISurfaceDocument*`/`ISurfaceRenderTarget*`) + the registry + the readiness relay + the per-frame **tick-pump**
(`FSurface::RenderAndRelay`: Document→RenderFrame → Target→ProduceFrame → on change, relay GetReadyFrame) —
all **stub-tested** (`FRecordingDocument`/`FFixedDocument`/`FScriptedTarget`), pure-bespoke Core-only. The
**gap** is NOT "no tick" — it's that the tick drives **stub seams**: nothing backs `ISurfaceDocument`/
`ISurfaceRenderTarget` with the real engines, the composition root doesn't exist, and the mapper + glue are
unwired.

**Out of scope (named, resolved):**
- **Texture Egress** (`FSurfaceReadinessSeam` → `RHIUpdateTexture2D` → the UTexture2D) — a separate sprint;
  023 produces the CPU-bitmap Surface (the readiness relay fires; the GPU copy is Egress).
- **The device/world spawn path** (who calls `Register`) — integration / later.
- **No leaf carries composition glue / no engine or CathodeSurfaces or CathodeDom deps the engines** (the
  ruling — the glue lives in the composition root; the providers stay neutral).
- **No re-proof** of the engines (016–022) or CathodeSurfaces' bespoke tick-pump/registry/relay (stub-tested).

## Problem

CathodeSurfaces' tick-pump drives stub seams; nothing backs them with the real engines, no composition root
binds the pipeline, and the mapper/adapter/glue are unwired. Two grounded seam-mismatches: (1)
`FSurfaceRasterizer` is a **private** header (CathodeRasterizer/Private) — an out-of-module backer can't
construct it, so it needs a minimal **Public promotion** (the Rasterizer's own type; no new dep; stays leaf);
(2) the **paint-op type gap (the heart)** — Layout emits `FLayoutPaintOp` (un-shaped `FString Text` +
`FLayoutFontRef`; the seam comment: "DrawText — un-shaped run (integration shapes via 004)", CathodeLayout.h:106)
while the Rasterizer consumes `FPaintOp` (a **shaped** `FPositionedGlyphRun`), so the mapper must resolve
images **and shape text** — the by-design integration work where the 022 glyph-map glue lives.

## Goal

A per-frame tick drives the full pipeline on a real document: the composition root backs `ISurfaceDocument`
(real Layout) + `ISurfaceRenderTarget` (real Rasterizer), `FSurface::RenderAndRelay` drives DOM→Layout→
(Text-shaped)→Raster→Surface → the ready front-buffer holds the **rasterized bitmap** — proven the bitmap
reflects the document's **Layout-computed** render (content/glyph ink at its laid-out position, blank
elsewhere; **wrong-position → RED**), driven through the **full composed tick** (not 022's isolated DrawGlyphRun
/ a direct raster / a pre-baked bitmap).

## In Scope (ordered slices — dev+reviewer-converged)

1. **The composition root + the core non-text tick.** Create the composition-root module (deps all engines +
   CathodeDom, no dependents); promote `FSurfaceRasterizer` Private→Public (minimal); back `ISurfaceDocument`
   (real Layout) + `ISurfaceRenderTarget` (real Rasterizer, capturing the push `FFrameReadySeam`'s
   `FBitmapData`); wire the `ITextMeasurer` adapter (**Layout-construction prereq** —
   `CreateLiteHtmlLayoutEngine(ITextMeasurer&)` requires it even for a no-text doc); the non-text mapper
   (fill/box/image). *Proof:* a known **inline non-text** document (a div with a known bg color) → one
   `RenderAndRelay` tick → resolve the opaque `Buffer.Handle` → `FBitmapData`/`GetData` → the color at its
   **Layout-computed box** AND blank elsewhere (a no-render → blank → RED; wrong-position → RED). The adapter
   is **wired but not discriminatingly exercised** here (its text proof is slice 2). Win64-content-gated.
2. **The text path (the glyph-run map = the mapper's text case).** The mapper shapes `FLayoutPaintOp` text
   (→ the real shaper → `FGlyphRun` → the 022 map → `FPositionedGlyphRun` → `DrawGlyphRun`). *Proof:* a text
   document → the tick → glyph ink at the **laid-out text position** (the 022 EXISTS-ink/ALL-blank, now
   through the full composed tick — discriminatingly exercising the adapter + the map).
3. **The A2 production resolver wiring (separable; the tooling gate).** The production `SetResourceResolver` +
   the default-root + the `DirectoriesToAlwaysStageAsUFS=/Game/Cathode` staging (none exists today — a tcdent
   tooling gate). *Proof:* a linked-resource document resolves through the real resolver into the rendered
   Surface. Depends on slice 1 (not slice 2). **Escalate the tooling gate to tcdent when sequenced.**

## Non-Goals

- No Texture Egress / GPU copy / UTexture2D; no device-spawn path.
- No engine / CathodeSurfaces / CathodeDom dep on the engines (the glue lives in the composition root).
- No re-proof of 016–022 or the bespoke tick-pump; no hand-assert of vendor pixels (the render relation, the
  022 tautology-guard analog at the Surface level).
- No `.uproject`/staging change in slices 1–2 (A2's gate is slice 3 only — flag if it creeps in).

## Starting Surface (the audit the dev produced)

`the composition root (deps all engines + CathodeDom, no dependents) -> back ISurfaceDocument (Layout's
ILayoutEngine::Relayout -> FLayoutResult.PaintOps) + ISurfaceRenderTarget (Rasterizer's FSurfaceRasterizer
[Private->Public] -> capture the FFrameReadySeam FBitmapData) -> the FLayoutPaintOp->FPaintOp mapper (resolve
images + shape text) -> FSurface::RenderAndRelay tick -> GetReadyFrame -> the opaque Buffer.Handle ->
FBitmapData/GetData (the proof, in the composition module's test -- only it can resolve the opaque handle,
Surfaces is pixel-blind)`. Audit targets: `CathodeSurfaces.h`/`CathodeSurface.cpp` (FSurface, the seams,
RenderAndRelay), `CathodeLayout.h` (ILayoutEngine, CreateLiteHtmlLayoutEngine(ITextMeasurer&), FLayoutPaintOp),
`CathodeRasterizer` (FSurfaceRasterizer [promote], FPaintOp, the bitmap), `CathodeText` (the shaper, FGlyphRun),
`CathodeResolver` (CreateUEFileBundleSource, SetResourceResolver), the leaf-preservation precedent (021/022),
components.md:136 (the composition-root ruling). COVERAGE.md (the seam-backing deferral retired) + the A2
tooling-gate.

## Method

Stand up the composition root; back the seams (real Layout/Rasterizer, injected); wire the adapter + the
mapper (shaping text); drive one tick; prove the document-reflecting render through the **full composed tick**
(a known document → the tick → the front-buffer holds the render at the Layout-computed position; a no-render/
wrong-position → RED). Reuse the engine proofs (don't re-prove 016–022). **Win64 split:** the tick **structure**
(wiring compiles, seam-backing, RenderAndRelay-returns-true-on-change, relay-fires) is **cross-platform** and
asserts on any platform; only the pixel-**content** proof is Win64-gated (`#if PLATFORM_WINDOWS`; the `#else`
AddErrors; a marker only the executed render emits — the 021/022 discipline, no vacuous `#if`'d-out green). The
A2 resolver (slice 3) pulls the `.uproject`/staging tooling gate — escalate to tcdent when sequenced; slices
1–2 use inline content (no resolver). Run the FULL Cathode suite green (a cross-module composition perturbs
many — gate all touched modules). Coherence + the authoritative-run discipline (the cross-team red-module /
crash→survive-if-a-render-crash cases as they apply).

## Acceptance (reviewer-formulated)

- **The composition root + the seams backed:** the dedicated module (deps all engines + CathodeDom, no
  dependents) backs `ISurfaceDocument` (real Layout) + `ISurfaceRenderTarget` (real Rasterizer) — injected
  into `FSurface`; the engines + CathodeSurfaces + CathodeDom stay neutral leaves (grep-clean of cross-engine
  deps).
- **A frame tick produces a Surface holding the rasterized bitmap (the full composed tick):** a known document
  → one `RenderAndRelay` → the ready front-buffer holds the render reflecting the document's **Layout-computed**
  geometry (color/glyph ink at its laid-out position, blank elsewhere) — driven through FSurface → real Layout
  → real Rasterizer → GetReadyFrame, NOT a direct raster / 022 in isolation / a pre-baked bitmap. A no-render
  → RED; wrong-position → RED.
- **Slice 1** (core non-text): the colored box at its Layout box; the adapter wired (Layout-ctor prereq) but
  not text-exercised. **Slice 2** (text): glyph ink at the laid-out text position (the adapter + map
  discriminatingly exercised). **Slice 3** (A2): a linked resource resolves through the real resolver (the
  tooling gate escalated).
- **The 2 seam-mismatches:** `FSurfaceRasterizer` Private→Public (minimal, leaf-clean); the mapper shapes text
  (the by-design gap).
- **No regression:** the full Cathode suite green; the engine + bespoke (registry/relay/lifecycle) tests stay.
- **Coherence:** the tick proof bites a no-render/stub-backed seam (blank → RED); the touched TUs recompiled;
  Win64-content-executed (the structure cross-platform); the authoritative run.
- **COVERAGE updated** at close — the seam-backing deferral retired; the composition root recorded; the A2/
  Egress/spawn-path boundaries.

## Review Questions (resolved)

1. **The composition home** — RESOLVED (browse-design ruled B): the dedicated composition-root module (deps
   all engines + CathodeDom, no dependents); the engines + Surfaces + CathodeDom stay neutral. Dep-confinement
   the decisive ground.
2. **The A2 resolver** — RESOLVED: separable (the inline basic tick needs no resolver); slice 3, off the core
   path; the `.uproject`/staging tooling gate escalated to tcdent when sequenced.
3. **The slicing** — RESOLVED: slice 1 (core tick + the adapter as a Layout-ctor prereq + the non-text mapper)
   / slice 2 (the text glyph-map) / slice 3 (A2). The adapter (Layout-measurement) + the map (raster-shaping)
   are distinct glue, split 1/2.
4. **The proof** — RESOLVED: the full composed tick, document-reflecting at the Layout-computed position
   (wrong-position → RED); the bitmap relation, not vendor pixels; Win64-content-gated.
5. **Texture Egress / the spawn path** — RESOLVED: out (separate sprints).

## Outcome

023 completed the **integration phase**: a dedicated `CathodeComposition` module composes the seven engine
leaves — Dom / Resolver / Layout / Text / Rasterizer / Surfaces — into **one driven frame tick** that renders
an HTML/CSS document (box fill, text, and linked resources) to a real Blend2D bitmap. The composition root is
the **sole cross-engine dep site** (deps all engines + CathodeDom, no dependents); every engine + Surfaces +
CathodeDom stay **neutral leaves**. It backs `ISurfaceDocument` (`FLayoutSurfaceDocument` = real Layout + the
production resolver) and `ISurfaceRenderTarget` (the real Rasterizer via the `ISurfaceProducer` /
`CreateSurfaceRasterizer` factory + the `FLayoutPaintOp`→`FPaintOp` mapper), injects them into `FSurface`, and
drives the tick through the exported `ISurfaceRegistry::DriveAll`. The `ITextMeasurer` adapter threads **one
`FFontHandle`** through measure + shape + raster.

**Slices & commits:** slice 1 `bfd5809` — the composition root + the first composed frame (the div at its
Layout-computed box); slice 2 `d4380da` — the text path (measure + shape + raster over one font, the
adapter-vs-shaper two-color); slice 3 `03abf50` — the A2 production resolver (a linked-CSS `@import` resolves
through the production caller over `/Game/Cathode/` into the rendered bitmap). This close adds the three
COVERAGE records + the retrospectives.

**The proof (reviewer-accepted, on-box, 119/119):** each slice the tautology-free **content discriminator** —
the color / glyph ink / linked green at its *Layout-computed* position (read from the layout result, not
hardcoded), blank elsewhere, with the null-resolver control (s3) and the measured-width-observable two-color
(s2). Win64-content-executed (the structure cross-platform and explicitly secondary); freshness by the
**additive marker** through a live mtime race, never "build done."

**Two integration-found bugs, each fixed at the right layer:** the uninitialized surface buffer (the
dirty-region model assumed a defined initial buffer creation never gave — root-caused in-tree by the
non-determinism tell, fixed at the buffer *owner*, `FBlend2DBitmap::Create` clear-once-on-creation); the
top-left↔baseline convention mismatch (clipped glyphs — bridged in the *glue*, the mapper's `+Ascent`, Layout
and the Rasterizer each left correct in its own convention). The integration found the defects; the fixes
landed at the owner/glue layer, never by weakening an assertion.

**The 019 coupling come due (s3):** the production default-root (`ProjectContentDir()/Cathode`) == the
`+DirectoriesToAlwaysStageAsUFS=/Game/Cathode` staging entry (in-tree dispositive, tcdent-approved); the
cooked-pak staging is named **corroborated-inference** — the empirical packaged read is a deferred cook pass
(env's domain), not overclaimed. **The epic's engine integrations are complete** (016 real JS → 017 classes →
018 DOM → 019 resource loading → 020 litehtml on the real Dom+Resolver → 021 real HarfBuzz shaping → 022 real
Blend2D glyph raster → **023 the composed pipeline**) — an HTML/CSS document is now a real bitmap, end to end.

**Residuals the next epic inherits:** (a) the deferred packaged-build cook pass (env's empirical UFS-staging
validation); (b) the face-name→`FFontHandle` font catalog (023 used the single default OFL font); (c) the
glyph-only-frame dirty (Rasterizer Deferred #2); (d) Texture Egress + the device/world spawn path + the
author-gate tooling; (e) the slice-1 vendor-lib `assign_data` patch (the 022 follow-on, sequenced by tcdent).

## Lead Retrospective — uptime.browse-lead

The integration phase's value was routing the loop's discipline through **two integration-found bugs** and a
string of build-infra gates without letting either bend the evidence bar. What the loop got right: each bug was
**owned, not worked around** — the dev surfaced the uninitialized buffer and the baseline mismatch as forks
rather than band-aiding, and I ruled each to the correct *layer* (the buffer owner; the glue) as a correctness
call, not a taste fork, with the reviewer independently re-deriving the cause *in-tree* (dispositive — browse's
own code, not the 022-style vendor inference). The fix-**location** refinement (`FBlend2DBitmap::Create` over
the ruled `FDoubleBuffer`) was a grounded mid-build correction I **confirmed, not re-litigated** — the dev hit a
real recording-backend test-churn wrinkle and solved it at the actual pixel owner, preserving the agreed
invariant; only the file moved. The **discriminator sharpened at each slice** (structure-vacuous-vs-Win64-content
→ the adapter-vs-shaper measured-width-observable → the default-root↔staging match + the cook-staging honesty),
and the reviewer's pre-stage sharpenings were the load-bearing ones — I relayed them as *build-shaping
requirements*, not after-the-fact gates, so the proof documents were built to bite the right risk. The **A2
tooling gate** was escalated to tcdent *before* framing slice 3 (the `.uproject` staging is his approval), with a
clean defer option, never surfaced mid-build. The **build-infra gates** — the hung wrapper, the mtime race
(twice), the recurring desktop-session lapse — stayed env's/tcdent's domain: no box action taken, the recurrence
flagged as a *durable-keep-alive pattern* rather than re-flagged each time. And the scoped-commit verification
earned its keep again — the reviewer caught cross-team WIP (Makefile / setup.ps1 / UptimeWorld / patches) sitting
in the working tree; the selective stage excluded it, where a `git add -A` would have swept it into the close.

## Review Retrospective — uptime.browse-review

(Appended via agent-message; folded for the record.) The integration phase's discriminator sharpened at each
level, and at each the same trap recurred: **the structure is vacuous; the content is the proof.** Slice 1 — a
stub-backed seam *also* makes `RenderAndRelay` return true and the relay fire, so those structural asserts bite
nothing; only the **Win64 content** (the div's color at its *Layout-computed* offset box, read from the layout
result not hardcoded, blank elsewhere) bites a no-render / direct-raster / stub. Slice 2 — the **adapter-vs-shaper
measured-width-observable** was the place a vacuous green would have slipped: a single fixed-origin glyph exercises
the *shaper* (per-glyph ink) but is *width-independent*, so it never bites the *adapter* (`MeasureWidth`); the
two-color shrink-to-fit document (a BLUE bg sized BY the measured width, separate from the RED glyphs) makes the
measurement observable in the bitmap, so a stub-measurer REDs the blue independently of the glyphs. Slice 3 — the
**default-root↔staging literal match** (`GetDefaultResourceRoot()` == `+DirectoriesToAlwaysStageAsUFS`) is the
in-tree *dispositive* gate for the 019 coupling come due ("don't ship a root the cook can't stage"), and the
**cook-staging method honesty** held the line: the wiring/resolve/render/match are empirical, the cooked-pak
staging is *corroborated-inference* (the editor reads on-disk; the packaged read is a deferred cook pass), named
as such, not overclaimed.

Two cross-cutting threads. **Freshness through a live mtime race**: each slice was proven by the *additive marker*
(the new test present + its `[WIN64-EXECUTED]` content marker), never "build done" — and slice 1 added the
*garbage→deterministic-0 behavior-flip* (a stale pre-fix binary would re-emit the uninitialized 34→635, the clean
0 proves the fix is in the binary). **Two integration bugs, each fixed at the right layer**: the uninitialized
buffer root-caused *directly in-tree* (browse's own `FBlend2DBitmap::Create`, dispositive — not the 022-style
vendor corroborated-inference) and fixed at the buffer *owner* (clear-once-on-creation, never per-frame, so the
dirty-region accumulation survives); the top-left↔baseline mismatch bridged in the *glue* (`CathodeComposition`'s
mapper, `+Ascent`) with Layout and the Rasterizer each left correct in its own convention. The integration found
the defects and the fixes landed at the owner/glue layer, never by weakening an assertion. Closing note: a
`git add -A` would have swept cross-team WIP (Makefile/setup.ps1/UptimeWorld) into the slice-3 commit — the
selective-hunk manifest mattered. **Residuals the next sprint inherits:** the deferred packaged-build cook pass
(env's empirical validation of the UFS staging), the font catalog (face-name→font; 023 used the single default
OFL font), and Texture Egress + the device/world spawn path (out of 023's scope).

## Dev Retrospective — Mundo (uptime.browse-dev)

The plan held; the surprises were two real bugs the *composed* render exposed that the unit-tested engines never
exercised — and the lesson is that the integration phase is exactly where those unexercised assumptions surface.

- **The integration finds the bugs; the fix lands at the owner/glue layer.** Slice 1: the dirty-region rasterizer
  ACCUMULATES, so it assumes a defined initial buffer — but `BLImage::create` doesn't zero, and the first composed
  render showed garbage. The tell was **non-determinism across builds with identical render code** (ink-outside
  34→635) — the hallmark of uninitialized memory, not a render bug. Fix at the buffer owner (`FBlend2DBitmap::Create`
  clear_all), once on creation (never per-frame, which would defeat the accumulation). Slice 2: glyphs rendered
  *nowhere* (clipped) — `DrawText.Position` is the text-box top-left (litehtml), `DrawGlyphRun`'s origin is the
  baseline (Blend2D); the mapper bridges (`+Ascent`), each engine left correct in its own convention.
- **Diagnose, don't guess-fix.** Each failure I added targeted diagnostics and ran a diag pass before touching the
  fix: the stray-ink samples (s1, garbage vs a clean fill), the DrawText color/shaping/in-box-non-blue (s2, which
  pinned color-fine/shaping-fine/baseline-wrong), the green-with/without-resolver (s3). The diagnostics turned each
  "it's red" into "it's *this* line."
- **The fix location is its own decision.** The lead/reviewer ruled "FDoubleBuffer clears," but implementing it
  there routes through the recorded dispatch API and breaks the rasterizer's recording-backend tests
  (`FrameTests` asserts `DispatchedCalls[0]==Clip`; the GetData `this`-sentinel makes a memzero corrupt). I placed
  the clear at the real BLImage owner (`FBlend2DBitmap::Create`) — same invariant, zero test churn — and surfaced
  the deviation + rationale rather than silently changing it.
- **The null control needs a frame.** Slice 3's null-resolver document was an empty div → zero paint ops → no
  frame relayed → my `-1` sentinel, not a clean 0. A baseline inline box made a frame relay in both surfaces, so
  the discriminator (green=1600 vs 0) is a real 0, not an error code.
- **Build-infra discipline:** the box hit a hung wrapper, the shared-tree mtime race (twice), and a *recurring*
  desktop-session lapse. The authoritative marker was always the **relink mtime advance** (+ the additive
  content marker), never "build done"; box actions stayed tcdent's; the durable keep-alive was escalated, not
  worked around.
- **My own miss (future-shaped):** on the composition-home fork I oscillated B→A→B before settling that both were
  constraint-clean and it was a pure architecture-taste call (design's, not mine). The lesson: on a taste call,
  state the stable facts and surface the neutral fork — don't manufacture a confident lean.

**Next sprint inherits:** the deferred packaged-build cook pass (the UFS-staging empirical validation, env's),
the face-name→font catalog, the glyph-only-frame dirty (Rasterizer Deferred #2), Texture Egress + the device-spawn
path, and the slice-1 vendor-lib `assign_data` patch (the 022 follow-on, sequenced by tcdent).
