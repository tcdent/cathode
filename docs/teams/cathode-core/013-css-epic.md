# Sprint 013 — CSS Epic: build the real cascade to the DOM-defined stub interface

## Nature

The **cascade-build sprint** that pairs with 012's DOM-completion. 012 built the DOM surface and **stubbed**
its CSS touch-points; those stubs **define** the contract (`docs/browse/css-epic-contract.md`). 013 builds the
**real CSS cascade** *to* that committed interface — litehtml parses + cascades the document and inline CSS into
`FResolvedStyle`, `getComputedStyle` returns **real** computed values, and the supported-property set comes from
**Cathode's own litehtml-grounded CSS capability declaration**. Nothing in 013 re-opens the DOM surface; the
joints are **1-line-real** on the DOM side.

> "The DOM is the consumer that defines the CSS interface. Even when a DOM slice stubs an interaction with a
> TODO, that stub is a decision about what CSS must expose. The CSS subsystem builds **to** the interface the
> DOM's interactions define. A stub **defines**, it does not **gate**." (`components-css.md`.)

> **The gate flips.** 012 stubbed the cascade / empty values BY DESIGN; 013 BUILDS them — so the bar flips
> from "stub-coherent" to "values REAL against the primitive's (litehtml's) own output." An empty value that was
> correct in 012 is a **defect** in 013. (browse-review 012 close retro.)

## Scope Boundary

**This sprint owns:** the CSS cascade surface around the litehtml engine, defined by the DOM's interface —
- the **CSS capability declaration** (the supported-property single source, authored from litehtml's actual
  coverage), replacing the DOM's provisional ACCEPT-ANY validation;
- **`var()` / custom-property** support (the theming path — the highest-risk coverage item, validated early);
- **`GetDocumentCss` ingress** (the `<style>`-content collection + the raw-text serialization-escaping exemption)
  and the litehtml **parse + cascade** of the document + inline CSS;
- **`QueryComputedValue`** — the computed-value resolution that populates `FResolvedStyle` (the bounded contract
  property set) so `getComputedStyle` returns real values;
- the **inline-mutation re-cascade** (the `element.style` write path).

**This sprint does NOT own:**
- the **DOM surface** — 012 owns it; its stubs ARE the contract, joined 1-line-real (never re-open
  `element.style`/`getComputedStyle`/the `style`-attribute single-source-of-truth).
- the **litehtml engine** — `CathodeLayout`/005 hosts it (parse, cascade, layout, geometry). Extend its CSS
  wiring; do **not** rebuild the engine.
- the **OUT Axis-3 features** — Grid, animations/transitions/`@keyframes`/transforms, shadows/filters/gradients,
  `@layer`/`@property`/`@supports`/`@media`, attribute & sibling selectors, `oklch`/`lab`/`color-mix`. Settled OUT
  in `spec-surface.md` Axis-3 (a future change-trigger only, not an open fork).
- the **full CSSOM** — `FResolvedStyle` is **bounded** to the contract properties, not everything litehtml resolves.
- the **FORK-1 firewall** — `CathodeDom → litehtml` stays **absent** (litehtml is `CathodeLayout`-private behind
  the neutral seam, proven by-the-negative in 012). The cascade lives in `CathodeLayout`.

## Problem

The CSS interactions are **stubbed** (`css-epic-contract.md`), so the cascade is wired but fed empty and reads back
nothing:
- `QueryComputedValue(NodeId, property)` = `return FString()` (`CathodeLayoutEngine.cpp`) → `getComputedStyle(el).<prop>`
  reads `''` for every contract property.
- supported-property validation is **ACCEPT-ANY** — there is **no** CSS capability declaration (`Authoring/` is a stub).
- `FDomDocument::GetDocumentCss()` = `return ""` → the litehtml cascade is fed **empty** document CSS.
- `var()` / custom-property litehtml support is **unvalidated** (the highest-risk coverage item).

The DOM has defined the interface and proven the joints coherent (012 `style-with-CSS-stubs`); the cascade itself
is not yet built.

## Goal

