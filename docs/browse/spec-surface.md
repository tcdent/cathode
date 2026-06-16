# Cathode — Spec Surface (the supported subset)

The authoritative definition of the web-platform subset Cathode intends to support. **Intent of
record:** this is the target the implemented interface conforms to; the `.d.ts` and the build gate
are *generated from the interface*, and this doc is what says whether the interface is complete or
has gaps. It supersedes the scattered "draft list"; `panelkit/SURFACE.md` is *input evidence* (what
real panels call), and Cathode's own CSS capability declaration (`Authoring/`, authored from litehtml's
coverage) is the machine-checkable CSS form — **not** panels' `css-subset.json`.

> **DOM axis authority:** the member-by-member DOM surface is **not** defined here — it lives in
> [`components-dom-surface.md`](components-dom-surface.md) (spec-exact signatures, Preact-measured
> scope, behavioral contract). This doc carries only the *principles* for that axis, to keep one
> source of truth. Axes 1 (JS) and 3 (CSS) are defined here.

## Principles

- **Standard names and semantics, always — no Cathode-isms.** We adopt members from the web specs;
  we never invent names. This is load-bearing: it's what lets real Preact/React/Tailwind output run
  unmodified. `tagName` vs `nodeName` etc. are distinct *spec* members, not naming choices.
- **Three axes, three standards.** The "web platform" was never one spec:

  | Axis | Standard(s) | Realized by |
  |---|---|---|
  | JS environment | ECMA-262 | quickjs-ng |
  | DOM | WHATWG DOM (+ HTML, CSSOM, CSSOM-View) | our DOM layer over litehtml |
  | CSS | CSS specs | litehtml's coverage (+ forks) |

- **Two tiers** (both standard-named): the **author surface** (what apps call — app-usage-driven;
  the DOM doc sub-tiers it into *required* / *margin* / *reflected*) and the **observation reads**
  (wider introspection for validation, *off* the author contract).
- **Trusted, bundled content only** — no networking/security surface; deny-by-default for anything
  not listed.

## Axis 1 — JavaScript environment (ECMA-262)

- **Baseline:** ES2022 *(open: confirm the exact pin against quickjs-ng)*. Pre-bundled single file
  (IIFE/classic) — **no dynamic module loading**.
- **Core language used:** `Object`, `Array`, `Map`, `Set`, `Promise`, `JSON`, `Math`, `String`/
  `Number` methods, `Array.isArray`.
- **Host globals we provide** (not ECMAScript): `console`; `setTimeout`/`clearTimeout`;
  `requestAnimationFrame`; the `game.*` host bridge *(deferred — shape pending the sim)*.
- **Not provided:** `eval`, `Proxy`, `Reflect`, `WeakMap`, `BigInt`, `Symbol` (beyond internal),
  Workers/threads, `Intl`/ICU, locale/timezone `Date` beyond core.

## Axis 2 — DOM (WHATWG DOM + HTML + CSSOM)

**The authoritative member list is [`components-dom-surface.md`](components-dom-surface.md)** —
spec-exact signatures, Preact-measured scope, and the behavioral contract (the `name in dom` write
routing; the reflected-property set `id`/`className`/`value`/`checked`; the extensible Event object;
`style` `cssText`/`setProperty`; `createElementNS` as the creation primitive; `Text.data`;
`remove()` / `insertBefore`-only). This axis states only the principles; the surface is **not**
duplicated here (one source of truth — see that doc). At a glance:

- **Author surface**, sub-tiered in the member doc: **required** (Preact's renderer / the panel
  mount actually call it), **margin** (obvious standard siblings included so stock author JS works —
  tcdent-ratified 2026-06-14), **reflected** (IDL props that must exist on the node:
  `id`/`className`/`value`/`checked`).
- **Observation reads** — off the author contract: `nodeName`, `nodeValue`, `attributes`
  enumeration, `getBoundingClientRect` (CSSOM-View), `getComputedStyle` (CSSOM).
- **Dispatch is bubble + capture (both required).** Capture is no longer deferred — Preact attaches
  capture-phase listeners.
- **OUT** (not implemented): `classList`, `DocumentFragment`, `innerHTML`; pointer/mouse input and
  its `Event` fields (deferred — see `components-input-routing.md`); layout reads beyond observation
  (`offset*`, `scrollTop`); storage / `history` / `location` / observers / `customElements` /
  `canvas` / `WebSocket`.

## Axis 3 — CSS (CSS specs, via litehtml; authority = Cathode's own CSS declaration)

**Authority:** Cathode's own CSS capability declaration (`Cathode/Authoring/`), authored from
litehtml's **actual** coverage — currently a stub (not yet authored). Panels' `panels/css-subset.json`
is the panels team's own separate guard; Cathode neither uses, moves, nor depends on it (*internalize*:
author our own conformant declaration, never couple to another dev's area).

### IN

- **Layout:** flexbox — `display:flex`, `flex-direction`, `flex-wrap`, `flex` shorthand,
  `align-items`, `justify-content`, and `gap` (implement in litehtml or fall back to margins — a
  build-how, not an open decision); block/inline.
- **Box model:** `margin`, `padding`, `width`, `height`, `box-sizing`, `border` (width/style/color),
  `border-radius`.
- **Color:** hex and **`var()`** — CSS custom properties are **required** (the theming model);
  *(risk: litehtml var() support — validate early)*.
- **Text:** `font-family`, `font-size`, `font-weight`, `text-align`, `opacity`, `letter-spacing`,
  `text-transform`.
- **Selectors:** class, descendant, child (`>`), `:focus`.
- **Units:** `px`, `%`, `em`, `rem`.
- **`@font-face`** (WOFF/TTF) — forthcoming with brand/glyph fonts.

### OUT (settled — not open forks)

- **CSS Grid** — out. (Fork into litehtml only if a real screen ever needs it — a future
  change-trigger, not a standing open decision.)
- **Animations / transitions / `@keyframes` / transforms** — out.
- `box-shadow`, `text-shadow`, filters, gradients; `@layer`/`@property`/`@supports`/`@media`;
  attribute & sibling selectors; `oklch`/`lab`/`color-mix`/space-separated `rgb()`.

## Settled

The Axis-3 IN/OUT lists above ARE the CSS design — settled, not a standing decision bucket. The former
CSS "open decisions" are resolved: animations/transitions/transforms are OUT; flex `gap` is IN as a
build-how (implement, or fall back to margins). The only remaining pin — the ES2022 baseline — is a
JS Axis-1 item, noted there, not a CSS decision.

*(Resolved 2026-06-14, formerly open: the DOM author-surface edges — `checked` is IN (reflected),
`classList`/`DocumentFragment` are OUT, the out-of-the-box margin is ratified; and the under-covered
members `nodeType`/`getElementById`/`querySelector` are confirmed IN the contract. All now carried by
`components-dom-surface.md`.)*

## Relationship to the gate

This doc = **what** we support (intent, standard-anchored). `components-authoring-contract-gate.md`
= **how** it's enforced (the `.d.ts` generated from the interface, the CSS linter against
Cathode's own CSS declaration, the build hard-fail). The interface should realize this surface; where it
doesn't, that's a bug against intent, not a redefinition of it.
