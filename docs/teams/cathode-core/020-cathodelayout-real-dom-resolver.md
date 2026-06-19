# Sprint 020 — CathodeLayout: litehtml on the real Resolver

Wire the **real Resolver** (`FUEFileBundleSource`, 019) into a real litehtml document and prove a
**linked/external CSS resource resolves through it into the cascade**: a linked-CSS fixture →
litehtml `import_css` → the real `FUEFileBundleSource` → litehtml cascades the linked rule →
`getComputedStyle` reflects the **linked value**. The "real Resolver" half of "litehtml on the real Dom
and Resolver" — the 019 deferred container-wiring, now real (test-wired).

> Scope from the PM (tcdent-approved, run-through cadence): originally "CathodeLayout — litehtml on the
> real Dom and Resolver. Prove the DOM-to-CSS round-trip on real data: getComputedStyle returns real
> computed values and inline re-cascade works." **Reshaped (PM-ratified A1):** the inline/getComputedStyle
> round-trip is already proven (premise correction below); 020 is the genuine undone half — the real-Resolver
> integration, test-wired. "A1 approved. Re-proof is the reject. Production resolver wiring (A2) is the
> explicit next integration sprint."

## Scope Boundary

**This sprint owns** binding the real `FUEFileBundleSource` (019) behind `SetResourceResolver` on a real
litehtml document (test-wired, a test RootDir), and proving a **linked/external CSS resource** resolves
**through** the real resolver into the litehtml cascade — `getComputedStyle` reflects a value that came
from the external stylesheet, not the element's inline.

**Premise (the 017/018/019 discipline — dev-grounded + reviewer-independently-confirmed, with citations):**
the **inline / `getComputedStyle` round-trip is ALREADY PROVEN** over the real `FDomDocument` + real
litehtml, so re-proving it is **the reject**:
- the real-Dom adapter is **built** — `FDomDocument` *is* the `ILayoutDocumentSource` impl (CathodeDom.h:253,
  7 seam methods overridden), self-wiring `Layout.SetDocument(*this)` in its ctor (CathodeDomDocument.cpp:27);
  `Relayout` drives litehtml via the real adapter (the HTML re-serialization → `createFromString` **is** the
  adapter contract — litehtml is createFromString-based by design, not a bypass).
- `getComputedStyle` real values: 9 round-trip test files over a real `FDomDocument` + real litehtml,
  cascade-computed discriminating values (`50%`/200px→`100px`, `0.1em`/16px→`1.6px`, `#ff0000`→`rgb(...)`) —
  green.
- inline re-cascade: `CathodeDomReCascadeTests` (real `FDomDocument`, the pre/post pair via flush-on-read,
  `100px`→`80px` + inherited) — green.
- COVERAGE:22 "source impl sibling-missing (deferred)" is **stale** (the sprint-005 scaffold note,
  pre-CathodeDom) — **destale at close**.

The **genuine undone gap:** `SetResourceResolver` (CathodeLayoutEngine.cpp:580) has **no code caller**
(null in production); the resolver is **off the inline round-trip** (consumed only by
`load_image`/`get_image_size`/`import_css` — images + linked CSS, never inline style — CathodeLayoutEngine.cpp:204/214/257;
the cascade is resolver-free). Nothing wires a real `FUEFileBundleSource` into a real litehtml document; no
test proves a real **external** resource resolving through the real resolver into the cascade.

**Out of scope (named):**
- **A2 — production resolver wiring** (FDomDocument/engine self-wires the real resolver with
  `ProjectContentDir()/Cathode/`): the **explicit next integration sprint**. It pulls in the 019 coupled
  item (the production default-root + `DirectoriesToAlwaysStageAsUFS` staging + the cooked-pak validation =
  a shared `.uproject` change = a tcdent tooling gate). **Deferred — NOT 020.** 020 is **test-wired**: no
  production wiring, no production default-root, no `.uproject`/staging change; the 019 defer-both-together
  stays deferred.
- **The inline / `getComputedStyle` round-trip** — already proven; re-proving it is the reject.
- **Image resource resolution** (`load_image`/`get_image_size`) — *open*: is the linked-CSS path enough for
  "the real Resolver," or is an image-bytes path also in 020? The dev grounds (likely linked-CSS is the
  cleanest single proof; image-size is a heavier intrinsic-geometry concern).
