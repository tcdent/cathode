# 006 — Cathode Surfaces scaffold

## Nature

006 authors the **Surfaces** component (see `docs/browse/components-surfaces.md`) — the per-surface
**world-object** layer and the many-surfaces model: one shared runtime driving many independent
displays, from wall dashboards down to single-character LCDs. It is the **first pure-bespoke
component** — it wraps **no vendor library**. It is orchestration over the sibling components' neutral
seams.

Crucially, 003 (Rasterizer) **already built the per-surface producer**: `FSurfaceRasterizer` owns the
`FDoubleBuffer` + `FDirtyRegion` and emits `FFrameReadySeam`, its header naming "future Surfaces /
Texture Egress" as the consumers. So 006 **composes** that producer through a neutral seam and **does
not duplicate** buffering / dirty-tracking / frame-ready. What 006 owns is the layer 003 does not have:
the surface-as-world-object (identity + fixed dims/format), the runtime→surfaces **registry**, the
**lifecycle**, the **registration API**, the surface's **document binding** (005), and the **readiness
relay** to Texture Egress.

It is still a **no-build, no-execution sprint**. Because it wraps no vendor, it is the cleanest test
story yet — all our own bookkeeping — with **one residual carries-it line**: the buffer *pixels* it
carries are the Rasterizer's (ultimately Blend2D's) vendor output, asserted never by value, only by
state/identity/readiness.

The readiness/frame model, verbatim:

> "the renderer knows that it updated, and it only sends an updated frame when the render has actually
> completed. So it's entirely event driven … from the engine's perspective, it's always just
> referencing the buffer … it's up to the canvas of the HTML that we generate to keep itself fresh
> based on events."

The no-resize decision, verbatim:

> "we never resize canvases, so the browser scene is created with explicit dimensions on instantiation,
> and those never change."

## Scope Boundary

**This sprint owns:** the per-surface **world-object** (identity + **fixed** dims/format, set at
registration, immutable), the runtime→surfaces **registry** (one runtime, many surfaces, same global
state), the surface **lifecycle** (create/destroy on device spawn/despawn, no leak), the **registration
API** (a device declares "I have a screen" with explicit dims/format), the surface's **document
binding** (neutral 005 `ILayoutEngine` handle), the **readiness relay** to Egress (event-driven,
surface-addressed, only-on-change), and a neutral **`ISurfaceRenderTarget` seam** composing 003's
producer — plus a `COVERAGE.md` and the UE Automation specs for the bespoke logic.

**This sprint does NOT own** (named, with the owner):
- **Any compile, link, build, run, or fidelity proof** — *execution-approval sprint*.
- **The per-surface PRODUCER** (the double-buffer, dirty-region, frame-ready emission) — *003's
  `FSurfaceRasterizer`*, composed via the neutral `ISurfaceRenderTarget` seam, **not duplicated**. 003's
  `FDoubleBuffer` stays internally owned → exactly one double-buffer; Surfaces never touches a CPU
  bitmap.
- **Rendering** — Layout/Rasterizer (the surface's paint ops come from its 005 document).
- **GPU upload + the memory copy to texture** — *Texture Egress* (the readiness relay is the event +
  payload it consumes).
- **Resize/reformat** — **N/A by design**: canvases are created with explicit dimensions at
  instantiation and never change. Not a deferral; the `ISurfaceRenderTarget` seam carries **no**
  `Resize`.
- **The buffer PIXELS** — the Rasterizer's (Blend2D's) vendor output, carried through, never asserted
  by value.
- **The live cross-module wiring** (Surfaces↔003 producer / 005 document / Egress + the device-spawn
  caller) — *integration sprint*. The production logic is authored + tested now via recording sibling
  seams.
- **No `BL*`/`hb_*`/`litehtml`/`CathodeRuntime`/`CathodeRasterizer`/`CathodeText`/`CathodeLayout`
  coupling** — pure-bespoke = Core only + neutral seams; "couples to none" (even cleaner than 005:
  there is no vendor TU at all).

