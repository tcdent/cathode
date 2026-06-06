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

**Generate from one source.** Produce the quickjs bindings and the author `lib.cathode.d.ts` from
Unreal reflection. *Why:* one definition keeping runtime, types, and gate in lock-step is the only
way to guarantee no drift. *Ground:* UE reflection (UHT) → bindings + types; bounded type
vocabulary.

## Key concern — intentionally deferred shape

What the API *contains* and how it's *organized* (a single wrapper file vs each game subsystem
publishing its own public surface, which could double as the game's internal interface) is left
open until the simulation exists. Settled regardless: it's curated, `game.*`, and generated from
reflection.

## Boundaries

Owns the host API surface and its generation. Uses JS↔Native Bridge for the bindings and lifetime.
Feeds the Authoring Contract & Gate (the generated `.d.ts`). Registered into the JS global by
Runtime Core at startup. Cathode itself stays game-agnostic — this surface is filled in by the host
game.

## Open / deferred (team's call)

- API code organization (deferred, pending the sim).
- The reactivity/change-signal model.
- The exact reflection→binding generation path.