The real CSS cascade is live to the committed interface: litehtml **parses + cascades** the document CSS
(`GetDocumentCss` `<style>` content) + inline `style` into `FResolvedStyle`; `getComputedStyle` returns **real**
computed values for the bounded contract property set; `element.style` mutations re-cascade; and the
supported-property set is **Cathode's own litehtml-grounded CSS capability declaration** (replacing ACCEPT-ANY).
Within the settled Axis-3 breadth (flexbox + block/inline, box model, color + `var()`, text, the listed selectors,
`px`/`%`/`em`/`rem`, `@font-face`); `var()` is **required**.

## Final State

1. **CSS capability declaration** (`Cathode/Authoring/`, authored from litehtml `css_properties.h`/`types.h` —
   never fabricated/hardcoded, never copied from panels): the single source for `element.style` valid names +
   `getComputedStyle` exposed props + the gate allow-list. The DOM's validation seam references it (1-line-real,
   replacing ACCEPT-ANY).
2. **`var()` / custom properties** resolve: `--custom` set via `setProperty`/`cssText` (carried through the canonical
   `style` attribute) + `var(--custom)` resolves to the value in the cascade. (litehtml support validated; a wall
   surfaced + escalated before depth, if any.)
3. **`GetDocumentCss` ingress**: `FDomDocument::GetDocumentCss()` collects the document's `<style>` element content
   (no longer `""`); the `<style>`/`<script>` raw-text escaping exemption is in the serializer (CSS/JS text is not
   HTML-escaped). litehtml parses + cascades it.
4. **`getComputedStyle` real values**: `QueryComputedValue(NodeId, property)` returns litehtml's cascaded value for
   each bounded contract property → `FResolvedStyle` is populated → `getComputedStyle(el).<prop>` reads the real
   computed value (the 012 downstream joint, rebound 1-line-real on the DOM read path).
5. **Inline-mutation re-cascade**: an `element.style` write re-cascades so `getComputedStyle` reflects it (the DOM's
   inline write already dirty-marks; the incremental re-cascade is built here, with the existing full-relayout
   re-parse as the meanwhile baseline).

## In Scope (ordered slices)

Risk-front-loaded; the PM's staged order is the sequencing anchor.

1. **The CSS capability declaration** — author Cathode's own supported-property single source from litehtml's actual
   coverage (`css_properties.h`/`types.h`), scoped to the Axis-3 IN set. Method: litehtml-grounded (enumerate
   litehtml's real coverage; the Axis-3 IN list is the breadth), never fabricated. Rebind the DOM's ACCEPT-ANY
   validation to reference it (1-line-real). *Parallel-available — depends on nothing downstream.*
2. **`var()` / custom-property feasibility** (the highest-risk — validate EARLY, armed tripwire): prove litehtml
   resolves `var(--x)` against a `--x` custom property in the cascade; if litehtml lacks `var()` support, surface the
   **wall** to the lead → human before the epic commits depth (do not invent a shim downstream). Method: a minimal
   real cascade with a custom property + `var()` read-back.
3. **`GetDocumentCss` ingress + the cascade** — `GetDocumentCss` collects the document `<style>` content; add the
   `<style>`/`<script>` raw-text escaping exemption to the serializer; litehtml parses + cascades the collected CSS +
   inline `style`. Method: a real `<style>` rule + a real layout pass → the rule participates in the cascade.
4. **`getComputedStyle` real values** — implement `QueryComputedValue` to map litehtml's resolved/cascaded values to
   the bounded contract property set in `FResolvedStyle`; `getComputedStyle(el).<prop>` returns the real computed
   value. Method: a real cascade (a `<style>` rule + inline `style`) → assert the computed value against litehtml's
   own resolved output (RELATIONAL invariants, never a hand-typed pixel).
5. **Inline-mutation re-cascade** — the incremental re-cascade on an `element.style` write so `getComputedStyle`
   reflects the mutation. Method: `el.style.<prop> = v` → re-cascade → `getComputedStyle(el).<prop>` === the
   cascaded result of `v`.

## Non-Goals

- Do **not** re-open the DOM surface (012) — its stubs are the contract; join 1-line-real (the read path already
  calls `GetComputedStyle`; the `style` attribute stays the single source of truth; do not add a parallel store).
- Do **not** build the OUT Axis-3 features (Grid / animations / transitions / transforms / shadows / filters /
  gradients / `@layer`/`@property`/`@supports`/`@media` / attribute & sibling selectors / `oklch`/`lab`/`color-mix`)
  — settled OUT; a future change-trigger only.
- Do **not** fabricate or hardcode the supported-property set — author it from litehtml's **actual** coverage.
- Do **not** expose the full CSSOM — `FResolvedStyle` is bounded to the contract properties.
- Do **not** rebuild the litehtml engine — `CathodeLayout` hosts it; extend its CSS wiring.
- Do **not** form a `CathodeDom → litehtml` dependency — the FORK-1 firewall holds (litehtml stays
  `CathodeLayout`-private behind the neutral seam); the cascade lives in `CathodeLayout`.
- Do **not** delete a fallback just because production does not hit it — trace callers and tests first (the
  full-relayout re-parse is the re-cascade meanwhile baseline until the incremental path lands).

## Starting Surface

Audit first:
- **litehtml's actual CSS coverage** (`css_properties.h`/`types.h`) — the source for the capability declaration + the
  `var()` support reality (the highest-risk).
- the **`QueryComputedValue` stub** (`CathodeLayoutEngine.cpp` = `return FString()`) + the `FResolvedStyle` shape
  (the bounded contract property set it must populate).
- the **`GetDocumentCss` stub** (`CathodeDomDocument.cpp` = `return ""`) + the existing cascade wiring (litehtml
  already parses `GetDocumentCss` + inline `style`, fed empty) + the serializer's all-text-escaping (the raw-text
  exemption owed).
