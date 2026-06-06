# 005 — Cathode Layout & Style scaffold

## Nature

005 authors the **Layout & Style** component (see `docs/browse/components-layout-style.md`) — it turns
the DOM tree + CSS into positioned boxes and the paint calls that draw them, defines the CSS coverage
authors get, drives relayout on dirty, routes paint, and surfaces computed geometry + resolved style
for the observer. It is the connective component: it **produces** the draw calls the Rasterizer (003)
consumes and **consumes** the measurement Text & Fonts (004) provides.

It is still a **no-build, no-execution sprint** (execution on hold). 005 authors the component as
proper C++ — the litehtml swap boundary, the bespoke orchestration (paint routing, border
decomposition, dirty tracking + the strategy-agnostic relayout with a re-feed baseline, the
geometry/style read), and the tests for that bespoke logic — but **nothing is compiled or run**.
Behavioral proof, build/log coherence, and all layout/CSS fidelity are inherited by the
execution-approval sprint.

The **whole Layout & Style is in scope** — minus **hit-testing** (point→element, which served the
deferred pointer input; the element→box **geometry read** for observation *is* in scope). Same
pattern as 002/003/004. The human, verbatim:

> "We have a complete rerender, and then we have … intelligent Dirty Dom rerender. Continuing the
> same pattern of creating our own primitives and then wrapping the library that we are reliant upon."

## Scope Boundary

**This sprint owns:** our internal **litehtml swap boundary** (`ILayoutEngine` in our types,
`litehtml`/`lt::` confined to the impl), the **`document_container`** impl (the central callback seam
litehtml calls during render), **dirty tracking + the strategy-agnostic relayout** (with a
**complete-rerender / re-feed baseline** behind the seam), **paint routing** (litehtml draw ops → our
neutral paint-op stream), **border decomposition** (solid → 4 edge fills), the **geometry + bounded
resolved-style observation read**, the **neutral seams** (paint-op output, `ITextMeasurer` input), a
`COVERAGE.md`, and the UE Automation specs for the bespoke logic.

**This sprint does NOT own** (named, with the owner):
- **Any compile, link, build, run, or layout/CSS fidelity proof** — *execution-approval sprint*. The
  whole component is **built**; fidelity is *proven* in the validation pass once the support surfaces
  are connected. "Can't see it run" is **not** a reason to defer building anything the doc requests.
- **Glyph rasterization + drawing** — *Rasterizer (003)*. Layout produces a neutral paint-op stream;
  the integration sprint maps it → 003 `FPaintOp`. Layout never reaches into Blend2D/`BL*`.
- **Text shaping/measurement** — *Text & Fonts (004)*. Layout calls a neutral `ITextMeasurer`; the
  integration sprint backs it with 004's shaper. Layout never reaches into HarfBuzz/`hb_*`.
