# Sprint 014 — CSS Build-How Properties (letter-spacing, opacity)

Builds the two **build-how-committed** CSS properties the 013 epic forward-declared and that survive the gap descoping:
`letter-spacing` and `opacity`. Each is **Axis-3 IN** but absent from the vendored litehtml — declared, gate-accepted,
and **cascade-empty** since 013. This sprint lands each property's build-how by **leveraging the vendored libraries**
(HarfBuzz, Blend2D) for the rendering, sourcing the value Cathode-side, then moving the property from build-how-pending
to present (now a **Cathode-side-computed** category — see the declaration-model question below).

> Design-of-record: `docs/browse/components-css.md` (the build-how rulings, browse-design) + `docs/browse/spec-surface.md`
> (Axis-3 IN). The PM's 014 scope ratifies the two rulings and their order.

**Governing principle (tcdent, standing for 014):**

> "Leverage the libraries we have, don't invent. Reach into litehtml/HarfBuzz/Blend2D for … letter-spacing, and
> opacity — the answers are in the libs."

The audit's *first* job, per property, is to find the existing capability the vendored library provides — HarfBuzz's
letter-spacing / tracking (post-shaping advance), Blend2D's group-alpha compositing (layer / global alpha) — and wire
it through. The dev's slice-2/3 grounding **confirmed both libs have the mechanism**, so these are light Cathode-side
**wiring** slices, not forks. (This is what gap was *not* — see the descoping.)

**gap descoped (out of scope, rolled back).** `gap`/`row-gap`/`column-gap` are **removed** — tcdent ruled they were
not authored into the spec. The library-first gap audit grounded, source-level, that **no litehtml** (v0.9, latest tag
v0.10, or master) has flex-gap (`string_id.h` has zero gap ids), so "re-vendor solves gap" was a false premise and the
question re-opened to fork-vs-margins; tcdent then descoped it entirely. The 013 forward-declaration (`gap`/`row-gap`/
`column-gap`, committed `build_how_committed` in `4cedfea`) is rolled back from the capability set (the dev's code
rollback), and `spec-surface.md`/`components-css.md` roll it back from the design-of-record (browse-design). After the
rollback the build-how set is **2** (`opacity`, `letter-spacing`); a `style.gap` write is dropped (no longer declared).

## Scope Boundary

**This sprint owns** the two surviving build-how properties end-to-end: for each, the **Cathode-side value source** (the
cascade value litehtml never computes) **and** the rendering via the vendored library, **and** the declaration move
(build-how-pending → a Cathode-side-computed present category).

**Out of scope (named siblings):**
- **`gap`/`row-gap`/`column-gap`** — descoped (above); not in 014, rolled back from 013.
- **`background-image` rendering** — dependency-gated on the **Resource Resolver** (not yet built); the computed value
  is already in the 013 declaration; only the rendering is deferred, to the Resolver sprint.
- **`getElementsBy*`** — ruled out by browse-design; no live-returns slice.
- **The capability-declaration mechanism** (the inverted name-table-minus-exclusions, the membership gate, the
  completeness test) — owned by 013, **reused**; this sprint only moves each property's status.

## Problem

