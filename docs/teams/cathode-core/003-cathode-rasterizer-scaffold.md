# 003 — Cathode Rasterizer scaffold

## Nature

003 authors the **Rasterizer** component (see `docs/browse/components-rasterizer.md`) — the paint
backend that turns the layout engine's draw calls and Text & Fonts' glyph runs into pixels in a CPU
bitmap, on the worker thread, off the game's render path. It is where the frame is produced.

It is still a **no-build, no-execution sprint** (the human's standing constraint until execution is
approved). 003 authors the component as proper C++ — the Blend2D swap boundary, the bespoke
orchestration (bitmap/buffer bookkeeping, dirty-region tracking, draw-dispatch), and the tests for
that bespoke logic — but **nothing is compiled or run**. Behavioral proof, build/log coherence, and
**all visual proof** are inherited by the execution-approval sprint.

The **whole Rasterizer is in scope** — one sprint, same pattern as 002 (Runtime Core). The human:

> "Rasterizer is in scope for this sprint as described in the document."

## Scope Boundary

**This sprint owns:** our internal **Blend2D swap boundary** (the bitmap/surface + the draw session,
in our types, with Blend2D confined behind the impl), the **CPU bitmap** + pixel-format handling +
double-buffering, **dirty-region tracking**, the **draw-dispatch** (paint op → backend call), the
**generalizable input/output seams** (draw calls in, glyph runs in, bitmap+dirty-rect out), the
**knob inventory + a config struct** for tunables, and the UE Automation specs for the bespoke logic.

**This sprint does NOT own** (named, with the owner):
- **Any compile, link, build, run, or visual proof** — *execution-approval sprint*. Tests authored,
  not run; fidelity proven only when rendered + reviewed.