- the **DOM joints** (012): `getComputedStyle → GetComputedStyle → FResolvedStyle`; the ACCEPT-ANY validation seam;
  the `element.style` dirty-mark.

Inventory shape: `stub (today) → the real operation (the epic builds) → the DOM joint (1-line-real) → litehtml site`.

## Method

- **The gate flips — REAL against the primitive's own output.** Each operation DISCHARGES its 012 stub: an empty/
  ACCEPT-ANY/`""` value correct in 012 is a **defect** here. Drive the **real** cascade (a real `<style>` rule + a real
  layout pass via the real litehtml engine, not a stub) and assert the computed value against **litehtml's own resolved
  output** — RELATIONAL invariants, never a hand-typed pixel (the 012 DOMRect/T3 oracle discipline).
- **litehtml-grounded, never fabricated.** The capability declaration enumerates litehtml's actual coverage; the
  cascaded values come from litehtml's resolution. Do not invent a property set or a value.
- **Front-load the `var()` risk + arm the tripwire.** Validate `var()` litehtml support EARLY (slice 2); a support
  wall surfaces to the lead → human **before** the epic commits depth — do not invent a downstream shim.
- **The joints rebind 1-line-real on the DOM side.** `QueryComputedValue` lands → the DOM's existing read path
  returns real values; the capability declaration lands → the ACCEPT-ANY seam references it. Confirm the DOM side is a
  rebind, not a re-open.
- **The FORK-1 firewall holds.** The cascade is `CathodeLayout`-side; `CathodeDom → litehtml` stays absent (prove
  by-the-negative: a litehtml change does not relink `CathodeDom`). Coherence on the cross-module aggregate buildId.
- **The authority boundary is the epic's** (cascade authority, the supported-set source/granularity, the
  computed-value granularity, the `<style>`-ingress shape) — but a genuine design fork still routes up (browse-design
  / human), never resolved downstream.

## Acceptance

- **`getComputedStyle` returns real values:** a real cascade (a `<style>` rule + inline `style`) over the real
  litehtml engine → `getComputedStyle(el).<prop>` === litehtml's resolved value for the bounded contract properties
  (RELATIONAL, not hand-typed); an empty value is a **defect**.
- **`var()` resolves:** `--custom` (set via `setProperty`/`cssText`, carried through the canonical `style` attribute)
  + `var(--custom)` → the resolved value in the cascade. (Or the feasibility wall escalated, with the human's ruling.)
- **The capability declaration is the source:** the DOM's validation references the authored declaration (not
  ACCEPT-ANY); the set is authored from litehtml's actual coverage (`css_properties.h`/`types.h`), never fabricated;
  it feeds `element.style` valid names + `getComputedStyle` exposed props + the gate allow-list.
- **`GetDocumentCss` ingress:** a document `<style>` rule is collected (`GetDocumentCss` ≠ `""`) and cascades; the
  `<style>`/`<script>` raw-text escaping exemption is in the serializer (CSS text round-trips un-escaped).
