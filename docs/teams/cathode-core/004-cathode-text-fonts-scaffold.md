# 004 — Cathode Text & Fonts scaffold

## Nature

004 authors the **Text & Fonts** component (see `docs/browse/components-text-fonts.md`) — shaping,
measurement, font management, and per-run feature control. It sits across the boundary between
Layout and the Rasterizer, serving both from a single shaped result.

It is still a **no-build, no-execution sprint** (execution is on hold — the human is writing
first-build validation in parallel and the shared build env is in use). 004 authors the component as
proper C++ — the HarfBuzz swap boundary, the bespoke orchestration (metrics derivation, the
dual-consumer single-shaped-result, the shape-plan/result cache, feature mapping, the font registry
+ single-load byte sharing), and the tests for that bespoke logic — but **nothing is compiled or
run**. Behavioral proof, build/log coherence, and **all shaping/typography fidelity** are inherited
by the execution-approval sprint.

The **whole Text & Fonts is in scope** — one sprint, same pattern as 002/003.

**Cross-sprint carry (this sprint is the unblocker 003 named):** Text & Fonts produces the glyph
**extents** that `CathodeRasterizer/COVERAGE.md` named as the dependency for 003's deferred #2
(glyph-only-no-dirty) and #1 (vendor glyph fill). 004 retires those *dependencies*; the live
cross-module *wiring* still belongs to the integration sprint.

## Scope Boundary

**This sprint owns:** our internal **HarfBuzz swap boundary** (shaping in our types, `hb_*` confined
behind the impl), the **single shaped result** (`FShapedRun`) serving both consumers, **metrics
derivation** (advances → width, ascent/descent), the **shape-plan/result cache**, **per-run feature
control**, the **font registry + single-load byte store** (engine-neutral `FFontHandle` + a
font-bytes accessor), the **glyph-run + measurement seams**, the knob inventory + config, a
`COVERAGE.md` accountability record, and the UE Automation specs for the bespoke logic.

**This sprint does NOT own** (named, with the owner):
- **Any compile, link, build, run, or shaping/typography fidelity proof** — *execution-approval
  sprint*. Tests authored, not run; the shaped result is HarfBuzz's, proven only when shaped.
- **Glyph rasterization** — *Rasterizer (003)*. Text & Fonts produces the glyph run + extents + the
  engine-neutral font handle; the Rasterizer maps the handle → `BLFont` and draws. Text & Fonts
  **never reaches into Blend2D/`BL*`**.
- **Layout** — *Layout & Style*. Text & Fonts serves measurement off the shaped result; Layout
  consumes it.
- **The live cross-module wiring** — Layout calling measure, the Rasterizer calling the glyph-run
  seam — *integration sprint*. The production extraction logic is authored + tested now; the
  cross-module callers are future, Core-only this sprint.
- **Bidirectional (RTL) ordering** — *deferred* (a separate small lib until non-Latin RTL is real;
  doc).
- **Building/vendoring HarfBuzz** (staged in 001; binaries are the execution sprint).

## Problem

