# Cathode — CSS Subsystem (detail)

The CSS subsystem is **mandated** (`spec-surface.md` Axis-3) and **defined by its consumer, the DOM**.
It was previously only deferred/stubbed/gated across Layout & Style + the DOM rather than staged as a
coherent subsystem; this doc is its design-of-record. What/why + the **interface**. The CSS coverage
extents are **settled** in `spec-surface.md` Axis-3; the *epic staging* is the PM's (see *Division*).

## Methodology — the DOM defines the CSS interface (consumer-driven)

The DOM is the **consumer that defines the CSS interface.** Its CSS interactions —
`GetDocumentCss`, `element.style` / `CSSStyleDeclaration`, `getComputedStyle` — *are* the CSS
use-case and surface. Even when a DOM slice **stubs** an interaction with a TODO, that stub is a
**decision about what CSS must expose** — it defines the DOM↔CSS cross-interface. So "we have no CSS
system/use-case yet" is answered *by the DOM telling us the use-case*: the CSS subsystem builds **to**
the interface the DOM's interactions define. (This is the consumer-driven-contract pattern — the
DOM-completion slices stub the surface; CSS builds to it. A stub **defines**, it does not **gate**.)

## The DOM↔CSS interface (the consumer-driven contract)

Grounded in the DOM's actual CSS touch-points (`CathodeDom` over `CathodeLayout`/005):

**1. Ingress — `GetDocumentCss` (DOM → CSS).** The DOM serializes the document's stylesheet text
(the `<style>` content) for litehtml to parse and cascade (`ILayoutDocumentSource::GetDocumentCss`,
`CathodeDomDocument.cpp:199`). *Status:* **stubbed empty** (COVERAGE #6 — "materializes in due
time"). *Owed:* the `<style>`/`<script>` **raw-text escaping exemption** (COVERAGE #7 — the serializer
escapes all text; CSS/JS raw-text must not be escaped). *CSS must:* accept the document's CSS text and
parse/cascade it.

**2. Read-back — `getComputedStyle` / `FResolvedStyle` (CSS → DOM).** The DOM reads the **bounded
resolved (cascaded) style** per element — scoped to the contract's CSS properties, not the full CSSOM
— backed by litehtml's resolved values (`FDomNode::GetComputedStyle` → 005's
`FLayoutResult.Geometry.Style`, `CathodeDomNode.cpp:475`). *Status:* **built** (observation-tier; off
the author surface). *CSS must:* expose the resolved/computed values for the contract's property set,
per element, as `FResolvedStyle`.