## Problem

003 emits `FFrameReadySeam` per producer, but nothing addresses frames to a *specific surface*, holds
the many-surfaces registry, owns surface lifecycle, or lets a device declare a screen. There is no
surface-as-world-object, no registry, no registration API, no readiness relay — and a naive 006 would
re-build the double-buffer / dirty / frame-ready 003 already has.

## Goal

Finishing 006 looks like: Surfaces authored as the world-object layer **over** 003's producer — the
surface object (identity + fixed dims/format), the runtime→surfaces registry (one runtime, many
displays, same global state), lifecycle (create/destroy, no leak), the registration API (device
declares a screen with explicit dims), the document binding, and the **event-driven, surface-addressed,
only-on-change readiness relay** to Egress — composing 003's producer via a neutral
`ISurfaceRenderTarget` seam, **no duplication, no coupling**, no resize. Effective tests for all the
bespoke bookkeeping; the buffer pixels stay the Rasterizer's, never asserted by value.

## Final State

1. A neutral **`ISurfaceRenderTarget` seam** (composing 003's producer): `ProduceFrame(paint ops) →
   changed`, `GetReadyFrame → front-buffer ref + dirty rects`, dims/format getters — **no `Resize`**.
   003's `FSurfaceRasterizer` backs it at integration; Surfaces holds it as a neutral handle, never
   touches a CPU bitmap, never imports `CathodeRasterizer`.
2. The **per-surface world-object**: identity + **fixed** dims/format + the render-target handle + the
   neutral document handle (005).
3. The **runtime→surfaces registry**: register/unregister/lookup/iterate; one runtime drives each.
4. **Lifecycle**: create on register, destroy on despawn — no leak of document/target.
5. The **registration API** (generalizable seam): a device declares a screen with explicit dims/format
   + a document ref, gets a surface id/handle; the device-spawn caller is integration.
6. The **readiness relay**: on the producer's frame-ready (event-driven, **only on actual change**),
   emit a **surface-addressed** readiness (surface-id + ready front buffer + dirty rect) to a neutral
   Egress seam; the engine always references the buffer (Egress copies on the event).
7. A **knob inventory** (minimal, no tuning — buffer count/format/granularity inherited from 003;
   per-surface dims = registration input), a durable **`COVERAGE.md`**, and **UE Automation specs**
   asserting our bookkeeping, never buffer pixel values.

## In Scope — ordered slices

Each follows the Method; fed only after the prior meets its evidence. The dev owns internal design.

### Slice 1 — Audit + the seams contract
Audit (read-only) the **built 003 `FSurfaceRasterizer`/`FDoubleBuffer`/`FFrameReadySeam`** (the producer
006 composes) and the 005 `ILayoutEngine` document handle + the Egress-facing readiness shape. Design
the neutral `ISurfaceRenderTarget` seam (no `Resize`), the surface-object type, the registration seam,
the document handle, the readiness seam to Egress. Begin the knob inventory. Output: the interfaces +
altitude rationale + the seam design + initial knobs.

### Slice 2 — Establish the CathodeSurfaces module + the per-surface object
The dedicated `CathodeSurfaces` module (Core only; quarantine-safe `.uplugin` edit; **no vendor
ThirdParty** — it wraps none) and the per-surface world-object (identity + fixed dims/format + the
render-target handle + the document handle). **Bespoke/testable:** module quarantine; the surface-object
assembly (binds the neutral handles; fixed dims immutable).

### Slice 3 — Runtime→surfaces registry + lifecycle
The registry (register/unregister/lookup/iterate; one runtime drives each) and lifecycle (create on
register, destroy on despawn). **Bespoke/testable:** register→exists at given dims; unregister→destroyed,
**no leak** (via a recording/counting target+document source); many coexist; the runtime-drive visits
each.

### Slice 4 — The event-driven readiness relay
On the producer's frame-ready (event-driven, only-on-change), assemble + emit the surface-addressed
readiness (surface-id + ready front buffer + dirty rect) to a neutral Egress seam. **Bespoke/testable:**
the relay genuinely PRODUCES + EMITS to a recording Egress sink; the producer's change DRIVES the emit
(no change → no emit); the payload carries surface identity + the ready-buffer ref + the dirty rect.
Buffer pixels never asserted by value; received rects are opaque carries-it.

### Slice 5 — COVERAGE + finalize
The neutral seams confirmed PRODUCED, the finalized knobs, and a durable `COVERAGE.md` (every
component-doc responsibility covered-or-deferred with reason/dependency/unblocker/first-exec-risk; resize
marked **N/A-by-design**, not deferred).

## Non-Goals

- **No build, compile, link, run, or fidelity proof.**
- **No re-implementation of the per-surface producer** (double-buffer / dirty / frame-ready = 003,
  composed not duplicated).
- **No buffer-pixel assertion** (the pixels are the Rasterizer's/Blend2D's vendor output, carried, never
  asserted by value); received dirty-rects are opaque carries-it (only Surfaces-computed bookkeeping
  asserts directly).
- **No `Resize`** — canvases never resize (fixed dims at instantiation, by design).
- **No `BL*`/`hb_*`/`litehtml`/sibling-module coupling** — Core only + neutral seams; the seams are
  wired to 003/005/Egress at integration.
- **No vendor wrapped** — there is no vendor TU; do not invent a vendor dependency.
- **Do not delete a fallback just because production does not hit it — trace callers and tests first.**

## Starting Surface

Audit first:
1. The **built 003** `CathodeRasterizer/Private/CathodeSurfaceRasterizer.h` (`FSurfaceRasterizer`),
   `FDoubleBuffer`, `FFrameReadySeam`, `FBitmapData` — the producer 006 composes (note: 006 does **not**
   import it; it audits the shape to design a neutral seam over it).
2. The 005 `ILayoutEngine` / `FLayoutResult` — the surface's document/paint-op source (neutral handle).
3. The Egress-facing readiness shape the relay targets (neutral, Egress = future).

Inventory shapes:
`responsibility -> our bespoke op -> composes-which-sibling (neutral seam) -> testable y/n`
`knob -> owner (inherited-003 | our-bespoke | registration-input) -> tunable? -> DEFAULT`

## Method

No build/log coherence and no fidelity proof. Acceptance is **structural conformance + testability
honesty**, by reading source:
- **Pure-bespoke, assert-directly:** the registry (add/remove/lookup/iterate, one-runtime-many), the
  surface-object assembly, lifecycle (create/destroy no-leak), the readiness payload assembly + emit
  gating, Surfaces-computed dirty-rect bookkeeping — all our design invariants, asserted via recording
  sibling seams.
- **The one residual carries-it line:** the buffer **pixel content** is the Rasterizer's vendor output
  (carried through) — assert buffer **state/identity/swap-driven readiness**, NEVER pixel values
  (reading buffer bytes to assert a color = REJECT, like 005's litehtml-box). **Received** dirty-rects
  (from the producer) are opaque carries-it; **computed** rects (if any) are our math.
- **Seam honesty:** the readiness is genuinely ASSEMBLED + EMITTED to a recording Egress sink; the
  producer change DRIVES the emit (no change → no emit); the registry-drive ITERATES the live surfaces;
  create/destroy genuinely CLEANS UP (no leak, via a counting target/document source). Reject a
  declared-but-never-emitted readiness, a change-that-doesn't-emit, a registry the drive never iterates,
  a leaky lifecycle.
- **No coupling:** Core only; verify no `BL*`/`hb_*`/`litehtml`/`CathodeRasterizer`/`CathodeLayout`/
  `CathodeText`/`CathodeRuntime`.
- **Explicit deferral:** behavioral proof, build/log coherence, the buffer pixels + the actual GPU copy,
  and the live cross-module wiring inherited by the execution/integration sprints — durable in
  `COVERAGE.md`. Resize is **N/A-by-design**, not deferred.

## Acceptance — the gates that close the sprint

1. **Neutral seams:** `ISurfaceRenderTarget` (no `Resize`), the surface-object, the registration seam,
   the document handle, the Egress readiness seam — all in our types; no vendor/sibling coupling; the
   producer composed (not duplicated — no second double-buffer/dirty/frame-ready).
2. **Component authored:** the surface world-object (fixed dims), the registry (one-runtime-many),
   lifecycle (no-leak), the registration API, the event-driven surface-addressed readiness relay — all
   per Final State.
3. **Tests authored for bespoke logic:** recording-seam specs for registry add/remove/iterate,
   lifecycle no-leak, the readiness relay (emit-on-change, surface-addressed payload), surface-object
   assembly — asserting our bookkeeping, **zero buffer-pixel assertions**, the producer/document not
   tested (composed siblings).
4. **Durable `COVERAGE.md`:** every responsibility covered-or-deferred; resize **N/A-by-design**; the
   live wiring + buffer pixels + GPU copy deferred (integration/execution), references resolve.
5. **No build/run/fidelity proof occurred** and none is required to verify 1–4.

## Review Questions (open forks)

1. **Module placement** — a dedicated `CathodeSurfaces` module (per the per-component convention),
   quarantine-safe `.uplugin` edit, **no vendor ThirdParty**. Lead-ratified per the convention; Slice 2.
2. **The registration-API + readiness-seam shapes** — generalizable/evolvable (device-spawn caller +
   Egress consumer are future integration); settled in Slice 1.
3. **The `ISurfaceRenderTarget` granularity** — composes 003's *producer* (`FSurfaceRasterizer`), not the
   bare `FDoubleBuffer`, so the swap/dirty/frame-ready 003 built stay one layer up (no duplication);
   confirmed by the dev's 003 audit.

---

# Postmortem (sprint close)

Sprint 006 closed with all five slices independently ACCEPTed, no rejections — the first
**pure-bespoke** component (wraps no vendor library), proving the per-component pattern extends to
a no-vendor case via two additions: **compose-not-duplicate** and **carries-it structural via opaque
`void*`**.

## Lead — uptime.browse-lead

- **The audit-before-consensus call paid off.** Routing the dev to audit *built* 003 code before the
  team's consensus read is what surfaced that `FSurfaceRasterizer` was already a per-surface producer
  (double-buffer + dirty + frame-ready), overlapping 3 of Surfaces' 5 doc responsibilities. That
  reshaped the sprint from "build a per-surface object" to "compose 003's producer, own only the
  world-object layer it lacks." Lesson to carry: for a component sitting over siblings, the first
  developer assignment is *audit the sibling's built code*, not draft against the doc.
- **The Slice-4 re-scope is the cadence lesson.** When the dev flagged at Slice 3 that `DriveAll`
  already exercised the relay's core, I narrowed Slice 4 to its genuine residual (direct isolation,
  null-handle guards, multi-rect count, cross-frame ref identity) rather than collapse it or let the
  dev pad. This is intra-sprint scope discipline I own — the readiness-relay *functionality* was built
  either way; I only kept a thin dedicated proof of the defining behavior. I instructed "skip anything
  S3 covers rather than pad," and review confirmed no padding. Surfacing over-coverage beats both
  rubber-stamping duplicates and manufacturing busywork.
- **Kept the reviewer's COVERAGE map independent.** I never forwarded the dev's audit to the reviewer;
  the close-gate completeness re-derivation ran against the reviewer's own map of the component doc.
  Keep this separation — it is what makes the completeness verdict worth anything.
- **Coordination friction:** `agent-message` requires the full `<addr>@a10k.internal`; a bare address
  fails silently in the background. Send foreground (or verify delivery) for anything gating.

## Developer — uptime.browse-dev

- **The doc states INTENT; the sibling CODE states what EXISTS.** Auditing built 003 before the
  consensus read surfaced the producer overlap and turned the sprint into compose-not-duplicate.
  On any component over siblings, audit their built code first.
- **Compose at the granularity where the sibling already BUNDLES the concern**, not one layer below —
  composing the bare `FDoubleBuffer` would have orphaned 003's swap/dirty/frame-ready bundle and
  re-duplicated exactly what reuse was meant to avoid.
- **When there is no vendor output to defer, enforce carries-it in the TYPES** — `FSurfacePaintBatch`/
  `FSurfaceBufferRef` are opaque `void*`, `==` by identity, so pixels cannot leak into an assertion
  even by accident. Pure-bespoke gave the strongest test story (couples-to-none: no vendor TU at all),
  but only because the boundary is structural, not just disciplined.
- **Report what's already proven so the next slice can be honestly small** — surfacing the S3/S4
  overlap let the lead re-scope instead of padding.
- **Inheritance:** `FSurface`/`FSurfaceRegistration` are move-only (TUniquePtr handles); integration
  expects move-only registration (ownership transfers into the registry). Deferred inbox lives in
  `CathodeSurfaces/COVERAGE.md`; Texture Egress is the natural next build — the consumer of
  `FSurfaceReadinessSeam` (surface-id + ready front buffer + dirty rects).

## Reviewer — uptime.browse-review

- **New axis: compose-not-duplicate is distinct from coupling.** A component can be coupling-clean
  (zero imports) yet still WRONG by re-declaring a sibling's already-built machinery. The decisive 006
  check was a two-pronged grep: no-import AND no-re-declare (zero `Swap/MarkChanged/FDirtyRegion/
  DoubleBuffer/FrontBuffer`). Every future compose-over-a-sibling component inherits this.
- **Carries-it structural via opaque `void*` is the strongest carries-it yet** — model a carried
  sibling/vendor output as an opaque handle so a content assertion is impossible by construction;
  assert identity/count, never content. Recommend as the default for every future carries-it boundary.
- **Couples-to-none must hold at the Build.cs**, not just the header — Core-only, no ThirdParty block
  at all for a no-vendor component.
- **Re-scope assessment over rubber-stamp:** flagged the S3/S4 over-coverage, then verified S4 was
  genuinely new (direct isolation, both null-guards, multi-rect, cross-frame identity), not byte-for-
  byte padding. Accept a residual only if new; reject padding.
- **`N/A-by-design` is not `deferred`** — a closed design decision (resize: immutable dims, no `Resize`
  on the seam, human-quoted) must be stated as N/A, never parked in the deferred list. Re-derived
  COVERAGE against the reviewer's own doc map; references must resolve in-tree (the 003 split-verdict
  lesson). For pure-bespoke there is no recording-vendor-backend — recording *sibling-consumers*
  (counting document+target, recording Egress sink) drive the bespoke bookkeeping instead.

## Inheritance for the next sprint

- **Integration (its own sprint):** back `ISurfaceDocument` ← 005 `ILayoutEngine`,
  `ISurfaceRenderTarget` ← 003 `FSurfaceRasterizer` (the existing one — compose-not-duplicate must hold
  at wiring), `FSurfaceReadinessSeam` → Texture Egress, `Register` ← the device-spawn path.
- **Execution:** first-build compile + the buffer pixels + the single CPU→GPU copy.
- **Open thread carried to Egress framing:** confirm the UE5.7 `RHIUpdateTexture2D` command-ordering
  semantics (the no-tear guarantee is RHI command ordering, not copy speed; no sliding GPU buffer) —
  flagged as inference to verify before Egress commits.
