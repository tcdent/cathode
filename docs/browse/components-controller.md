# Cathode — Controller / Input Events (detail)

> **Deferred** until the end of the current development cycle. This describes *what we want to do*
> and *why*. The **device layout** (buttons, axes, physical resolution) is still adopted from
> upstream Unreal input conventions, not invented here. The **Cathode-facing semantic vocabulary**,
> however, is now **ratified** (see below) — it is pinned because an external contract (the player
> `SurfaceFocus` handshake) depends on it.

Detail for the controller-input model (see `components.md`). It refines the input thinking in
`components-input-routing.md`: this doc covers where input *comes from* and how it's *emitted into
JS*; Input Routing covers *which surface* receives it and focus management.

## What we want

UPTIME is controller-first, and the panels run in-game, so the controller must drive them. The
model: **Unreal is the single source of truth for input.** Unreal's input system already resolves
physical input — controller *and* keyboard, with the player's bindings — into semantic actions.
We use that resolved layer directly and **emit its events into the JavaScript runtime**, rather
than fabricating keystrokes.

## Ratified vocabulary

The Cathode-facing semantic set is **six discrete navigation intents** — no analog/continuous input:

| Intent | Meaning |
|---|---|
| `Up` / `Down` / `Left` / `Right` | directional focus move |
| `Accept` | activate the focused item (enter / confirm) |
| `Back` | back out — **unifies cancel** (no separate Cancel intent) |

Ratified by tcdent (2026-06-14) while ratifying the player `SurfaceFocus` contract. Rationale:
Cathode targets **d-pad-style 2D UI navigation** — no on-screen cursor, no analog. Binding the
boundary to a continuous vector would model input Cathode never consumes; the primitive is
*navigation*. Back/Cancel are unified deliberately (this is 2D UI, not a rich animated environment);
they can split later only if a real need appears. A lower-level button/stick channel remains a
possible *future* generalization (native-controller conventions), not part of this set.

This is the discrete form of the player handshake's `FSurfaceInputEvent` — the contract carries only
the intent `Kind`, no `NavAxis`. See `docs/teams/player/006-surface-handshake-gap-analysis.md`.

## Why — not keyboard emulation

Emulating the controller as keyboard keys forces a **double conversion with two configs to keep in
sync**: the host maps controller→key, then every panel re-maps key→action and has to assume the
host's controller→key mapping to interpret anything. It discards the semantic input layer the game
already computes, and it makes remapping fragile — a player rebinds in the game and the emulated
keys and the panel's fixed bindings can silently disagree.

Sourcing semantic events from Unreal collapses that to **one config, one conversion**:

- **Single source of truth** — physical→semantic is resolved once, by Unreal.
- **Device- and binding-agnostic panels** — a panel reacts to meaning ("up", "confirm", "back"),
  never to keys, devices, or bindings.
- **Free remapping** — the player rebinds in the game; resolved events are unchanged; panels need
  no changes.
- **Simpler on both sides** — the host stops fabricating keystrokes; the panel drops its
  key→action layer.

**Delivery vs. sourcing — the distinction (this is *not* the rejected emulation).** What's rejected
above is *host-side keyboard emulation that re-derives semantics from a configurable controller→key
map* — the second, drift-prone config. Cathode does the opposite: the player resolves
physical→semantic **once** (the single Unreal config), and Cathode then *delivers* that already-
resolved intent as a standard `keydown` through a **fixed, non-configurable** intent→key table. The
**semantics live upstream**; the **delivery is stock web**. No second config exists to desync — the
integrity this section argues for is preserved, and panels still consume ordinary keyboard events
out of the box. Ratified by tcdent (2026-06-14): the player layer is semantic, Cathode's delivery is
standard `keydown`.

## Responsibilities

- **Source semantic input from Unreal.** Take the already-resolved input events from the engine's
  input system. *Why:* reuse the one config that exists rather than a parallel mapping.
- **Emit them into the JS runtime.** Surface the events to the running panels. *Why:* the panels
  can't react to what they can't receive.
- **Deliver as standard synthetic keyboard events.** The player-resolved intent is delivered into
  the surface's JS as a standard `keydown` `KeyboardEvent` on `document` — the six intents mapped to
  canonical `e.key` values: `Up`→`ArrowUp`, `Down`→`ArrowDown`, `Left`→`ArrowLeft`,
  `Right`→`ArrowRight`, `Accept`→`Enter`, `Back`→`Escape`. *Why:* panels are authored to stock
  `addEventListener('keydown', e => e.key === …)` — out-of-the-box web, no Cathode-specific event API
  to learn. The intent→key map is a **fixed constant** in Cathode (not a config), so there is no
  second mapping to drift (see the distinction below).

## Not in this doc — the contract

Left to upstream Unreal conventions, to be adopted rather than reinvented:

- the device layout / button + axis shape,
- how Unreal Enhanced Input *resolves* physical input down to the six ratified intents.

The **semantic vocabulary itself is no longer open** — it's the ratified six-intent set above
(pinned by the player contract). What stays Unreal's is the physical/device layer that produces it.
There's no reason to invent the shape of a controller; we take Unreal's.

## Boundaries

- This component is the input **source and emission** (Unreal → semantic events → JS).
- **Surface targeting** (which surface gets an event) and **focus** are Input Routing's concern.
- **Text/character entry**, if ever needed, is a separate channel — nav semantics are not character
  input, and don't fold one into the other.
- A small **dev-environment shim** (mapping a real keyboard to the same semantic events) keeps
  panels developable/testable in a real browser and against the Chromium oracle.

## Grounding

Unreal's **Enhanced Input** system (Input Actions + Input Mapping Contexts) is the engine's
semantic input layer — it resolves hardware to named actions under a remappable config. It is the
natural source of both the emitted events and the device-layout conventions we'll adopt.

## Open / deferred

- Whole component (the *build*) deferred to end of the current development cycle.
- ~~Event vocabulary~~ — **ratified** (the six-intent set above); only the **device layout** /
  Unreal-side resolution remains to adopt from Unreal conventions.
- Semantic level: the ratified high-level nav/action set fits the panels; a lower-level
  button/stick channel can be added later only if richer input is ever needed.
- The dev-environment keyboard→semantic shim.