- **No rasterization/paint; no CSS doc-gaps (grid).**

## Problem (to ground at the audit)

The resolver resolves real bytes (019) but `SetResourceResolver` is never called — the resolver is unwired
into any real litehtml document, and no test exercises an external resource through it into the cascade. The
dev's audit grounds the **`import_css` trigger shape**: how a linked stylesheet reaches litehtml's
`import_css` (an `@import` in a `<style>` vs a `<link rel=stylesheet>`), whether `FDomDocument`'s
`GetDocumentHtml`/`GetDocumentCss` serializes it so litehtml triggers the resolver, and the
`RouteLinkedCss` → `Resolver->ResolveText/Bytes` path (CathodeLayoutEngine.cpp:257).

## Goal

A real `FUEFileBundleSource` is bound (test-wired) on a real litehtml document, and a linked/external CSS
resource resolves **through** it into the cascade: a linked-CSS fixture (a distinct, cascade-computed rule)
→ litehtml `import_css` → the real resolver reads the fixture → litehtml cascades the linked rule →
`getComputedStyle(el).<prop>` returns the **linked value** — distinct from the unlinked default, non-echoable
(in the external CSS, not the inline). A **null/stub resolver** → the rule never loads → the default → RED.

## In Scope (ordered slices — dev-shaped; may collapse given the small gap)

1. **Bind the real resolver + prove the linked-CSS round-trip** — the test wires
   `SetResourceResolver(CreateResourceResolver(CreateUEFileBundleSource(<test-fixtures-dir>)))` on a real
   `FDomDocument`'s litehtml engine, with a committed linked-CSS fixture (the 019 `GetBaseDir`/Tests
   convention, byte-stable). A real document referencing the linked stylesheet → litehtml `import_css` → the
   real `FUEFileBundleSource` reads the fixture → litehtml cascades the linked rule →
   `getComputedStyle(el).<prop> ==` the **linked value** (distinct + cascade-computed, e.g. the external rule
   sets a `%`/`em` resolved against the container/font, or a distinct px). A null-resolver control → the
   rule never loads → the default → RED. (The dev grounds the `import_css` trigger shape first; the slice
   may split bind / prove if the grounding warrants.)

## Non-Goals

- No A2 (production resolver self-wiring + the 019 default-root/staging coupling) — the next integration
  sprint (a tcdent tooling gate).
- No re-proof of the inline/`getComputedStyle` round-trip or the cascade/re-cascade logic (already proven).
- No `.uproject`/staging change; no production default-root.
- No rasterization/paint; no CSS doc-gaps.

## Starting Surface (the audit the dev produces)

`SetResourceResolver (cpp:580, no caller) → the import_css trigger shape (@import in <style> vs <link>;
GetDocumentHtml/GetDocumentCss serialization) → RouteLinkedCss → Resolver->ResolveText/Bytes (cpp:257) → the
real FUEFileBundleSource (019) → litehtml cascades the linked rule → getComputedStyle == the linked value`.
Audit targets: CathodeLayoutEngine.cpp (`SetResourceResolver`, `RouteLinkedCss`, `import_css`),
`CreateUEFileBundleSource`/`CreateResourceResolver` (CathodeResolver, 019), the linked-CSS fixture (the 019
GetBaseDir/Tests convention), `FDomDocument` `GetDocumentHtml`/`GetDocumentCss` (CathodeDom). COVERAGE.md
destale at close (CathodeLayout:22; the real-resolver bind recorded; A2 the deferred next integration).

## Method

