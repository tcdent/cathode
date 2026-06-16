# Cathode — Host Bridge `game.*` (detail)

Detail for the **Host Bridge** component (see `components.md`). What/why, not how. UE reflection is
grounding.

This is how a screen reads and affects the game. Author code reaches the host through a curated
`game.*` API — never raw object access — and that one API definition drives the runtime bindings,
the author types, and the build gate so they cannot drift.

## Responsibilities

**Expose the curated `game.*` API.** A deliberate public surface of host functions and state,
under the `game` global. *Why:* a curated API is a stable contract and an encapsulation boundary;
raw object access would couple screens to the game's internal graph and rot on every refactor.

**State ingress with change signals.** Surface host-backed state to JS and signal when it changes
so screens re-render reactively. *Why:* screens are views of game state; they must reflect it
without polling.

**Action egress.** Let screens call host functions to affect the game. *Why:* interaction has to
do something — a button press becomes a host call, directly (a C↔JS call, not a transport).

**Generate from one source.** One annotated C++ interface is the single source; a single
generate-from-interface projection produces the quickjs bindings, the author `lib.cathode.d.ts`,
and the runtime-verification descriptor — **one extraction, three outputs**, locked in step by
construction. *Why:* one definition keeping runtime, types, gate, and verification in lock-step is
the only way to guarantee no drift. *Ground:* the projection reads the **C++ binding-interface**
(the non-UObject quickjs-class layer built in `CathodeBinding`/007), **not** UHT/UObject reflection
— the earlier "UE reflection (UHT)" grounding predated the 007 binding model and is superseded by
it; bounded type vocabulary per 007.

## Key concern — intentionally deferred shape

What the API *contains* and how it's *organized* (a single wrapper file vs each game subsystem
publishing its own public surface, which could double as the game's internal interface) is left
open until the simulation exists. **Settled regardless:** it's curated, `game.*`, and produced by
the single generate-from-interface projection (above). **Only `game.*` *content* waits on the sim**
— the mechanism and the engine↔app delineation are settled, and the engine-side surfaces (the DOM
member bindings, the standard globals) ride the same projection now, sim-independent.

## Boundaries

Owns the host API surface and its generation. Uses JS↔Native Bridge for the bindings and lifetime.
Feeds the Authoring Contract & Gate (the generated `.d.ts`). Registered into the JS global by
Runtime Core at startup. Cathode itself stays game-agnostic — this surface is filled in by the host
game.

**`game.*` is the SOLE Unreal-coupled surface in Cathode.** The DOM and the standard globals are
bound by Cathode's own non-UObject mechanism with **no Unreal dependency**; UHT/UObject reflection,
if used at all, lives **only here** — binding the *existing* Unreal subsystem objects that `game.*`
selects from — never in the DOM. So `game.*` is a thin **configuration layer** choosing which public
API of an existing Unreal interface to expose under the global, not a binding of the whole system
(`document.getElementById` needs no knowledge of game state). It is a separate concern from the DOM
JS-binding, and its *content* is sim-deferred; the DOM + globals bindings are pure-Cathode and build
independently of it.

## Open / deferred (team's call)

- API code organization (deferred, pending the sim).
- The reactivity/change-signal model.

*(Settled 2026-06-14 — no longer open: the generation path is generate-from-interface over the C++
binding-interface, one extraction → bindings + `.d.ts` + verification descriptor. It and the
engine↔app delineation were set with tcdent; built in the **Host Bridge / generate-from-interface
sprint**. See `components-authoring-contract-gate.md`.)*