**3. Mutation — `element.style` / `CSSStyleDeclaration` (DOM → CSS, live).** Inline-style get/set,
`cssText`, `setProperty` — **live, write-through**: a write updates the element's inline style →
dirty-mark → (at relayout) serialize-to-markup → litehtml re-cascades. *Construction (ruled):*
**one** C++-backed live `CSSStyleDeclaration` type, **parameterized by backing** — *inline-write-through*
for `element.style`, *resolved-read-only* for `getComputedStyle` (writes are no-ops, per spec).
Open-ended **named access** (`style.color`, any CSS property) via a **C++-backed property handler**, not
a fixed per-property accessor set (that would couple the JS shape to the declaration). The **`style`
attribute is the single source of truth** for inline style; `element.style` is an *accessor over it*
(write-through + dirty-mark) and `cssText` operates on that canonical string — **no separate parallel
store to drift** (the dev may memoize a parse for perf as long as the attribute stays canonical).
*Supported-property validation* references **Cathode's CSS capability declaration** (the defined
contract — the CSS-epic's first step), **stubbed** provisionally (accept-any, seam defined, *no
hardcoded set*) until the declaration lands; 1-line-real when it does. *CSS must:* accept inline-style
mutations and re-cascade (the cascade itself is litehtml's, already wired).

**The pipeline these define:** `GetDocumentCss` (+ inline `element.style`) → litehtml **parse + cascade**
(in Layout & Style) → `FResolvedStyle` + geometry → `getComputedStyle` / relayout. CSS owns the parse,
the cascade, the resolved-value exposure, and the inline-mutation re-cascade.

## Stub boundary — the style-with-CSS-stubs slice (the consumer-driven CSS-epic contract)

The slice's stubs **define** the CSS-epic interface (the dev authors the gap-doc with the detail).
Ratified boundary:

- **BUILT (this DOM slice):** `element.style` as the live write-through `CSSStyleDeclaration` (→ the
  existing dirty-mark + serialize-to-markup → litehtml at relayout); `getComputedStyle` as the
  read-only `CSSStyleDeclaration` over `FResolvedStyle` (the read *path* works; **values empty** until
  the epic populates them — the downstream joint, 1-line-real on `QueryComputedValue`); the **stub
  operations** (supported-property validation, resolved read-back, `GetDocumentCss`).
- **STUBBED → the CSS epic:** the actual **cascade** + inline-mutation re-cascade, the **computed-value
  resolution** (`QueryComputedValue`), and the **CSS capability declaration** (the supported-property
  single source).
- **Proof bar (what "done" is here):** the members WORK (`style[k]=v` / `cssText` / `setProperty`
  round-trip the inline style; the inline write reaches the layout-feed markup; `getComputedStyle`
  returns a `CSSStyleDeclaration` reading `FResolvedStyle`, empty values = the stubbed cascade) AND the
  stub interface is COHERENT (the epic can build to it). The actual cascade is **not** proven here — a
  real-cascade proof IS the CSS epic.

## Axis-3 requirements (coverage / content)

- **litehtml parse + cascade** hosts the engine work (CSS 2/3, flexbox; no grid; partial CSS3) — see
  `components-layout-style.md`.
- **Cathode's CSS capability declaration** (authored from litehtml's *actual* coverage —
  `css_properties.h`/`types.h`, per the css-declaration grounding) is the **single source** for the
  supported-property set, referenced by all three: `element.style` (valid names), `getComputedStyle`
  (exposed props), and the build gate (the allow-list). Authored, not copied from panels.
- **`var()` / custom properties — REQUIRED** (the theming model is entirely `var(--…)`). litehtml
  support is the highest-risk coverage item — validate early.
- **`FResolvedStyle` is bounded** to the contract's properties (not the full CSSOM) — the read-back
  surface is the contract, not everything litehtml resolves.

## Relationship to existing components

- **Layout & Style** hosts the litehtml parse/cascade (the engine); the CSS subsystem is the *surface
  and coverage* around it, defined by the DOM's interface.
- **DOM** is the consumer — its CSS interactions define the interface (above); the DOM stubs them.
- **Authoring** holds the CSS declaration (the supported-property single source) + the gate's allow-list.
This subsystem ties those into one mandated, staged whole rather than CSS-as-deferred-fidelity.

## Division

- **Interface / design (this doc) — designer.** The DOM↔CSS contract + the Axis-3 requirements above.
- **Epic staging + sprint-order — PM.** This doc is what the CSS epic builds to; the PM stages it and
  sequences its place.

The CSS coverage **extents are settled** in `spec-surface.md` Axis-3 — the IN/OUT list IS the design,
not a standing decision bucket. `flex gap` is IN (implement, or fall back to margins — a build-how);
Grid / animations / transitions / transforms / shadows are OUT (a future change-trigger only if a real
consumer ever needs one, not an open fork).

## Stub-not-gate (reframe)

The prior "style is gated on the css-declaration" is corrected: `element.style` **stubs** the CSS
interface (defining the surface), it does not **gate** on CSS. The DOM-completion slices stub their CSS
interactions (TODO) — that *defines* the contract — and the CSS subsystem builds to it. Nothing in the
DOM completion waits on CSS; CSS waits on (builds to) the DOM-defined interface.

## Cross-refs

`spec-surface.md` (Axis-3) · `components-layout-style.md` (the cascade host) · `components-dom.md` /
`components-dom-surface.md` (the consumer + the `style`/`getComputedStyle` members, #8) ·
`components-authoring-contract-gate.md` (the gate's allow-list) · the css-declaration grounding
(litehtml `css_properties.h`/`types.h`).