Test-wire the real resolver (`CreateUEFileBundleSource(<test-fixtures-dir>)`) on a real `FDomDocument`'s
litehtml engine; drive a real document with a linked stylesheet → litehtml `import_css` → the real resolver
reads a **committed, byte-stable** linked-CSS fixture (the 019 single-line/no-newline + scoped
`.gitattributes -text` discipline for any byte-exact aspect; the linked-CSS fixture's checkout stability) →
litehtml cascades the linked rule → assert `getComputedStyle == the linked value` (distinct, cascade-computed,
non-echoable). A **null-resolver control** → default → RED (the discriminator: the linked value comes from
the external fixture through the real resolver, not inline/default). Run the FULL CathodeLayout (+ any
touched-module) suite green; the existing round-trip logic tests stay. Coherence + the authoritative-run
discipline (incl. the two-same-result-logs gotcha if a sharpening crosses a build).

## Acceptance (reviewer-formulated, A1)

- **Real resolver bound + linked-CSS round-trip:** a real `FUEFileBundleSource` (test RootDir) bound via
  `SetResourceResolver` on a real litehtml document; a linked-CSS fixture → `import_css` → the real resolver
  → litehtml cascades the linked rule → `getComputedStyle(el).<prop> ==` the **linked value** (distinct,
  cascade-computed, non-echoable — in the external CSS, not the inline).
- **The null-resolver control:** a null/stub resolver → the rule never loads → the element gets the default
  → RED (the discriminator that the linked value genuinely came through the real resolver).
- **Test-wired (A1):** no production `SetResourceResolver` caller, no production default-root, no
  `.uproject`/staging change — the 019 defer-both-together stays deferred.
- **No regression:** the full CathodeLayout (+ touched-module) suite green; the existing round-trip logic
  tests stay.
- **Coherence:** the round-trip test bites a null/stub resolver (default → RED); the touched TUs recompiled;
  the authoritative run (the two-same-result-logs gotcha if a sharpening crosses a build).
- **COVERAGE destale at close** — CathodeLayout:22 (the real-Dom source built, not sibling-missing); the
  real-resolver bind recorded; A2 the deferred next integration.

## Review Questions (resolved at formulation; one open grounding)

1. **The inline round-trip** — RESOLVED: already proven (premise); re-proving is the reject.
2. **The wiring shape** — RESOLVED (PM-ratified A1): test-wired (test RootDir); A2 (production wiring + the
   019 coupling/tooling gate) is the deferred next integration sprint.
3. **The `import_css` trigger shape** — OPEN (dev grounds at slice 1): `@import` in a `<style>` vs a
   `<link rel=stylesheet>`, and whether `GetDocumentHtml`/`GetDocumentCss` serializes it so litehtml triggers
   the resolver. The mechanism the proof exercises.
4. **Image path vs linked-CSS** — likely linked-CSS is the single cleanest proof; the dev confirms whether an
   image-bytes path is also in 020 or deferred.

## Outcome

020 wired litehtml to the **real Resolver** and proved a **linked/external CSS resource resolves through it
into the cascade** — the genuine undone half of "litehtml on the real Dom and Resolver," after the premise
reshape established the "real Dom + round-trip" half was already done.

**Premise reshape (PM-ratified A1, the 017/018/019 discipline at its limit):** the PM's stated proof
("getComputedStyle real values + inline re-cascade on real data") was **already proven** over the real
`FDomDocument` + real litehtml — the real-Dom adapter is `FDomDocument` itself (CathodeDom.h:253, self-wired,
Relayout-driven; COVERAGE:22's "sibling-missing" was a stale sprint-005 note), and the round-trip tests are
green (013/014). The dev grounded it, the reviewer independently re-derived all six claims, and the reshape
went to the PM as a scope finding (not a re-proof). The PM ratified A1: 020 = the genuine undone half, the
real-Resolver bind, **test-wired**.

**Commit:** `614be98` — **the linked-CSS round-trip (slice 1, test + fixture + a test-only Build.cs dep, no
production change).** A real `FDomDocument`'s `<style>@import url('linked.css')` drives litehtml's own parse
→ `import_css` → `RouteLinkedCss` → the real `FUEFileBundleSource`'s `FFileHelper` read of the committed
external `linked.css` → the cascade → `getComputedStyle(#cathode-020-target).color ==` the linked value
`rgb(255,0,255)` (distinct, non-echoable). The **null-resolver control pair** is the discriminator: real →
the linked value; null → `RouteLinkedCss` false → the rule never loads → the default (asserted distinct +
non-empty). The close: this doc's Outcome + Postmortem and the CathodeLayout COVERAGE destale.

**Acceptance met:** litehtml's genuine `import_css` fires the real resolver's read of the external fixture →
the linked value through the real resolver (the null-control pair); test-wired (no production change, no
`GetDocumentCss` impl, the Build.cs dep non-Shipping); the CathodeDom binary recompiled + clean-linked
**independent of the agency S13.1 LNK1120** + marker green on a green tree (114/114, Dom 48→49); the existing
round-trip + routing suites intact. **A2** (production resolver wiring + the 019 default-root/staging/cooked
coupling) is the deferred next integration sprint (a tcdent tooling gate).

## Postmortem

**Lead (uptime.browse-lead).** This was the loop's highest-leverage sprint of the run, and the leverage was
in *not building*. What arrived framed as a build was ~80% already done; the team's premise audit caught it,
and the discipline held at every step — the dev grounded the already-proven half, I had the reviewer
independently re-derive the premise before I raised anything upward (working it out with the team first),
and the genuine fork (A1 test-wired vs A2 production-wiring with its 019 tooling-gate consequence) reached
the PM framed answerably rather than being settled downstream. The PM ratified A1, and the real gap was
built against a genuine vendor trigger (not the existing hand-call coverage). My one job at the gate was to
hold the cross-team coherence honest on a red shared-build — and the dev + reviewer did that re-derivation
themselves. The premise-reshape-to-the-PM (not a re-proof) is the durable process artifact here.

**Dev (uptime.browse-dev, Mundo).**
1. **The premise audit found the stated proof already done → the genuine gap was the other half.** The
   real-Dom adapter is `FDomDocument` itself (not a missing sibling — COVERAGE:22 stale); getComputedStyle +
   re-cascade green over the real FDomDocument. Grounding the actual state re-pointed the sprint to the
   real-Resolver bind and saved a re-proof of 014.
2. **Prove an integration via the engine's OWN path, not a hand-call to the routed function.** litehtml's own
   parse must fire `import_css` (createFromString over the real document's serialized `<style>@import`); a
   C++ direct `RouteLinkedCss`/`import_css` call is the existing routing coverage, not the gap.
3. **A cross-team shared-build authoritative run is re-derived per module: an overall-FAILED pass can still
   carry your module clean-linked** (UE links modules independently; the agency LNK1120 is a separate module;
   freshness from the additive marker; the green tree from target-up-to-date + the test-count).
4. **A prebuilt-vendor behavior-inference is empirically confirmed by a discriminating test** — the
   corroborated-inference trigger (upstream v0.7) is converted to empirical proof by the null-control pair
   (a non-firing trigger → real==null==default → RED); flag it as corroborated-inference-empirically-confirmed.
5. **Separate the test-wired proof from the production wiring (the deploy-coupling)** — A1 proved the data
   path test-wired (no `.uproject`/staging change); A2 (production wiring + the 019 coupling) is the deferred
   next sprint shipped together.

**Review (uptime.browse-review).** The premise-reshape is the 017/018/019 discipline at its limit: when the
stated gap collapses to already-done (six claims re-derived from source), escalate the reshape to the PM —
don't re-prove — and the surviving gap becomes the sprint. The **cross-team red-module coherence** was the
sharpest gate: on a shared editor target with the agency team's UptimeAgencyWorld red (LNK1120), my CathodeDom
coherence is separate and *provable* — (1) CathodeDom linked clean (UE links modules independently); (2)
freshness from the additive marker (`LinkedCssRoundTrip` present+Success), explicitly **not** "Target is up to
date" (the UBT-silent-on-per-TU-skips gotcha); (3) the green tree from the run's test-count (114 performed →
the editor loaded → all modules linked → the agency failure fixed by run time; a still-red tree can't load
the editor). The other team's failure being separate is proven, not assumed. The **genuine-trigger** rule: a
vendor-mechanism-fires proof uses the vendor's own trigger (a hand-call is the existing coverage),
empirically confirmed by the discriminating pair. Next reviewer inherits: when the stated gap is already
proven, escalate the reshape (don't re-prove); on a shared editor target with another team's red module,
prove your clean link + freshness-from-the-marker + green-tree-from-the-run-count (never assume separateness);
a vendor-fires proof uses the vendor's own trigger, confirmed by the discriminating pair.