- **Inline re-cascade:** `el.style.<prop> = v` → `getComputedStyle(el).<prop>` reflects the cascaded `v`.
- **Axis-3 breadth:** the IN features cascade (flexbox + block/inline, box model, color + `var()`, text, the listed
  selectors, the listed units); the OUT features are not built.
- **FORK-1 firewall + no 012 regression:** `CathodeDom → litehtml` absent (by-the-negative); the full 011+012 DOM/
  runtime/faculty suites stay green; the DOM joints rebind 1-line-real (no re-open). Coherence per-run on the aggregate
  buildId.

## Review Questions (open forks)

1. **`var()` litehtml support** (the highest-risk): does the vendored litehtml resolve `var()`/custom properties, or
   is a fallback/shim needed? Validate in slice 2; a wall is the human's call (browse-design / human), not a downstream
   invention.
2. **The capability-declaration exact set** — authored from litehtml's actual coverage scoped to the Axis-3 IN set;
   browse-design (Authoring) owns the source/granularity. Confirm the litehtml-grounding (never fabricated).
3. **Computed-value granularity / normalization** — which properties `QueryComputedValue` exposes and how values are
   normalized (e.g. resolved vs specified, units) is the epic's call (browse-design); confirm the bounded set.
4. **The `<style>`-ingress shape** — how `GetDocumentCss` collects `<style>` content + the escaping exemption scope;
   the epic's call (browse-design).
5. **Flex `gap`** — implement in litehtml or fall back to margins (a build-how per Axis-3, the dev's how within the
   IN coverage).

## Outcome

The 5-slice CSS arc is delivered, each slice independently re-derived + accepted at its boundary and committed scoped
per-slice (PM per-slice cadence):

- `4cedfea` — **S1** the CSS capability declaration (litehtml's `string_id.h` name-table minus 7 audited, source-cited
  exclusions = 108) + the `element.style` accept-and-ignore gate (membership-keyed; the 113 mirror = 108 present + 5
  build-how-pending; custom `--*` props always valid).
- `97c0fdc` — **S2** `var()` feasibility, the highest risk, retired green (the vendored litehtml's `subst_vars`
  resolves `var()` over the real cascade; relational oracle, no wall).
- `aa5c1d1` — **S3** the `<style>`/`<script>` raw-text serialization exemption; ingress is path-(i) (litehtml's own
  `<style>` extraction from the serialized HTML), `GetDocumentCss` stays a wired stub.
- `8709435` — **S4** `getComputedStyle` real values via `QueryComputedValue` (CSSOM resolved-value forms over the
  18-prop contract set: box-model used-px from `get_placement`, css()-computed edges, the `get_font()` handle-read).
- `cddf1d8` — **S5** inline re-cascade via a lazy flush-on-read (`getComputedStyle` flushes pending layout, reusing
  the existing full-relayout Controller).

**Review Questions resolved:** (1) `var()` supported (S2, no wall); (2) capability set = litehtml's name-table minus
exclusions, the **inverted** method, not a member-filter (browse-design re-ratified); (3) computed-value granularity =
CSSOM resolved-value forms over the 18-set (browse-design Fork-A + the font-pair `b1` read-via-handle ruling); (4)
`<style>`-ingress = path-(i) serialized-HTML extraction + the `{style,script}` exemption (browse-design Fork-1/Fork-2);
(5) flex `gap` = build-how-pending (deferred).