Text & Fonts does not exist. The Rasterizer (003) routes a `DrawGlyphRun` but its glyph data is a
`TODO(Text)`: `FPositionedGlyphRun` carries no glyph ids/positions/extents and no font, so text does
not render and glyph-only frames do not even dirty (003 deferred #1/#2). Nothing shapes, measures,
manages fonts, or produces the shaped result both Layout and the Rasterizer need.

## Goal

Finishing 004 looks like: Text & Fonts authored as a proper subsystem — HarfBuzz confined behind our
own swap boundary at the right altitude; one shaped result (`FShapedRun`) serving both Layout
(metrics) and the Rasterizer (glyph run) with **no double-shape**; the font registry loading bytes
**once** and sharing them so HarfBuzz and Blend2D build over the **same face** (same glyph-id space);
the glyph run carrying ids + fractional positions + **extents** + an engine-neutral font handle that
does not couple Text & Fonts to Blend2D; per-run feature control; effective tests for the bespoke
orchestration — all without a build, a run, or any shaped position hand-asserted.

The font-ownership decision, verbatim:

> "Single load share, and it can just be the local file system, and it can even be coupled to
> unreal. So it could go through the unreal asset engine if that's not overengineering it."

The seam philosophy (carried from 003), verbatim:

> "We'll expect a contractual interface as we add those systems in, but since you don't know the
> shape of those systems, we'll keep it generalizable and allow it to evolve as the system evolves."

## Final State

1. A **HarfBuzz swap boundary** in our types: a shape entry (string + font + features → our shaped
   result) wrapping `hb_shape`/`hb_font`/`hb_buffer`. `hb_*` appears only in the impl; swappable.
2. **`FShapedRun`** (our type): the glyphs (id, advances, offsets, cluster) + metrics + the
   `FFontHandle`. The single shaped result both consumers read.
3. **Font registry + single-load byte store:** an engine-neutral `FFontHandle` from the registry;
   the font file loaded **once** (local file system / UE file or asset APIs — least-overengineered,
   UE coupling OK, no UPTIME coupling) with a shared const **font-bytes** accessor. HarfBuzz builds
   `hb_font` and the Rasterizer builds `BLFont` over the **same bytes/face** → same glyph-id space.
4. **Metrics derivation** (our computation on the shaped glyphs: sum advances → width, ascent/descent
   from font metrics) feeding the **measurement seam** to Layout.
5. The **glyph-run seam** to the Rasterizer: glyph ids + **fractional** positions + **extents** + the
   `FFontHandle` — our neutral type (the 003 `FPositionedGlyphRun`), **no `BL*`**.
6. The **shape-plan/result cache** (key: text, font, features, size) — a repeat request does not
   re-shape.
7. **Per-run feature control:** run config (kern/liga/calt default; `kern=0` retro-CRT) → the
   HarfBuzz feature array.
8. A **knob inventory + config**, a durable **`COVERAGE.md`** (every component-doc responsibility
   covered-or-deferred with reason/dependency/unblocker/first-exec-risk), and **UE Automation specs**
   for the bespoke logic — asserting our bookkeeping via a recording shaper, **never the shaped
   positions**, not run.

## In Scope — ordered slices

Each follows the Method; fed only after the prior meets its evidence. The dev owns internal design;
the contract fixes responsibility + the testability discipline.

### Slice 1 — Audit + the HarfBuzz swap-boundary contract + the font seam design
Audit (read-only) the staged HarfBuzz API we need (`hb_buffer`, `hb_font`/`hb_face`/`hb_blob`,
`hb_shape`, `hb_feature_t`, `hb_glyph_info_t`/`hb_glyph_position_t`) and the staged Blend2D font path
the glyph-run seam must stay neutral against (`BLFontFace`/`BLFont`). Design the swap boundary
(shape entry in our types, `hb_*` confined), the `FShapedRun`/`FFontHandle`/font-bytes seam, and
begin the knob inventory. Output: the interface + altitude rationale + the font-seam design + the
initial knob inventory.

### Slice 2 — Font registry + single-load byte store
The registry resolving `FFontHandle`, loading the font file **once** (local FS / UE), owning the byte
lifetime, exposing the shared const font-bytes accessor. **Bespoke/testable:** handle resolution,
single-load (one load per handle on repeat lookups), shared-byte identity.

### Slice 3 — Shaping behind the boundary → `FShapedRun`
The shape entry wrapping `hb_shape` (feature array built from run config), assembling the shaped
glyphs into `FShapedRun`. **Bespoke/testable:** feature MAPPING (run config → `hb_feature_t` list),
the `FShapedRun` assembly. **Vendor (untested):** `hb_shape` itself; the shaped positions are vendor
value, never hand-asserted (the recording shaper returns scripted glyphs as opaque test input).

### Slice 4 — Metrics derivation + dual-consumer single-shape + result cache
The metrics derivation (sum advances → width, ascent/descent) and the **single shaped result feeding
both** measurement and the glyph-run with **one** shape call; the result cache. **Bespoke/testable:**
metrics math (against a recording shaper's scripted advances — our sum/extent, never the per-glyph
advance as truth), single-shape both-consume (shape-call-count == 1, same result handle to both),
cache hit/miss/eviction by shape-call-count.

### Slice 5 — Glyph-run + measurement seams + feature control + COVERAGE + finalize
The measurement seam to Layout and the glyph-run seam to the Rasterizer (ids + fractional positions +
**extents** + handle, neutral type), per-run feature control, the finalized knob inventory, and
`COVERAGE.md`. **Bespoke/testable:** both seams genuinely PRODUCED from the one shaped result and
CONSUMED (measurement extracts metrics; the glyph-run seam is populated incl. extents) — no
built-but-unconsumed, no double-shape, no `BL*` coupling. Extents production closes the 003 #1/#2
*dependency*.

## Non-Goals

- **No build, compile, link, run, or shaping/typography fidelity proof.** Acceptance never depends on
  a shaped run.
- **No hand-asserted shaped position/advance/glyph-id**, no readback of the run's positions as if
  they were the test's truth — the shaped result is HarfBuzz's output (the **tautology tell**: a
  recording shaper returns scripted values as INPUT; asserting those scripted values as "the shaping
  output" is circular and pretends to test the vendor). Measurement **width** (a sum of vendor
  advances) is deferred; assert that measurement READS+REUSES the one shaped result, not the number.
- **No full HarfBuzz re-export and no recreation** — only the ops we need, in our types.
- **Do not test vendor functionality** (HarfBuzz shaping) — assume it works.
- **No Blend2D/`BL*` in Text & Fonts** — the glyph-run/font seam is engine-neutral; the Rasterizer
  maps the handle → `BLFont` on its side.
- **No double-shape** — one shaped result serves both consumers.
- **No UPTIME coupling** (UE coupling for font loading is allowed; `Source/Uptime` is not).
- **Do not delete a fallback just because production does not hit it — trace callers and tests first.**

## Starting Surface

Audit first, producing the inventory before editing:
1. `vendor/lib/cathode/harfbuzz/` — the ops we need (`hb_buffer_create`/`add_utf8`/`guess_segment_properties`;
   `hb_blob`/`hb_face`/`hb_font` create + scale; `hb_shape` + `hb_feature_t`; `hb_glyph_info_t`/
   `hb_glyph_position_t`; font metrics).
2. `vendor/lib/cathode/blend2d/` — the font path the glyph-run seam must stay NEUTRAL against
   (`BLFontFace::create_from_data` / `BLFont`), so the seam carries bytes/handle, not `BL*`.
3. The 003 `FPositionedGlyphRun` (the neutral glyph-run type to fill) + its `COVERAGE.md` deferrals
   #1/#2 (the extents dependency 004 closes).

Inventory shapes:
`responsibility -> our boundary op -> HarfBuzz primitive(s) wrapped -> bespoke? (testable y/n)`
`knob -> owner (HarfBuzz-config | our-bespoke) -> tunable? -> applies-where -> DEFAULT`

## Method

No build/log coherence and **no shaping proof** this sprint. Acceptance is **structural conformance +
testability honesty**, by reading source:
- **Wrapper altitude:** the boundary exposes the shape ops we need in our types, confines `hb_*` to
  the impl, swappable — not an `hb_*` re-export, not a recreation.
- **Bookkeeping-not-shaping:** specs assert our orchestration (feature mapping, metrics math, cache
  hit/miss by shape-call-count, single-shape both-consume, font registry/single-load) via a
  **recording shaper**, and **never** the shaped positions/advances/ids. Reject the tautology tell.
- **Seam honesty (the 003 wired-not-built lesson, text-shaped):** ONE production shape PRODUCES the
  result; the measurement path AND the glyph-run-seam population BOTH genuinely CONSUME that SAME
  result (single shape, both extractions real + exercised). Reject a declared-but-unconsumed seam, a
  stub/hardcoded measurement, an empty glyph-run placeholder shaping never populates, or a
  double-shape. Cross-module callers (Layout/Rasterizer) are future; the extraction logic is real now.
- **Same-face correctness:** the font-bytes seam guarantees HarfBuzz and Blend2D build over the same
  bytes/face → same glyph-id space (the seam carries bytes/handle, not `hb_font`/`BLFont`).
- **No Blend2D coupling:** Text & Fonts is Core + HarfBuzz only; verify no `CathodeRasterizer`/`BL*`
  compile coupling.
- **Explicit deferral:** behavioral proof, build/log coherence, and all shaping/typography fidelity
  inherited by the execution-approval sprint — stated as deferred in every verdict; durable in
  `COVERAGE.md`.

## Acceptance — the gates that close the sprint

1. **Swap boundary:** the shape interface exists in our types at the right altitude; `hb_*` confined
   to the impl; vendor swappable; Text & Fonts has **no `BL*`/`CathodeRasterizer` coupling**.
2. **Component authored:** `FShapedRun` (one result), the font registry + single-load byte store
   (engine-neutral handle + bytes), metrics derivation, the cache, per-run feature control, and the
   measurement + glyph-run seams — all present per Final State; each seam traced to a real
   PRODUCED-and-CONSUMED path (single shape, both consume).
3. **Extents produced (003 carry):** the glyph-run seam carries glyph extents — the dependency 003
   `COVERAGE.md` #2/#1 named; verified produced (wiring stays integration-sprint).
4. **Tests authored for bespoke logic:** recording-shaper specs for feature mapping, metrics math,
   cache, single-shape both-consume, registry single-load — asserting our bookkeeping, **zero shaped
   positions / no tautology**, vendor not tested.
5. **Durable `COVERAGE.md`:** every component-doc responsibility covered-or-deferred
   (reason/dependency/unblocker/first-exec-risk); RTL deferred, live cross-module wiring deferred,
   shaping/measurement-width/fidelity deferred — all durable in-tree.
6. **No build/run/shaping proof occurred** and none is required to verify 1–5.

## Review Questions (open forks)

1. **Font loading mechanism** — local FS read vs the UE asset engine, the human's call relaxed to
   "least-overengineered, UE coupling OK." Dev picks the simplest honest mechanism; flag if it forces
   a structural choice.
2. **Module placement** — a dedicated `CathodeText` (or similar) module wrapping HarfBuzz (per the
   per-component-vendor convention from 003's `CathodeRasterizer`), quarantine-safe `.uplugin` edit.
   Dev proposes; lead/human ratify (touches the `.uplugin`).
3. **Shape-plan/result cache policy** (size/eviction) — a bespoke tunable like 003's dirty
   granularity; default now, tune later.

---

# Postmortem (sprint close)

Sprint 004 **closed ACCEPTED** — all five slices independently accepted by `uptime.browse-review`,
with the durable `COVERAGE.md` close gate cleared *without a split verdict* this time. The **whole
Text & Fonts** is authored as no-build/no-run scaffold: the HarfBuzz swap boundary (`hb_*` confined
to one `.cpp`), the dedicated `CathodeText` module + single-load font byte store (engine-neutral
`FFontHandle`, heap-stable shared bytes), shaping → `FShapedRun`, metrics + run extents + result
cache + the dual-consumer single-shape, and the measurement + glyph-run seams. All bespoke logic is
tested via a recording shaper/loader, **never the shaped output**. Behavioral proof, build/log
coherence, and all shaping/typography fidelity are inherited by the execution-approval sprint; the
durable record is `game/Plugins/Cathode/Source/CathodeText/COVERAGE.md`.

## Cross-sprint carry (003's loop closed)

004 produces the run **extents** (`DeriveExtents`, Slice 4) that `CathodeRasterizer/COVERAGE.md`
named as the dependency for 003's deferred #2 (glyph-only-no-dirty) and #1 (vendor glyph fill). Those
dependencies are **retired**; the live cross-module *wiring* (Text → Rasterizer's
`FPositionedGlyphRun`, the `FFontHandle` → `BLFont` map over the same bytes) stays the integration
sprint's. The same-bytes→same-face anchor (one font's bytes loaded once, both engines build over
them) is the correctness guarantee that makes HarfBuzz glyph ids match Blend2D outlines.

## Lead retrospective — uptime.browse-lead

This sprint ran clean — the corrections from 003 stuck. I waited for the human's font-seam decision
and the tuning steer before writing the contract (no getting ahead this time), and when the human
ruled HarfBuzz needs no tuning surface I relayed the **knob relaxation** so the dev did not over-build
a research-tuning struct. The accountability gate was applied *proactively* — I reminded the dev up
front that `COVERAGE.md` must be committed durable in-tree, so 003's split-verdict catch did not
recur.

Patterns I am keeping (now proven across four sprints): the **per-component-vendor swap boundary +
recording-backend** house pattern (Runtime/Raster/Text); relaying the **hardest acceptance criterion
up front** (no-forked-tick → one-dispatch-entry → single-shape) so the high-risk slice is built right,
not re-cycled; and keeping the reviewer's **coverage map independent** (I never forward the dev's
audit) so the completeness re-derivation is genuinely independent.

