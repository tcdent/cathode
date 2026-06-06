# Cathode — Controller / Input Events (detail)

> **Deferred** until the end of the current development cycle. This describes *what we want to do*
> and *why* — it does **not** define the contract (the actual buttons, axes, event names, or device
> layout). That shape is adopted from upstream Unreal input conventions, not invented here.

Detail for the controller-input model (see `components.md`). It refines the input thinking in
`components-input-routing.md`: this doc covers where input *comes from* and how it's *emitted into
JS*; Input Routing covers *which surface* receives it and focus management.

## What we want

UPTIME is controller-first, and the panels run in-game, so the controller must drive them. The
model: **Unreal is the single source of truth for input.** Unreal's input system already resolves
physical input — controller *and* keyboard, with the player's bindings — into semantic actions.
We use that resolved layer directly and **emit its events into the JavaScript runtime**, rather
than fabricating keystrokes.

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

## Responsibilities

- **Source semantic input from Unreal.** Take the already-resolved input events from the engine's
  input system. *Why:* reuse the one config that exists rather than a parallel mapping.
- **Emit them into the JS runtime.** Surface the events to the running panels. *Why:* the panels
  can't react to what they can't receive.
- **Deliver through the standard DOM event system.** Events arrive via `addEventListener` on
  `document` (already in the supported surface), as a semantic event type. *Why:* it stays
  idiomatic web — authors use an API they already know — while carrying intent instead of
  keystrokes. Native-and-semantic without a bespoke global.

## Not in this doc — the contract

Left to upstream Unreal conventions, to be adopted rather than reinvented:

- the device layout / button + axis shape,
- the event vocabulary and names,
- the semantic level(s) exposed.

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

- Whole component deferred to end of the current development cycle.
- Event vocabulary + device layout: adopt from Unreal conventions (to be found).
- Semantic level: a high-level nav/action set fits the panels; a lower-level button/stick channel
  can be added later if richer input is ever needed.
- The dev-environment keyboard→semantic shim.