**On-record deferrals & limitations** (carry-not-rediscover): fold-when-touched — the exact-boundary `</style>`/
`</script>` assert, the stale manifest `[css]` comment, the setProperty-declared-cell matrix completion; documented
in-code limitations — margin/padding/border-width return the css()-computed form for `%`/layout-resolved (the used
edges are on litehtml's private `render_item`), and font-family returns the resolved face not the CSSOM computed-list;
the subtree-path-activation **dual-trigger contingency** (reuse stays correct while attr/sibling selectors are OUT AND
the Controller relayouts full). The orphaned slice-3 `StyleIngressProbe` was retired (redundant — subsumed by
`StyleRuleCascade`).

## Postmortem

**Lead (uptime.browse-lead).** The loop held by routing every fork to its owner before depth — design shape to
browse-design, technical re-derivation to review, the design-model calls to tcdent — and refusing to advance on a
green that wasn't discriminating. The single highest-value move was running the FULL `Cathode.Dom` regression before
committing S1's gate-flip: review's by-inspection ACCEPT was real on the slice's own tests, but the shared
`SerializeNode` path had silently regressed `cssText` serialization — caught only by the empirical run, fixed
filter-only with the breaking geometry self-guarded, and captured as a durable memory (a behavior change to a shared
path must RUN the existing path-guards, never be inspected). Holding the dev's scaffold-the-mechanism /
hold-the-forked-part through the oracle's R1→R3 refinements, the two infeasibility waves, and the Fork-2
ruling-premise reframe bought ZERO rework, because no wrong answer was ever locked. A commit-scope oversight (an
orphaned S3 probe, untracked but in the build) was caught at the commit and routed to a redundancy verdict rather than
papered. The next lead inherits: gate the shared-path regression EMPIRICALLY before a behavior-change commit; hold the
scaffold through forks; route a ruling whose premise doesn't match the code back to the design owner before building;
verify the staged file list against the slice's intent at every commit; and the commit cadence/approval is the PM's
call, not the human's.

**Dev (Mundo).** The build delivered the CSS surface against a VENDORED litehtml whose internals are mostly
non-readable, and the recurring lesson is: DERIVE FROM THE DEPENDENCY'S OWN AUTHORITATIVE SURFACE, never reconstruct
from a downstream artifact. The capability set is litehtml's `string_id.h` name-table minus 7 audited exclusions —
NOT a member-filter, which silently drops shorthands (`flex-flow`) whose logic lives in the non-vendored `.cpp`;
computed values come from the typed getters / a handle-captured font, never the specified form. What changed the plan,
every time, was READING litehtml's headers BEFORE building — the box-model used-edges (`render_item`-private) and the
font-family/weight pair (member-only/no-getter, my own S1 R2 finding connected) were genuine infeasibilities I
surfaced (and corrected my own undercount, 15→13→16) before building, so the design rulings stayed correct with ZERO
rework; the Fork-2 reframe (the ruling assumed an incremental subtree path; the reality is root-only-boundaries → full
relayout) was the same discipline applied to the re-cascade mechanism. The near-slip was the S1 `cssText`
serialization regression — invisible by-inspection (the accept-and-ignore filter looked input-safe) but caught by the
pre-commit FULL regression run; fixed filter-only (drop unsupported, never re-serialize) with the breaking geometry
self-guarded in-slice. NEXT dev inherits three habits: prove RELATIONALLY (assert against the dependency's OWN
computation; on a mismatch true up only to a relationally-derivable expected, never tune-to-output); build
DISCRIMINATING geometry into every proof (specified≠computed / pre≠post, so it bites a wrong-source read or a missing
flush, not just a stub); and run the FULL shared-path regression EMPIRICALLY when touching a shared path, because
by-inspection "no regression" is exactly how the `cssText` break would have shipped.

**Review (uptime.browse-review).** The review through-line was one rule: a gate passes only when its oracle BITES the
failure it claims to exclude — a green reachable WITHOUT the behavior under test proves nothing. Holding that turned
five would-be-vacuous gates discriminating, via proof-geometry co-designed at pre-stage: specified≠computed inputs so a
wrong-SOURCE read fails (not just the empty stub), both-ways negative-scoping on the SAME escaping-requiring content,
font-family relational-to-capture (env-independent), and S5's `b1` as an honest empirical future-activation tripwire —
while DECLINING to gate the non-mandated if-dirty skip (discipline cuts both ways). The sharpest lesson is a near-slip
I OWN: my S1 by-inspection "no regression" ACCEPT missed a real `cssText`-serialization regression on the shared
`SerializeNode` path — the lead's belt-and-suspenders full-suite run caught it, proving a behavior change to a shared
path must RUN the existing path-guards, never be inspected (input-validity ≠ the-path-preserves-it). Two techniques
earned their keep: proving a module firewall SOURCE-DISPOSITIVELY (zero litehtml dep/include in CathodeDom — stronger
and more general than the build-box recompile-set, which I flagged as inference I could not re-derive in-tree), and
inverting a fragile oracle at its root (the header member-filter silently drops every shorthand → names-minus-a-closed-
exclusion-list). The next reviewer inherits: make every oracle discriminating; RUN the shared-path guards rather than
inspect them; flag build-box evidence as inference, not proof; and route a contract-form delta to the human via the
lead rather than bless it downstream.