- **Visual fidelity / 4K AA quality tuning** — a real de-risk with a **later mitigation plan** (the
  human's). 003 keeps the fidelity *decisions out of code and in the config as tunables*; it does
  not pick or prove them.
- **Glyph shaping + hinting** — *Text & Fonts*. The Rasterizer anti-aliases the coverage
  (`BLRenderingQuality`); it does **not** own hinting (upstream). No hinting knob is manufactured here.
- **litehtml integration / the draw-call source** — *Layout & Style* (its own confined vendor).
  Layout adapts litehtml's `document_container` → our paint ops; the Rasterizer never speaks
  litehtml's API.
- **GPU upload** — *Texture Egress*. The Rasterizer hands off a CPU bitmap + dirty rect; it does not
  touch the GPU.
- **Building/vendoring Blend2D + AsmJit** (staged in 001; binaries are the execution sprint).

## Problem

The Rasterizer does not exist. Runtime Core (002) sequences a phase-5 relayout/raster delegation
seam but nothing services it. There is no Blend2D boundary, no CPU bitmap, no dirty-region tracking,
no draw-dispatch — so there is nothing to produce a frame and nothing testable, and no swap boundary
isolating Blend2D.

## Goal

Finishing 003 looks like: the Rasterizer authored as a proper subsystem — Blend2D (JIT via AsmJit)
confined behind our own swap boundary at the **right altitude** (the handful of paint ops we need,
not the full Blend2D API, not a `BL*` re-export), the bespoke orchestration (bitmap/buffer
bookkeeping, dirty-region tracking, draw-dispatch) implemented and tested via a recording backend,
the **generalizable** input/output seams defined as real-but-evolvable integration points, and the
**knob inventory + config struct** surfaced for the human's later tuning research — all without a
build, a run, or any pixel asserted.

The seam philosophy, verbatim:

> "Define the interface for the input and output seams. If you don't have input and output seams,
> don't pre-optimize and create shapes you don't know the shape of. We'll expect a contractual
> interface as we add those systems in, but since you don't know the shape of those systems, we'll
> keep it generalizable and allow it to evolve as the system evolves."

The knob/tuning philosophy, verbatim:

> "Surface any flags or parameters you come across that we should think about as defaults, and we can
> launch additional research to find the same set of defaults and a great way to store them if there
> are any knobs we want to keep around for tuning."

## Final State

1. A **Blend2D swap boundary** in our types: a bitmap/surface (wrapping `BLImage` create + mutable
   pixel ptr/stride/format) and a draw session (begin → primitive ops: fill rect, fill box/border,
   draw image, draw glyph run → end, wrapping `BLContext`). `BL*` (and AsmJit) appear only in the
   impl; the vendor is swappable behind it. Op-set maps to what Layout/Text feed, not the full API.
2. The **CPU bitmap** + **pixel-format handling** (stride/size math, the format handed to egress) +
   **double-buffering** (front/back, swap on frame-ready, draw targets the back buffer).
3. **Dirty-region tracking** (coalesce/clamp/union/merge; mark → redraw set; empty-after-flush) +
   the granularity tunable.
4. The **draw-dispatch**: a paint op routes to the corresponding backend call with our-computed
   geometry/params — exercisable via a **recording backend** without rendering.
5. **Generalizable input/output seams**: draw-call ops in, glyph runs in (our evolvable
   positioned-glyph type), CPU bitmap + dirty rect out to (future) Surfaces — real integration
   points, minimal/evolvable shape, no future-taxonomy over-specification.
6. A **knob inventory** (knob → owner: Blend2D-config vs our-bespoke → tunable? → applies-where →
   `DEFAULT: TBD-human`) and a **C++ config struct** holding them with explicit TBD defaults, read at
   context creation. No baked values.
7. **UE Automation specs** for the bespoke logic (dirty-region math, buffer bookkeeping,
   draw-dispatch routing, pixel-format plumbing) — assert our bookkeeping via a recording backend,
   **never pixels**, not run.

## In Scope — ordered slices

Each slice follows the Method; fed only after the prior meets its evidence. The dev owns internal
design (naming/types/structure); the contract fixes responsibility + the testability discipline.

### Slice 1 — Starting Surface audit + the Blend2D swap-boundary contract
Audit (read-only) the staged Blend2D API surface we need (bitmap create + mutable data; context
begin/fill/end), AsmJit's role, and the 002 phase-5 relayout seam this will eventually service. Then
design our **swap boundary** (bitmap/surface + draw session, our types, `BL*` confined) at the right
altitude. Begin the **knob inventory**. Output: the interface + altitude rationale + the initial knob
inventory (in the inventory shape above).

### Slice 2 — CPU bitmap, pixel format, double-buffering
The bitmap buffer behind the boundary, pixel-format handling (stride/size, the egress format), and
double-buffering. **Bespoke/testable:** buffer bookkeeping (swap toggles active, draw targets back,
frame-ready only on real change, dims = surface), format/stride math. Pixel format is a TBD knob.

### Slice 3 — Dirty-region tracking
The dirty-rect management. **Bespoke/testable:** dirty-region math (coalesce/clamp/union/merge,
mark→set-contains, empty-after-flush). Granularity is a TBD knob.

### Slice 4 — Paint backend + draw-dispatch + input seams
The draw session servicing the primitive ops via the boundary, the dispatch routing, and the
**generalizable input seams** (draw-call ops in, glyph runs in — our evolvable types). AA/quality
applied as **config**, never hardcoded in draw calls. **Bespoke/testable:** dispatch routing (op →
backend call + our-computed geometry) via a recording backend — never the pixels.

### Slice 5 — Output seam + config wiring + knob inventory finalized
The CPU bitmap + dirty-rect **output seam** to (future) Surfaces; the config struct read at context
creation; the knob inventory finalized for the human's research. **Bespoke/testable:** the handoff
bookkeeping (produced + handed off, frame-ready-only-on-change), seam invocation traced both
directions.

## Non-Goals

- **No build, compile, link, run, or visual proof.** Acceptance never depends on a render.
- **No pixel/color/coverage assertion, no bitmap readback** — pixels are the engine's output;
  fidelity is deferred and never faked with a hand-authored value.
- **No full Blend2D re-export and no recreation** — only the paint ops we need, in our types.
- **Do not test vendor functionality** (Blend2D rasterization, AsmJit JIT) — assume it works.
- **No manufactured fidelity decisions** — AA/quality live in config as TBD tunables, not baked.
- **No hinting knob here** (Text & Fonts owns it); **no litehtml coupling** (Layout owns it).
- **Do not over-specify the seam taxonomy** for systems that do not exist — generalizable, evolvable.
- **Do not delete a fallback just because production does not hit it — trace callers and tests first.**

## Starting Surface

Audit first, producing the inventory before editing:
1. `vendor/lib/cathode/blend2d/` — the paint ops we need (`BLImage` create + `make_mutable`/data;
   `BLContext` begin/`fillRect`/`fillBox`/`blitImage`/`fillGlyphRun`/end; `BLFormat`,
   `BLRenderingQuality`, `BLContextCreateInfo`). `vendor/lib/cathode/asmjit/` — JIT role only.
2. The 002 phase-5 relayout/frame-ready delegation seam (`FSurfaceRelayoutSeam`) this eventually
   feeds — the downstream shape the output seam targets (generalizable).
3. Blend2D config knobs encountered (format, rendering/pattern/gradient quality, context hints,
   `threadCount`, JIT flags) → the knob inventory.

Inventory shapes:
`paint responsibility -> our boundary op -> Blend2D primitive(s) wrapped -> bespoke? (testable y/n)`
`knob -> owner (Blend2D-config | our-bespoke) -> tunable? -> applies-where -> DEFAULT: TBD-human`

## Method

No build/log coherence and **no visual proof** this sprint. Acceptance is **structural conformance +
testability honesty**, proven by reading source:
- **Wrapper altitude:** the boundary exposes the paint ops we need in our types, confines `BL*`/AsmJit
  to the impl, swappable — not a `BL*` re-export, not a Blend2D recreation.
- **Bookkeeping-not-pixels:** specs assert our bookkeeping (dirty-region math, buffer state,
  draw-dispatch routing via a **recording backend**, pixel-format plumbing) and **never the pixels**.
  A literal pixel/color/coverage constant, a bitmap readback, or any assertion that needs Blend2D to
  have actually rasterized = REJECT. Pixels are the engine value; fidelity is captured-from-real-render
  later, never hand-authored.
- **Seam honesty = invocation + real derived data, not taxonomy completeness:** a seam is honest when
  the production paint path actually calls it and routes real/derived data through it (a recording
  impl substitutes the *input* while production dispatch runs); reject a declared-but-never-called
  seam, a parallel path, or an assert-it-exists seam. Do **not** reject a seam for an under-specified,
  evolvable struct on a live call site.
- **Knobs surfaced, not decided:** tunables land in the config struct with explicit TBD defaults; no
  baked fidelity values; correct ownership attribution (AA = ours, hinting = Text).
- **Explicit deferral:** behavioral proof, build/log coherence, and **all visual fidelity proof**
  inherited by the execution-approval sprint — stated as deferred in every verdict.

## Acceptance — the gates that close the sprint

1. **Swap boundary:** the bitmap/surface + draw-session interface exists in our types at the right
   altitude; `BL*`/AsmJit confined to the impl; vendor swappable.
2. **Rasterizer authored:** CPU bitmap + pixel format + double-buffer, dirty-region tracking,
   draw-dispatch, the generalizable input/output seams — all present per Final State, each seam
   traceable to a real production call site (both directions).
3. **Tests authored for bespoke logic:** specs for dirty-region math, buffer bookkeeping,
   draw-dispatch routing, pixel-format plumbing — via a recording backend, asserting our bookkeeping,
   **zero pixel assertions**, vendor not tested.
4. **Knob inventory + config struct:** every surfaced knob attributed (Blend2D-config vs our-bespoke),
   tunable-flagged, `DEFAULT: TBD-human`; no baked fidelity values.
5. **No build/run/visual proof occurred** and none is required to verify 1–4; fidelity explicitly
   deferred to the execution sprint + the human's mitigation plan.

## Review Questions (open forks)

1. **Blend2D internal threading (`BLContextCreateInfo.threadCount`)** — architecture-adjacent: Blend2D
   can spin its own worker threads to parallelize rasterization, which interacts with the 002
   single-dedicated-worker model. Inventoried as a TBD knob this sprint; the human decides
   sync-on-our-worker vs Blend2D-parallel at tuning.
2. **Pixel format (`BL_FORMAT_PRGB32` vs `XRGB32`)** — chosen to match the egress texture format to
   avoid an upload conversion; depends on Texture Egress (future). TBD knob now.
3. **Knob-storage shape** — a documented C++ config struct (runtime rendering config, not
   author-facing) read at context creation, with TBD defaults. Confirm vs any other store the human
   prefers for tunables.