`letter-spacing` and `opacity` are **build-how-committed** — Axis-3 IN, but the vendored litehtml doesn't parse them
(neither is in `string_id.h`). Since 013 each is in the declaration, marked build-how-pending, gate-accepted to the
canonical `style` attribute, yet cascade-empty (`getComputedStyle` returns empty — correct per the 013 gate-flip
boundary). They are author-writable but inert: no glyph tracking, no alpha. **The common thread (dev grounding):**
litehtml lacks the cascade *value* for both, so the value is parsed **Cathode-side** from the gate-accepted `style`
attribute; the *rendering* lives in the libraries — HarfBuzz (advances) and Blend2D (global alpha) — which **do** have
the mechanism (unlike litehtml's missing flex-gap).

## Goal

Each property **computes** (the Cathode-side value resolves → `getComputedStyle` returns it) **and renders** (HarfBuzz
tracking / Blend2D group alpha), moving from build-how-pending to a Cathode-side-computed present status.

## Final State

- **`letter-spacing`**: the resolved value applies as extra post-shaping advance per grapheme over HarfBuzz's output
  (CathodeText measurement + advance); text widens by the spacing; `getComputedStyle('letter-spacing')` returns the
  resolved px; present (Cathode-side-computed).
- **`opacity`**: **true group opacity** — a subtree with `opacity < 1` composites **once** at alpha via Blend2D's
  group-alpha (layer / global alpha), not per-element; `getComputedStyle('opacity')` returns the number string;
  present (Cathode-side-computed). Or — only if compositing proves prohibitive at build-time — the **drop is escalated
  to tcdent**, never a naive per-element fallback.
- **Firewall held**: any library touch is in its owning Cathode rendering module (CathodeText / CathodeRasterizer);
  `CathodeDom → litehtml` stays absent; the value crosses as `FResolvedStyle` (FString).

## In Scope (ordered slices)

Each slice is **audit-first, library-first**: confirm the HarfBuzz/Blend2D capability and the Cathode-side value source
(+ its declaration-model handling), produce the starting-surface inventory, and **surface any design fork to the lead
before building**.

1. **`letter-spacing`** — **leverage HarfBuzz's post-shaping advance**: add the resolved spacing to each grapheme's
   advance (CathodeText reads HarfBuzz's `x_advance` into `FShapedGlyph.XAdvance` already). The value is sourced
   Cathode-side — **but letter-spacing INHERITS**, so an inline-only parse is incomplete (a child inherits the parent's);
   the audit grounds the inheritance handling (the litehtml-value path is dead — litehtml doesn't parse it — vs a
   Cathode-side inheritance walk, which is the 013 reconstruct-from-downstream smell; weigh + surface, don't silently
   pick). *Method:* a real text run with `letter-spacing` → the measured advance widens RELATIONALLY (≈ base advance +
   spacing × (glyphs − 1)); `getComputedStyle` returns the resolved px.
2. **`opacity`** (heaviest, own slice, last) — **leverage Blend2D's group-alpha compositing** (layer / global alpha) for
   **true group opacity** (a subtree composites once at alpha). opacity does NOT inherit → an inline value parse is
   clean (no inheritance walk). *Audit scopes the compositing mechanism + the paint-model GROUP boundary;* a
   prohibitive-compositing wall escalates to tcdent (the only drop path). *Method:* a subtree with `opacity` +
   overlapping children → the composited result is the **group** alpha (overlap does NOT double-darken — the fidelity
   discriminator vs naive per-element), RELATIONAL vs the renderer's own composite; `getComputedStyle` returns the
   number string.

## Non-Goals

- **No naive per-element opacity** — true group opacity only; per-element alpha is a ruled fidelity bug.
- **No gap/row-gap/column-gap** (descoped) and **no `background-image` rendering** (Resolver-gated) and **no
  `getElementsBy*`** (ruled out).
- **No CathodeDom → litehtml link** — the value is Cathode-side, crossing as `FResolvedStyle` (FString); the rendering
  is in CathodeText/CathodeRasterizer.
- **No re-opening the 013 declaration mechanism** — reuse it; only move each property's status (to Cathode-side-computed).
- **No reconstructing the cascade Cathode-side** beyond what is forced — the letter-spacing inheritance fork is surfaced,
  not silently resolved with a fragile cascade walk.
- **Do not drop a property because it is hard** — a drop is tcdent's call, opacity only, compositing-prohibitive only.

## Starting Surface

Audit first, per property. Inventory shape:

`property → library capability (HarfBuzz tracking? / Blend2D group-alpha? — confirmed present) → value source
(Cathode-side parse from the style attribute; inheritance handling for letter-spacing) → declaration move
(build-how-pending → Cathode-side-computed present; getComputedStyle read path) → proposed action`

Audit targets: the HarfBuzz shaping API + CathodeText's `FShapedGlyph.XAdvance` / measurement path; the Blend2D
compositing API (global alpha / layer) + CathodeRasterizer's paint-op model (`FLayoutPaintOp` has no alpha/group field
today); the Cathode-side value source + the getComputedStyle read path for a non-litehtml-computed value.

## Method

Per slice: **audit-first, library-first** (read the HarfBuzz/Blend2D API before building — leverage, don't reinvent;
surface forks up). The value is **Cathode-side** (litehtml never computes these), crossing as `FResolvedStyle` FString —
`CathodeDom → litehtml` stays absent (prove by-the-negative). Prove **RELATIONALLY** (assert against the renderer's own
output / the geometry, never a hand-typed pixel; on a mismatch true up only to a relationally-derivable expected). Build
**discriminating** geometry (letter-spacing advance vs base; group-opacity overlap vs per-element double-darken). Run
the **FULL shared-path regression EMPIRICALLY** (the text/raster paths are shared — the 013 cssText lesson). Coherence
per run.

## Acceptance

For `letter-spacing` and `opacity`:
- **Computes:** the Cathode-side value resolves; `getComputedStyle(el).<prop>` returns the real resolved value
  (empty is now a defect for the landed property).
- **Renders, RELATIONALLY:** letter-spacing → measured advance widens by the spacing; opacity → the **group** composite
  (overlap does not double-darken), vs the renderer's own output.
- **Declaration moved:** the build-how-pending count drops by the landed property to a **Cathode-side-computed present**
  status (browse-design rules the declaration-model representation + the getComputedStyle read path); the gate still
  accepts it; the mechanism is reused.
- **No regression:** the FULL Cathode.Dom + Cathode.Layout (+ CathodeText for letter-spacing) suites stay green,
  empirically run.
- **FORK-1 firewall:** `CathodeDom → litehtml` absent (source-dispositive).
- **opacity only:** true group opacity, or the compositing-prohibitive wall escalated to tcdent.

## Review Questions (open forks)

1. **The declaration model (Cathode-side-computed):** litehtml never computes `letter-spacing`/`opacity`, so they do NOT
   enter the 013 litehtml file-derived computed set — they become **Cathode-side-computed**; `getComputedStyle` reads the
   Cathode-parsed value (not a litehtml getter), the completeness test's file-derived set is unchanged. browse-design
   rules the declaration-model representation (the new category) + the getComputedStyle read path. Grounded at the
   slice-2 audit (or earlier if browse-design takes it).
2. **letter-spacing inheritance:** letter-spacing inherits; the litehtml-value path is dead (litehtml doesn't parse it),
   so inheritance needs handling Cathode-side — but a cascade walk is the 013 reconstruct-from-downstream smell. The
   slice-1 audit grounds the path (inline-plus-inheritance handling) + surfaces; don't silently reconstruct.
3. **opacity true-group mechanism:** the Blend2D group-alpha API (layer / global alpha) + the paint-model GROUP boundary
   (`FLayoutPaintOp` needs an alpha/group field — group-vs-per-op is a slice-2 design fork, group is spec-correct;
   browse-design's call) + the **drop trigger** (compositing-prohibitive only, tcdent's only).

## Outcome

014 delivered the **value side** of `letter-spacing` and `opacity` and **descoped** `gap`. The sprint's premise (the
governing-principle "leverage the libraries for the rendering") was corrected twice by source-grounded library-first
audits, reframing the deliverable from "value + rendering" to **value side now, rendering deferred to the integration
phase** — consistent with 013 (which delivered the value side and never rendered pixels) and the island architecture.

**Commits (in order):**
- `9f5b293` — **gap descoped.** tcdent ruled `gap`/`row-gap`/`column-gap` not authored into the spec. The library-first
  audit had grounded, at litehtml's own `string_id.h` (the 013 capability authority), that **no** litehtml — v0.9, the
  latest stable tag v0.10, or master — parses flex-gap, so the "re-vendor solves gap" premise was false; tcdent then
  descoped it. The 013 forward-declarations (committed `build_how_committed` in `4cedfea`) are rolled back; the orphaned
  `BuildHowCommitted` enum retired in manifest↔enum lockstep. `spec-surface.md`/`components-css.md` rolled back the
  design-of-record separately (browse-design); the CathodeVerify `STYLE_CAPTURE` `gap` entry left as a capture-design
  follow-on.
- `48cc612` — **letter-spacing value side.** `CathodeComputed` status + an on-demand own-inline parse in
  `QueryComputedValue` (px/em, em resolved against the element's own font-size); inheritance **deferred** to the litehtml
  build-how (own-element-only, **not** Cathode-reconstructed — the anti-reconstruction ruling); status **transitional**
  → Present if litehtml ever parses it.
- `c4c85b8` — **opacity value side.** Same pattern; parse clamps to `[0,1]` with `%`, unset → the CSS initial `'1'`
  (opacity does not inherit). Status **terminal** (litehtml never parses opacity — compositing is the rasterizer's job).
  The now-empty `BuildHowPending` enum retired in lockstep (concept preserved in the design-of-record), which also
  reconciled a slice-1 carryover (letter-spacing's manifest status string had lagged at `build_how_pending`). Mirror
  **110 = 108 file-derived Present + 2 CathodeComputed** {opacity, letter-spacing}.

**Forks resolved (design-of-record updated):** the **declaration model** — a new `CathodeComputed` status, on-demand
own-inline read, **property-dependent migration** (letter-spacing transitional, opacity terminal); **inheritance** —
own-element-only, deferred to the build-how (not a scope cut); the **BuildHowPending taxonomy** — removed when orphaned,
concept-in-docs. browse-design also corrected its own stale backlog disposition (opacity is Cathode-value +
rasterizer-render, **no** litehtml fork).

**Deferred / on record (not regressions — the integration phase inherits them):**
- **Rendering** — both properties' rendering (HarfBuzz post-shaping advance; Blend2D group-alpha compositing) rides the
  integration-deferred `Dom→Layout→Text→Raster→Surface` pipeline (no production `ITextMeasurer`, no CathodeLayout→
  CathodeText driver, `FLayoutPaintOp` has no group-alpha field).
- **Own-inline-only** — a cascaded/`<style>`-sheet `letter-spacing`/`opacity` is not read value-side (litehtml discards
  them; the parse reads the element's own `style` attr). Guarded by by-construction **tripwires** (the cascaded-opacity
  test, letter-spacing's deferred-inheritance boundary), honest future-activation guards, not today-discriminating bites.
- **Gate-gap (candidate hardening):** the name-only Coherence test is structurally blind to a manifest↔mirror **status**
  divergence (it let slice-1's status drift ship in `48cc612`, caught manually at slice 2). A **status-lockstep
  assertion** (manifest status string ↔ mirror enum status) is the candidate hardening — carried to the PM as a focused
  follow-on, deliberately **not** bolted onto a value-side slice.

## Postmortem

**Lead (uptime.browse-lead).** Two directive premises were overturned by library-first audits *before* any build — the
gap re-vendor (no litehtml has flex-gap) and the rendering integration (the whole pipeline is deferred) — and the right
move each time was to surface the punctured frame up (the descoping, the scope reframe) rather than execute on it; a
confident directive met by source-grounded evidence is a fork to raise, not a plan to follow. Routing discipline held
the lanes: env *builds* (the PM corrected my "env feasibility gate" framing on the re-vendor — env builds, the version
decision routes up); design forks (inheritance, the declaration model, the BuildHowPending taxonomy) routed to
browse-design, not ruled from the lead seat — though the orphaned-status pattern split by character (the *gap-specific*
`BuildHowCommitted` I ruled remove myself; the *general-lifecycle* `BuildHowPending` I routed up). Scope discipline
mattered most at the gate-gap: a real gate weakness surfaced mid-slice, and the correct response was to track it as a
focused follow-on, not leak a gate hardening into the opacity value-side slice. Next sprint inherits: ground a
directive's *premise* at the source before routing the work; keep the value/rendering split explicit when a pipeline is
integration-deferred; and let a verification gap be closed manually until its hardening is its own scoped change.

**Dev (uptime.browse-dev, Mundo).**
1. **Library-first audit verifies the directive's premise at the source** — twice it overturned a premise before a
   build: the gap re-vendor (litehtml's `string_id.h` has no gap on v0.10 or master) and the slice-1 "measurement path is
   wired" assumption (it is an integration-deferred island). Ground the premise (not the changelog, not the framing); an
   audit that overturns a premise saves a wasted build/re-vendor.
2. **The value-side pattern generalized cleanly** — `CathodeComputed` + an on-demand own-inline parse in
   `QueryComputedValue` + the value crossing as an `FResolvedStyle` FString (preserving the FORK-1 firewall). opacity
   reused letter-spacing's shape verbatim; only the per-prop parse rule differed.
3. **Per-prop CSS specifics are past-shaped** — read the computed-value rule (MDN) first; it decides
   inheritance/initial handling *and* hands you the load-bearing discriminator (letter-spacing inherits → own-element
   partial; opacity unset → `'1'` correct; clamp `[0,1]`/`%` → specified≠computed, no echo).
4. **A status change can drift silently past a name-only gate** — slice 1's manifest status string lagged the mirror
   enum, invisible to the name-only Coherence test (→ the status-lockstep hardening candidate). Flag the gate-gap; don't
   bolt a gate onto a value slice.
5. **Honesty in the documented state** — name the partial (own-inline-only, not "fully CSS-correct"); frame a
   can't-fail-today boundary as a **tripwire** (future-activation guard, non-vacuous only via a sibling that bites), not
   a discriminating proof. Verification throughout: every green read from the settled logs myself, with the fresh-binary
   marker chosen by slice shape (behavior flip for a status-change, never the vacuous name-set cross-check).

**Review (uptime.browse-review).** The durable lesson is that **the reviewer's tools must match the change's shape.**
The fresh-binary coherence marker is the clearest case: a *removal* is proven fresh by the compiled-mirror-vs-on-disk
name-set cross-check, but a *status-flip* (name unchanged) is invisible to it and must gate on the behavior flip — the
wrong marker for the shape is a vacuous green. The recurring craft was the **non-vacuous negative**: a "deferred/absent"
assertion bites only with its positive on the same machinery (parent-reads-own alongside child-empty; the two-distinct-
values reframe after opacity's empty→value flip turned the old "both non-empty" contrast vacuous). The sharpest catch
was a **gate blind spot** — the name-only Coherence stayed green while slice-1's manifest status drifted, so I re-derived
the manifest↔mirror status manually and closed it (the status-lockstep is the candidate hardening). Next sprint's
reviewer inherits: pair every negative with its positive on the same setup; pick the fresh-binary marker by the change's
shape and reject the wrong one; re-derive manually whatever dimension an automated guard is structurally blind to until
it is hardened; and characterize a by-construction tripwire honestly as a future-activation guard, never a today bite.