- **Styled/rounded border *drawing*** — Layout builds the **full border routing** (solid → edge fills;
  styled/rounded → a neutral stroke/rounded-rect/path op); what is missing is the **Rasterizer's**
  stroke/path op to *consume* it (003 `COVERAGE.md` #3). The routing functionality is built here; only
  the Rasterizer-side draw op is deferred.
- **Image byte loading/resolution** — the *Resource Resolver* (future); Layout routes `load_image`/
  `get_image_size` to a neutral image-info seam.
- **Hit-testing (point→element)** — served the deferred pointer input; *Input Routing* (deferred).
- **The DOM tree (the input)** — *DOM component* (future); Layout consumes a retained-DOM source via a
  neutral seam.
- **The live cross-module wiring** (Layout→Rasterizer paint, Layout→Text measurement, DOM→Layout) —
  *integration sprint*. The production extraction/production logic is authored + tested now.
- **CSS gaps (e.g. grid)** — forked in only as real screens need them (doc); not this sprint.
- **Building/vendoring litehtml** (staged in 001; binaries are the execution sprint).

## Problem

Layout & Style does not exist. The Rasterizer (003) routes `FPaintOp` but has no producer; Text &
Fonts (004) exposes a measurement seam but no consumer; there is nothing that turns DOM + CSS into
boxes + paint calls, drives relayout, or surfaces geometry/style for the observer. litehtml is staged
but unwrapped.

## Goal

Finishing 005 looks like: Layout & Style authored as a proper subsystem — litehtml confined behind our
own swap boundary; a strategy-agnostic relayout seam carrying **both** a complete-rerender (re-feed)
path **and** the **intelligent dirty-DOM rerender** (efficient relayout-on-dirty) — both *built*, with
the validation pass proving which/whether at execution; the **full paint routing** (all borders incl.
styled/rounded → neutral ops) producing a **neutral paint-op stream** (mapped to 003 `FPaintOp` at
integration); a neutral `ITextMeasurer` input (backed by 004 at integration); the geometry + bounded
resolved-style read for the observer; effective tests for the bespoke orchestration — all without a
build, a run, or any litehtml-computed box/cascade hand-asserted.

The relayout decision, verbatim: *"We have a complete rerender, and then we have … intelligent Dirty
Dom rerender"* — **both are built in this sprint** (the implementation sprint is where the doc's
functionality lives; can't-validate is never a reason to defer). The litehtml-fork-or-not mechanism is
the dev's design call, built now and validated when connected. The coupling decision: our own neutral
primitives wrapping the library, no cross-module coupling.

## Final State

1. A **litehtml swap boundary** (`ILayoutEngine` in our types: `SetDocument(retained-DOM source)`,
   `MarkDirty(node/region)`, `Relayout(availableWidth) → FLayoutResult{positioned geometry + neutral
   paint ops}`). `litehtml`/`lt::` only in the impl; swappable.
2. The **`document_container`** impl (litehtml confined): paint callbacks → our neutral paint-op
   stream; text callbacks → our neutral `ITextMeasurer`; rect/dpi callbacks = config/input.
3. **Dirty tracking + relayout** strategy-agnostic, carrying **both** a complete-rerender (re-feed)
   path and the **intelligent dirty-DOM rerender** (efficient relayout of only the dirtied subtree) —
   both built behind the seam, relayout-on-dirty genuinely invoked + paint-routed off the result.
4. **Paint routing**: litehtml `draw_background`/`draw_borders`/`draw_text`/`draw_list_marker`/image →
   our neutral paint ops (fill rect/box, draw image, draw glyph run, clear) with our-mapped params.
5. **Border routing (full)**: solid border → 4 neutral edge fill ops; styled/rounded → a neutral
   stroke/rounded-rect/path op (the Rasterizer-side draw op to consume it is 003's deferral, not ours).
6. The **geometry read** (element→box, our walk + neutral per-node mapping) and the **bounded
   resolved-style mapping** (litehtml cascaded values → our contract-scoped property set, not the
   full CSSOM) for the observer.
7. The **neutral seams**: the paint-op output (→ 003 `FPaintOp` at integration) and the
   `ITextMeasurer` input (→ 004 measurement at integration). Layout = Core + litehtml only.
8. A **knob inventory + config** (minimal — no tuning surface), a durable **`COVERAGE.md`**, and **UE
   Automation specs** asserting our bookkeeping via a recording backend, **never litehtml's computed
   boxes/cascade**, not run.

## In Scope — ordered slices

Each follows the Method; fed only after the prior meets its evidence. The dev owns internal design.

### Slice 1 — Audit + the litehtml swap-boundary contract + the neutral seams
Audit (read-only) the staged litehtml `document_container` + render API and the 003 `FPaintOp` / 004
measurement shapes the neutral seams stay neutral against. Design `ILayoutEngine`, the neutral paint-op
output type, the neutral `ITextMeasurer` input, and the retained-DOM-source seam. Begin the knob
inventory. Output: the interface + altitude rationale + the seam design + initial knobs.

### Slice 2 — Establish the CathodeLayout module + the `document_container` skeleton
The dedicated `CathodeLayout` module (Core + litehtml; litehtml `ThirdParty` commented; quarantine-safe
`.uplugin` edit) and the `document_container` impl skeleton (litehtml confined; callbacks stubbed to
the neutral seams). **Bespoke/testable:** module quarantine; the callback→neutral-seam routing
structure (no litehtml leak in the public surface).

### Slice 3 — Dirty tracking + strategy-agnostic relayout (both paths built)
The dirty tracking (mark→relayout-scheduled→flush) and the relayout seam carrying **both** the
complete-rerender (re-feed) path **and** the intelligent dirty-DOM rerender (efficient relayout of only
the dirtied subtree). **Bespoke/testable:** dirty bookkeeping (mark/scheduled/flush), the relayout
INVOCATION (mark-dirty → relayout invoked → result produced) for both paths, the dirty-DOM rerender's
affected-subtree selection (which nodes re-lay-out given a dirty mark — our logic, not litehtml's
computed boxes). The fork-or-not mechanism is the dev's design; both paths are built and tested, proven
in the validation pass.

### Slice 4 — Paint routing + full border routing
The `document_container` draw callbacks → the neutral paint-op stream, and the full border routing.
**Bespoke/testable:** paint-routing translation (which op + our-mapped params, via a recording
backend), border routing (solid → 4 edge rects; styled/rounded → a neutral stroke/path op with the
mapped border params). Never litehtml's computed box values.

### Slice 5 — Geometry + bounded resolved-style observation read
The element→box geometry read (our walk + neutral mapping) and the bounded resolved-style mapping (→
contract-scoped properties). **Bespoke/testable:** the read-out structure + the bounded mapping; the
box/cascade values are vendor (opaque), never asserted.

### Slice 6 — COVERAGE + finalize
The neutral seams confirmed PRODUCED, the finalized knobs, and a durable `COVERAGE.md` (every
component-doc responsibility covered-or-deferred with reason/dependency/unblocker/first-exec-risk).

## Non-Goals

- **No build, compile, link, run, or layout/CSS fidelity proof.**
- **No hand-asserted litehtml-computed box (x/y/w/h) or cascaded/resolved CSS value** — that is
  litehtml's output (the layout-output tautology; assert our routing/decomposition/mapping/read-out
  structure, treat box/cascade values as opaque input, never as truth).
- **No full litehtml re-export and no recreation** — only the ops we need, in our types.
- **Do not test vendor functionality** (litehtml layout/cascade) — assume it works.
- **No `BL*`/`hb_*`/`CathodeRasterizer`/`CathodeText` coupling** — Layout = Core + litehtml; neutral
  seams, mapped at integration.
- **No hit-testing** (point→element; deferred input). **No image byte loading** (Resource Resolver —
  Layout builds the routing + info seam, not the bytes). **No grid / forked CSS gaps** (doc-deferred).
  (Styled/rounded borders *are* routed here — only the Rasterizer's draw op to consume them defers.)
- **Do not delete a fallback just because production does not hit it — trace callers and tests first.**

## Starting Surface

Audit first, producing the inventory before editing:
1. `vendor/lib/cathode/litehtml/` — the `document_container` callbacks we implement (text:
   `create_font`/`text_width`/`draw_text`; paint: `draw_background`/`draw_borders`/`draw_list_marker`/
   `get_image_size`/`load_image`/`set_clip`/`del_clip`; config: `get_client_rect`/`pt_to_px`/
   `get_default_font_*`) and `document::createFromString`/`render`/`draw`.
2. The 003 `FPaintOp` (CathodeRasterizer) + 004 measurement (CathodeText) shapes the neutral seams
   stay neutral against (carry our types, not theirs).
3. The 003 `COVERAGE.md` #1 (Layout = the draw-call source) + #3 (styled/rounded borders) + the 004
   measurement-consumer dependency that 005 produces the producer side of.

Inventory shapes:
`responsibility -> our boundary op -> litehtml primitive(s) wrapped -> bespoke? (testable y/n)`
`knob -> owner (litehtml-capability | our-bespoke) -> tunable? -> applies-where -> DEFAULT`

## Method

No build/log coherence and **no layout proof** this sprint. Acceptance is **structural conformance +
testability honesty**, by reading source:
- **Wrapper altitude:** the boundary exposes the layout ops we need in our types, confines
  `litehtml`/`lt::` to the impl, swappable — not a re-export, not a recreation.
- **Bookkeeping-not-layout:** specs assert our orchestration (paint-routing translation, border
  decomposition, dirty bookkeeping, relayout invocation, the geometry read-out structure, the bounded
  style mapping) via a **recording backend** fed scripted/opaque litehtml inputs, and **never**
  litehtml's computed box coords or cascaded values. Reject the layout-output tautology (running real
  litehtml and asserting `box==(x,y,w,h)` or `cascade==red`).
- **Relayout produced, strategy deferred:** the relayout interface + invocation + the re-feed baseline
  are genuinely PRODUCED + WIRED + tested (mark-dirty → relayout invoked → paint-routed); only the
  dirty-DOM **optimization** is deferred (durable in COVERAGE). Reject a declared-but-never-invoked
  relayout, a no-op stub, or deferring the *whole* relayout.
- **Neutral seams:** the paint-op output and `ITextMeasurer` input carry our types; verify no
  `BL*`/`hb_*`/`CathodeRasterizer`/`CathodeText` coupling. Cross-module wiring is integration.
- **Explicit deferral:** behavioral proof, build/log coherence, all layout/CSS fidelity inherited by
  the execution-approval sprint — stated as deferred in every verdict; durable in `COVERAGE.md`.

## Acceptance — the gates that close the sprint

1. **Swap boundary:** `ILayoutEngine` + the `document_container` impl in our types; `litehtml`/`lt::`
   confined to the impl; vendor swappable; no `BL*`/`hb_*`/cross-module coupling.
2. **Component authored:** dirty tracking + strategy-agnostic relayout with the re-feed baseline (real,
   invoked, not a stub), paint routing, solid-border decomposition, the geometry + bounded
   resolved-style read, the neutral paint-op output + `ITextMeasurer` input — all per Final State.
3. **Cross-sprint producer:** Layout produces the neutral paint-op stream (003's draw-call source) and
   the `ITextMeasurer` consumer side (004's measurement consumer); the live wiring stays integration.
4. **Tests authored for bespoke logic:** recording-backend specs for paint routing, border
   decomposition, dirty/relayout invocation, geometry read-out, bounded style mapping — asserting our
   bookkeeping, **zero litehtml-computed box/cascade assertions**, vendor not tested.
5. **Durable `COVERAGE.md`:** every responsibility covered-or-deferred (reason/dep/unblocker/first-exec
   -risk) — dirty-DOM optimization, styled/rounded borders, image loading, hit-testing, grid/CSS gaps,
   live cross-module wiring, layout/CSS fidelity all durable.
6. **No build/run/layout proof occurred** and none is required to verify 1–5.

## Review Questions (open forks)

1. **Module placement** — a dedicated `CathodeLayout` module (per the per-component-vendor convention),
   quarantine-safe `.uplugin` edit. Lead-ratified per the established convention; established Slice 2.
2. **The retained-DOM-source seam shape** — Layout consumes a retained DOM (future DOM component) via a
   neutral seam; kept generalizable/evolvable (the human's seam philosophy), shape settled in Slice 1.
3. **Shape-of the neutral paint-op type vs reusing 003's `FPaintOp`** — own neutral type (the human's
   "our own primitives wrapping the library"), mapped at integration; confirmed.

---

# Postmortem (sprint close)

Sprint 005 **closed ACCEPTED** — all six slices independently accepted, the durable `COVERAGE.md`
close gate cleared without a split verdict. The **whole Layout & Style** is authored as
no-build/no-execution scaffold: the litehtml swap boundary (litehtml confined to one `.cpp`), the
`document_container` impl, dirty tracking + the affected-subtree selection + **both** relayout paths
(complete re-feed **and** the intelligent dirty-DOM rerender), paint routing + full border routing +
the splice offset, and the geometry + bounded resolved-style observation read. It is the **connective
component**: it produces 003's draw-call source (incl. the `StrokeBorder` op for 003's deferred
stroke) and 004's measurement-consumer side (`ITextMeasurer`). Behavioral proof, build/log coherence,
and all layout/CSS fidelity inherited by the execution/validation passes; the durable record is
`game/Plugins/Cathode/Source/CathodeLayout/COVERAGE.md`.

## The defining correction (build, don't defer for validation)

The lead's first framing deferred the *intelligent dirty-DOM rerender* — the named de-risk — to the
spike, because its perf/correctness needs a running spike. The human rejected that:

> "just saying that you can't build a feature because you can't validate that you built a feature
> violates the entire premise of how we've been building this … include the functionality in the
> sprint that the document requests, even if you're not confident that you can see it working."

So the dirty-DOM rerender (affected-subtree selection + both relayout paths) is **built** in 005, and
the team found a cleaner construction than the fork-first reading: re-feed-of-affected-subtree, no
litehtml fork (the fork stays a seam-compatible contingency). Only the **run-dependent** parts defer —
the boundary-offset *value* (B's prior-layout position) and the efficient==full *equivalence proof* —
never the functionality. **Lesson: can't-validate is never a reason to defer building what the doc
requests; validation is a separate pass.** The valid deferral reasons are: a sibling component/feature
not built yet, the document itself defers it, cross-module wiring (integration), or the validation
pass for fidelity/equivalence.

## Lead retrospective — uptime.browse-lead

Two corrections I absorbed this sprint. (1) **Surface forks as concrete decisions** — I handed the
human an abstract "neutral vs bridge" coupling fork instead of a crisp choice; forks must name the
options, what each means in code, the consequence, and the explicit ask. (2) **Build, don't defer for
validation** (above) — the bigger one; I conflated "can't see it run" with "defer," and the human's
push-back kept the de-risk in the implementation where it belongs. Both banked to memory.

What worked and stays (now proven across four component sprints): the per-component **swap boundary +
recording backend + own-neutral-type + durable `COVERAGE`** house pattern; **relaying the hardest
acceptance criterion up front** (the no-forked-tick / one-dispatch-entry / single-shape /
no-under-select line) so the high-risk slice is built right; and keeping the reviewer's **coverage map
independent** (never forwarding the dev's audit).

## Developer retrospective — uptime.browse-dev

Authored the whole component as no-build scaffold across six slices. What went well: the **dirty-DOM
intelligence made first-class** by the human correction (my first read leaned toward deferring it as
can't-validate; the correction reframed it to built — affected-subtree selection + both paths behind
`IRelayoutBackend`, only the offset value + equivalence deferred). **Neutral own-type seams +
quad-confinement** — the bridge component that couples to none (litehtml in one `.cpp`; output our
`FLayoutPaintOp`, input our `ITextMeasurer`, source our `ILayoutDocumentSource`); the bespoke logic
(selection, paint translation, border decomposition, splice offset, font cache, observation walk +
bounded mapping) all testable via recording impls without litehtml. The **splice-offset-logic-built /
equivalence-deferred** split. Accountability applied **proactively** (COVERAGE committed + every
reference resolving) so no split-verdict. Carry-forward: **surface deferral/scope forks to the lead,
don't self-resolve** — that is what let the human correction land early; and confirm-don't-guess on a
crossed HOLD/PROCEED.

## Reviewer retrospective — uptime.browse-review

Re-derived every slice from source. The **quad-confinement** escalated for the bridge component
(litehtml only in the engine `.cpp`, no `BL*`/`hb_*`/`CathodeRasterizer`/`CathodeText`), with the
`document_container` vendor-edge nuance (litehtml in the overridden callback *signatures* is required;
leaking *past* the `.cpp` is the reject). **Never-under-select** re-derived (the 003 never-under-redraw
analog — boundary is an ancestor walk, Structure includes children, conservative-correct). The
**splice ruling** (offset *logic* must be built; only the run-dependent equivalence *proof* defers).
The **per-field carries-it** throughline (transformed/our-computed → assert-directly; pass-through
vendor → opaque, never as truth) and **bounded-by-construction** style. The **reject-on-completeness**
close gate with the **dirty-DOM-covered watch-point** (deferring the *feature* would be the
can't-validate mistake) and the **no-RTL faithfulness** catch (RTL is Text/004, not this doc). Proved
the instrument, never the vendor layout output.

## Execution + integration inheritance

**The durable record is `CathodeLayout/COVERAGE.md`** (7 deferrals). The execution-approval sprint
un-stubs `CathodeLayout.Build.cs`'s litehtml `ThirdParty`, builds litehtml from source, binds the
vendor edges (`GetBoundaryOffset`, `QueryNodeBox`, `QueryComputedValue`) to litehtml's placed
boxes/cascade, and proves fidelity + the efficient==full equivalence at the **validation pass**. The
**integration sprint** maps `FLayoutPaintOp` → 003 `FPaintOp` (incl. `StrokeBorder` → the 003 #3
stroke op), `FLayoutFontRef` → 004 `FFontHandle`, `ITextMeasurer` → 004's shaper, and
`ILayoutDocumentSource` → the DOM component. Cross-sprint: 005 produces what 003 (the draw-call
source + the stroke op) and 004 (the measurement consumer) named; the live wiring is integration.