## Developer retrospective — uptime.browse-dev

Authored the whole component as no-build scaffold across five slices, carrying the 002/003 patterns:
vendor confined to one `.cpp` (grep-proven each slice); bespoke logic on top tested via a recording
shaper/loader; the swap-boundary altitude. The sharpest new discipline was the **tautology tell** —
the recording shaper returns scripted glyphs as opaque input, so every spec asserts *our* transform
(the sum for width, the cumulative position, the bbox, the feature mapping, the cache call-count, the
same-result-to-both) and never the input echoed back as "shaping output." The **same-face anchor**
(engine-neutral `FFontHandle` + single-load heap-stable byte store) guarantees HarfBuzz and Blend2D
agree on glyph ids without coupling Text & Fonts to Blend2D. The human's **knob relaxation** kept the
config minimal (no research-tuning struct). Applied the 003 accountability lesson proactively:
committed `COVERAGE.md` and pointed the in-code 003-dependency comments at the concrete resolving path
up front.

## Reviewer retrospective — uptime.browse-review

Re-derived every slice from source. The throughline was the **tautology tell and its per-field
sharpening**, applied consistently: transformed fields (px positions, width-sum, extents-bbox) are our
math → assert-directly; pass-through fields (glyph id, ascent/descent) reject asserting the *vendor
value* as truth, but a structural carries-it/preservation assertion is legit (no shaper runs; the
value is opaque input). Rulings: cache-as-dual-consumer satisfies the seam-invocation (one shape,
count==1, same one-shape-derived result — same-RESULT not same-instance is fine); the input/output
seam invocation-traces (input + single-shape S4; output GetMetrics/GetGlyphRun S5); extents genuinely
produced (the 003 closer); and **reject-on-completeness** on `COVERAGE.md` re-derived against an
independent map — confirmed which-fonts-ship is deferred-*with*-accountability (not silently absent
because loading is covered), committed in-tree, references resolving. All behavioral/shaping/fidelity
stayed deferred and stated as such.

## Execution-approval sprint (and the integration sprint) inherit

**The durable record is `CathodeText/COVERAGE.md`** — its DEFERRED list is the execution inbox:
- **#1 live cross-module seam consumers** — Layout calls measure; the Rasterizer maps `FFontHandle` →
  `BLFont` over the same bytes and consumes the glyph run into 003's `FPositionedGlyphRun`
  (integration sprint).
- **#2 which-fonts-ship / bundle** — the *loading* mechanism is covered (local FS / UE); *which* fonts
  ship is a content + Resource-Resolver decision (deferred).
- **#3 RTL / bidi** — a separate small lib until non-Latin RTL is real.
- **#4 shaping / typography fidelity** — HarfBuzz's shaped output + the real measurement width and
  ascent/descent values, proven at execution + human review.

Then: un-stub `CathodeText.Build.cs`'s HarfBuzz `ThirdParty` block (SDL2/`CathodeRasterizer` pattern),
build HarfBuzz from source, activate the plugin — the first owned build event, where build/log
coherence applies and the recording-shaper specs finally run. **Cross-sprint:** 004 retired 003's
#1/#2 extents dependency; the live Text → Rasterizer wiring is the integration sprint's.
