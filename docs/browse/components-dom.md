# Cathode — DOM (detail)

Detail for the **DOM** component (see `components.md`). What/why, not how.

The DOM is the live document author JavaScript manipulates and the source of truth fed to layout.
Its surface is deliberately small — that bound is the firewall against accidentally rebuilding a
browser.

## Responsibilities

**Maintain the retained node tree.** Elements, text nodes, attributes, parent/child structure.
*Why:* it is the live document JS mutates and the input layout consumes; it must persist between
ticks, not be re-parsed.

**Expose the DOM interface.** The C++ DOM interface we author — element create/insert/remove,
attribute and property access, tree reads, events — and nothing we didn't deliberately implement.
*Why:* it's what authors program against, and the authoring contract (`.d.ts` + gate) is
**generated from this interface**, not a hand-maintained string allow-list or a parallel
registration — one source of truth, no drift. *Firewall:* the line against "accidentally a
browser" now lives at the **implement-or-not decision** — a method is on the surface because we
chose to implement it, never because litehtml happened to expose it. Don't reflect litehtml's raw
surface wholesale; the interface is deliberate.

**Run the event system.** The `Event` object, `add`/`removeEventListener`, and dispatch with
bubbling. *Why:* interaction is the whole point of in-page JS, and event delegation depends on
bubbling.

**Expose observation reads (outside the author surface).** `getBoundingClientRect` and
`getComputedStyle` are exposed as engine reads for the observation layer (see `observation.md`),
backed by Layout & Style's computed geometry/style. *Why:* the shared observer needs them — but
they are deliberately **not** part of the author contract (panels don't use them; a scope choice,
not a difficulty one), so they don't widen the firewall. `getBoundingClientRect` is a near-free
read of existing layout data; `getComputedStyle` is a bounded mapping scoped to the contract's CSS
properties. Not gated behind a flag — just not advertised on the authoring surface.

**Dirty-mark on mutation.** Flag what changed when the tree is modified. *Why:* it tells Layout &
Style to relayout only the affected region, which keeps the per-change cost small.

## Key concern — node lifetime

DOM node lifetime is intertwined with the JS↔Native Bridge: listeners capture nodes, creating the
cross-heap cycle hazard. The DOM owns the *trigger points* for teardown (`removeChild`,
`removeEventListener`); the bridge owns the mechanism. Getting these handshakes right is the
component's main risk.

## Boundaries

Owns the tree, the API surface, and events. Delegates layout to Layout & Style,
and the cross-heap lifetime mechanism to JS↔Native Bridge. The interface it implements **is** the
source the Authoring Contract & Gate generates the `.d.ts`/gate from — not a separate string list.

## Open / deferred (team's call)

- The canonical surface is no longer a list to *freeze* — it **is** the implemented interface (the
  `.d.ts`/gate generate from it), and its authoritative member list now lives in
  `components-dom-surface.md`. The edges once open here are **resolved** (2026-06-14): the `Event`
  shape is `type`/`target`/`currentTarget`/`preventDefault`/`stopPropagation` + `KeyboardEvent.key`,
  with **bubble + capture** dispatch; `value`/`checked` are reflected get/set; `textContent` is an
  out-of-the-box margin member (text *updates* go through `Text.data`); `DocumentFragment` and
  `classList` are **out**.
