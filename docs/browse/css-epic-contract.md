# CSS-epic contract (the consumer-driven interface the DOM defines, the CSS epic builds to)

Authored by the DOM's **style-with-CSS-stubs** slice (browse 012). The DOM does NOT gate on CSS — it **stubs** the
CSS touch-points, and those stubs **define** this contract. The CSS epic builds TO it; nothing in DOM-completion
waits on CSS (`components-css.md:85`). This is the named gap-doc for the cross-area (DOM → CSS) seam.

Reconcile with `components-css.md` (the CSS design-of-record: the Mutation seam + the Stub-boundary section
browse-design ratified) and the two standing joints this consolidates (same joints, consistent naming):
- the **011 dormant CSS axis** — `CathodeLayout/COVERAGE.md` #2: `QueryComputedValue` deferred, re-tied to
  "Cathode's own litehtml-grounded CSS declaration (Authoring/, roadmap)";
- the **slice-5 `QueryComputedValue` stub** — `CathodeLayoutEngine.cpp` `QueryComputedValue` = `return FString()`.

## What the DOM BUILT (this slice) — the stubs that define the contract

- **`element.style` → a LIVE write-through `CSSStyleDeclaration`** (a C++-exotic class; open-ended named property
  access via the quickjs `get_own_property`/`set_property` handler; `cssText` get/set; `setProperty(name,value)`).
  The **`style` attribute is the single source of truth** — `element.style` is an accessor over it (named writes,
  `cssText`, `setProperty` parse/serialize the canonical `style` attribute via `SetAttribute`, which dirty-marks
  Layout). No parallel store. The inline write reaches the layout-feed markup (serialize → `style` attr → litehtml
  at relayout).
- **`getComputedStyle(elt)` → a read-only `CSSStyleDeclaration`** over `FResolvedStyle` (`FDomNode::GetComputedStyle`
  → `LastLayout.Geometry[].Style`). The read **path** exists and is wired; writes no-op (read-only per spec).
- **Supported-property validation: ACCEPT-ANY** (provisional, no hardcoded set) — the validation seam references
  "the CSS capability declaration"; it is **1-line-real** when the declaration lands.
- **`GetDocumentCss`-the-STUB (the `<style>`-ingress SEAM)** — `FDomDocument::GetDocumentCss()` is wired end-to-end
  (the litehtml consumer parses/cascades whatever it returns) and returns `""` today. It is a **built stub operation
  of this slice** (per browse-design's stub-boundary (d)) — the seam that **defines** the `<style>`-ingress contract;
  the actual `<style>`-content *collection* is the epic (step 4 below).

This slice's **built stub operations** (the built side of the stub boundary): validation ACCEPT-ANY + the resolved
read-back (`getComputedStyle` → `FResolvedStyle`) + `GetDocumentCss`-the-stub (the `<style>`-ingress seam, returns
empty).

## What the CSS epic must BUILD (the contract — stubbed/deferred here)

1. **`QueryComputedValue(NodeId, property)` — the computed-value resolution.** Today `return FString()` →
   `getComputedStyle(el).<prop>` reads `''` for the bounded contract properties. The epic populates `FResolvedStyle`
   with litehtml's cascaded values (the litehtml-typed → name-keyed-string mapping that does not exist yet). This is
   the **downstream joint**: 1-line-real on the DOM side (the read path already calls `GetComputedStyle`) when the
   epic implements `QueryComputedValue`. Granularity (which properties, value normalization) is the **epic's** call —
   the DOM does not set it.
2. **The cascade + inline re-cascade.** litehtml parses + cascades `GetDocumentCss` + inline `style` (already wired;
   fed empty CSS today). The DOM's inline write dirty-marks; the **incremental re-cascade** on a style mutation is the
   epic's (the DOM relies on the existing full-relayout re-parse of the `style` attribute meanwhile).
3. **The CSS capability declaration — the supported-property single source.** Authored from litehtml's actual
   coverage (`css_properties.h`/`types.h`), feeding `element.style` valid names + `getComputedStyle` exposed props +
   the build-gate allow-list. NOT BUILT. The DOM's validation is ACCEPT-ANY until it lands. **Authority/source of the
   set is the epic's (browse-design Authoring) — the DOM must NOT fabricate or hardcode a set.**
4. **`GetDocumentCss` ingress (CSS epic step 3) — defined-not-built here.** `FDomDocument::GetDocumentCss()` returns
   `""` today (stub); the epic collects `<style>` element content from the DOM and feeds it to the cascade (the
   litehtml consumer side is wired). The **`<style>`/`<script>` raw-text serialization-escaping exemption** lands as
   an early CSS-epic build step (the serializer HTML-escapes all text today, which would corrupt collected CSS).
   This is the epic's step 3, NOT a DOM-completion slice.

## Authority boundary (do NOT resolve downstream)

Any decision on **cascade authority**, the **supported-set source/granularity**, the **computed-value granularity**,
or the **`<style>`-ingress shape** is the CSS epic's (browse-design / human) — the DOM defines the *operations*, not
their *authority*. Coverage extents are settled in `spec-surface.md` Axis-3 (flex gap IN; grid/animations/transitions/
transforms/shadows OUT); `var()`/custom properties are REQUIRED (the theming path; the DOM's `setProperty` carries
them through to the canonical `style` attribute).
