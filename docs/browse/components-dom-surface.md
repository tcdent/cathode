# Cathode — DOM Surface (spec-grounded, Preact-measured)

The DOM contract Cathode exposes: the **minimal surface that runs our primary consumer** (the
compiled Preact panels) **plus a thin out-of-the-box margin**, every member at its **exact spec
signature**. Authority, in order:

1. **The spec defines each member** — name, call shape, return type, semantics
   ([WHATWG DOM](https://dom.spec.whatwg.org/) / WHATWG HTML / [CSSOM](https://drafts.csswg.org/cssom-1/) /
   [CSSOM-View](https://drafts.csswg.org/cssom-view-1/) / [UI Events](https://www.w3.org/TR/uievents/)).
   We never invent names; we pick a subset of the standard and implement it faithfully.
2. **Preact's real renderer scopes the subset** — which spec members earn a place is **measured from
   `panels/node_modules/preact/src`** (`diff/index.js`, `diff/props.js`, `diff/children.js`),
   2026-06-14 — not from `SURFACE.md`.
3. **A thin out-of-the-box margin** adds the obvious standard *siblings* of required members so stock
   author JS doesn't hit arbitrary holes (ratified by tcdent, 2026-06-14).

> `panels/lib/SURFACE.md` is a **reference pattern, not the authority.** Where it drifted from the
> measured renderer it loses — e.g. it claimed `createElement` is the always-invoked creation call;
> Preact's renderer actually calls `createElementNS` universally. The source is ground truth.

## Tags

- **required** — Preact's renderer or the panel mount actually calls it.
- **margin** — an out-of-the-box standard *sibling* of a required member; deliberately included so
  ordinary author code works, even though the renderer routes around it.
- **reflected** — an IDL property that **must exist on the node** (`name in dom === true`) so Preact's
  write path treats it as a property, not an attribute (see behavioral contract #1).
- **obs** — observation/introspection for validation; off the author `.d.ts`, same spec names.

Signatures in TS form (the `.d.ts` is the artifact); IDL nullable `T?` → `T | null`.

## Behavioral contract (what a member list can't express)

As binding as the signatures below:

1. **Property writes route by `name in dom`.** Preact's `setProperty` (`diff/props.js`) does
   `name in dom ? dom[name] = value : setAttribute/removeAttribute`. So the **reflected-property set is
   load-bearing** — it decides property-vs-attribute routing. Cathode reflects exactly **`id`,
   `className`, `value`, `checked`**; every other property name is *not* `in dom` and correctly falls
   to `setAttribute`/`removeAttribute`. (`className` must reflect to the `class` attribute, or
   class-based styling silently breaks.)
2. **The Event object is a plain *extensible* JS object.** Preact writes bookkeeping props onto the
   event (`e[…] = clock`) and relies on **capture phase** — `addEventListener` must accept a
   `useCapture` boolean. Listeners read `e.type`, `e.key`, `e.target`, and may call
   `e.preventDefault()` / `e.stopPropagation()`.
3. **`style` is a `CSSStyleDeclaration`** with `cssText` (string assign), named property access
   (`style[key] = v`), and **`setProperty(name, value)` for custom `--props`** — the `var()` theming
   path depends on `setProperty`.
4. **Element creation is `createElementNS(namespace, type)`** — the renderer's universal call (HTML
   namespace by default). `createElement` is the out-of-the-box sibling.
5. **Text content updates via `Text.data`** (`dom.data = newText`); `textContent` is the element-level
   out-of-the-box sibling.
6. **Removal is `node.remove()`; insertion is `insertBefore(node, ref | null)`** (`null` = append).
   `removeChild` / `appendChild` are out-of-the-box siblings.
7. **Hydration-only references — out of scope for trusted, pre-bundled, non-SSR panels.** Preact's
   hydration matching reads `element.localName` and probes `'setAttribute' in node` to tell element
   from text. Not exercised without SSR; implement only if hydration is ever turned on.
8. **Return-type fidelity — spec-TYPED surface, consumer-SCOPED depth.** A return exposes *exactly*
   the spec interface's surface, never a convenient superset; how *deep* it's built (liveness) is
   scoped to what the measured consumer actually needs.
   - **Surface (non-negotiable):** `childNodes` → a **`NodeList` surface** (`length` / `item(i)` /
     indexed `[i]` / iteration), **not a plain `Array`** — an Array adds `map`/`filter`/etc. that
     `NodeList` lacks, so author code using them would *false-pass on Cathode and break in Chrome*,
     the exact divergence the exact-spec-signature principle exists to prevent. Likewise `attributes`
     → `NamedNodeMap` surface, `getBoundingClientRect()` → `DOMRect`, `style` → `CSSStyleDeclaration`.
   - **Depth (consumer-scoped):** liveness is built only where a real consumer depends on it.
     `childNodes` / `attributes` are read at a point (Preact does `slice.call(childNodes)` at
     diff-time) → **typed *snapshot*, not live.** `style` is **written** by the consumer (Preact sets
     `style[k]` / `cssText`) → **live / mutable, write-through.** `DOMRect` is a point-in-time value by
     spec. Add liveness to a snapshot only when a consumer is shown to need it (faculty-B flags it).
     **Item types scope the same way:** `NamedNodeMap` items are **minimal `{ name, value }`** (the
     observer's only read), **not** a full registered `Attr` class. `attributes` is obs-tier — its
     consumer is our own observer, not author code — so there's no author consumer to protect, and
     minimal `{name,value}` is a *subset* of `Attr`, not a superset, so it raises **no** #8 false-pass
     (the surface rule guards a *superset* silently working on Cathode; a subset, on an obs-only
     return, isn't that). `instanceof Attr` isn't a contract path (obs-tier; and `Attr` undefined
     hard-errors / gate-fails, never silently diverges).
   - *Rationale:* exact-spec-signature exists for runtime **parity** (real code runs unmodified both
     ways) → the surface must be spec-typed (no superset false-pass); measured-consumer-scoping says
     don't over-build → depth/liveness is consumer-scoped. Spec-faithful surface, consumer-scoped depth.
   - *Resolves:* `childNodes` → **`NodeList`-typed snapshot** (the build call this gates).

## Document — [DOM / HTML]

| Signature | Tag |
|---|---|
| `createElementNS(namespace: string \| null, qualifiedName: string): Element` | required |
| `createElement(localName: string): Element` | margin |
| `createTextNode(data: string): Text` | required |
| `getElementById(elementId: string): Element \| null` | required (mount) |
| `querySelector(selectors: string): Element \| null` | required (mount) |
| `querySelectorAll(selectors: string): NodeList` | margin |
| `body: Element \| null` (attribute) | margin |
| `documentElement: Element` (readonly — the `<html>` root) | margin |
| `activeElement: Element \| null` (readonly — the focused element) | margin (the focus read-side + proof oracle) |

## Node — [DOM]

| Signature | Tag |
|---|---|
| `nodeType: number` (readonly, `unsigned short`) | required |
| `parentNode: Node \| null` (readonly) | required |
| `childNodes: NodeList` (readonly) | required |
| `firstChild: Node \| null` (readonly) | required |
| `nextSibling: Node \| null` (readonly) | required |
| `insertBefore(node: Node, child: Node \| null): Node` | required |
| `remove(): void` *(ChildNode)* | required |
| `removeChild(child: Node): Node` | margin |
| `appendChild(node: Node): Node` | margin |
| `textContent: string \| null` (get/set) | margin |
| `nodeName: string` (readonly) | obs |
| `nodeValue: string \| null` (get/set) | obs |

`nodeType` constants: `ELEMENT_NODE = 1`, `TEXT_NODE = 3`, `COMMENT_NODE = 8`,
`DOCUMENT_NODE = 9`, `DOCUMENT_FRAGMENT_NODE = 11`.

## CharacterData (Text) — [DOM]

| Signature | Tag |
|---|---|
| `data: string` (get/set) | required — Preact's text-update path (`dom.data = …`) |

## Element — [DOM] (+ CSSOM-View for geometry)

| Signature | Tag |
|---|---|
| `tagName: string` (readonly, upper-cased for HTML) | margin |
| `namespaceURI: string \| null` (readonly) | required (the `createElementNS` ns come-due) |
| `setAttribute(qualifiedName: string, value: string): void` | required |
| `removeAttribute(qualifiedName: string): void` | required |
| `getAttribute(qualifiedName: string): string \| null` | margin |
| `querySelector(selectors: string): Element \| null` | required |
| `querySelectorAll(selectors: string): NodeList` | margin |
| `matches(selectors: string): boolean` | margin |
| `closest(selectors: string): Element \| null` | margin |
| `id: string` (get/set) | reflected |
| `className: string` (get/set, reflects the `class` attribute) | reflected |
| `attributes: NamedNodeMap` (readonly, enumerable) | obs |
| `getBoundingClientRect(): DOMRect` — [CSSOM-View] | obs |

The **selector-query family** — `querySelector` (required, mount) + `querySelectorAll` / `matches` /
`closest` (margin) — all run on **one structural matcher** over the live `FDomNode` tree, reusing
litehtml's `css_selector::parse` (so `querySelector` and the CSS cascade agree on a selector by
construction). The margin three are **near-free** on that matcher (all-vs-first / boolean /
ancestor-walk), so they're included as the obvious siblings of `querySelector`. `querySelectorAll`
returns a **static `NodeList`** (snapshot, per behavioral-contract #8). `getElementsBy*` are *not* here
— they return a *live* `HTMLCollection`, which belongs with the live-returns work, not the matcher.

## HTMLElement / HTMLInputElement — [HTML] (+ CSSOM for `style`)

| Signature | Tag |
|---|---|
| `focus(): void` (HTMLElement) | required |
| `blur(): void` (HTMLElement) | margin (completes the focus model) |
| `style: CSSStyleDeclaration` (CSSOM, inline style) | required |
| `value: string` (HTMLInputElement, get/set) | reflected (required) |
| `checked: boolean` (HTMLInputElement, get/set) | reflected (required) |

The **focus model** is `focus()` / `blur()` / `document.activeElement`, all over the **one shared
`FocusedNodeId`** (focus sets it, blur clears it, activeElement reads it). `focus()` alone is half a
model — you can't unfocus or read focus state in JS — and it is **un-JS-provable without
`activeElement`** (the proof would otherwise be a C++ read-back, not the real surface). So
`activeElement` folds into the focus slice **by necessity** (focus()'s own JS-proof needs it);
`blur()` by the near-free trio precedent. `namespaceURI` is the settled `createElementNS`-ns come-due
(slice-5 dropped the ns "to namespaceURI's arrival"; this is that arrival — ns stored at
`createElement`/`createElementNS`, read via the getter; text nodes → `null`).

## CSSStyleDeclaration — [CSSOM] (the `style` object)

| Signature | Tag |
|---|---|
| `cssText: string` (get/set) | required |
| named property access — e.g. `style.color` (get/set) | required |
| `setProperty(property: string, value: string): void` | required — custom `--props` |

## EventTarget / Event / KeyboardEvent — [DOM] (+ UI Events for keys)

| Signature | Tag |
|---|---|
| `addEventListener(type: string, callback: EventListener \| null, options?: AddEventListenerOptions \| boolean): void` | required (capture supported) |
| `removeEventListener(type: string, callback: EventListener \| null, options?: EventListenerOptions \| boolean): void` | required |
| `dispatchEvent(event: Event): boolean` | margin |
| `Event.type: string` (readonly) | required |
| `Event.target: EventTarget \| null` (readonly) | required |
| `Event.currentTarget: EventTarget \| null` (readonly) | margin |
| `Event.bubbles: boolean` (readonly) | required |
| `Event.preventDefault(): void` | required |
| `Event.stopPropagation(): void` | required |
| `KeyboardEvent.key: string` (readonly) — [UI Events] | required |
| `KeyboardEvent.code: string` (readonly) — [UI Events] | margin |

Dispatch is **bubble phase + capture** (Preact attaches capture-phase listeners). The Event object
passed to a listener is **extensible** (behavioral contract #2). Cathode's synthetic input delivery
emits `keydown` `KeyboardEvent`s with a canonical `e.key` (`Up`→`ArrowUp` … `Accept`→`Enter`,
`Back`→`Escape`) — see `components-controller.md` / `components-input-routing.md`.

## Window — [CSSOM]

| Signature | Tag |
|---|---|
| `getComputedStyle(elt: Element): CSSStyleDeclaration` (live, read-only) | obs |

## Conformance vs. the current build

The contract is the **target**; `game/Plugins/Cathode/Source/CathodeDom/COVERAGE.md` tracks what the
current build implements. Known deltas to close (grounded in this pass):

- **`createElementNS`** (the build has `createElement`); **`Text.data`** get/set; **`node.remove()`**;
  **`style.cssText` + `setProperty`**; the **reflected-property write routing** (`id` / `className` /
  `value` / `checked` with `name in dom` semantics, `className`→`class`); the **extensible Event
  object** exposing `target` / `key` / `preventDefault` / `stopPropagation`; **`getElementById` /
  `querySelector`**; **`nodeType`**.
- The build's bubble/`stopPropagation`/`preventDefault` dispatch mechanics, `value`, and `textContent`
  exist — **verify** they match these signatures (notably `data` on `Text`, and `className`→`class`
  reflection rather than `setAttribute('className')`).

## Cross-refs

`components-dom.md` (what/why) · `spec-surface.md` (the three-axis contract) ·
`components-controller.md` + `components-input-routing.md` (synthetic `keydown` delivery) ·
`components-authoring-contract-gate.md` (enforcement). Grounding:
`panels/node_modules/preact/src/diff/{index,props,children}.js` (measured 2026-06-14).
