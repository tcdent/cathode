# Sprint 031 — Cathode Example Integration (the inverted reference)

> **GREEN-LIT (2026-06-25, PM: "drive the inversion"; tcdent direction).** Cathode ships as a
> standalone public library; this sprint builds the minimal HOST-AGNOSTIC reference example showing
> how any game wires into Cathode — game object registration into `game.*`, semantic-intent delivery,
> and the in-world targetable device actor. **The ruling: DRIVE THE INVERSION — Cathode OWNS the
> interfaces (`DeliverIntent` + the 6-intent enum + the targetable-surface interface); the example
> demonstrates the HOST implementing them; the player team ADAPTS to what Cathode exposes (they are
> NOT the contract owner).** The load-bearing invariant (`components-host-bridge.md`, tcdent): "the
> game wires to Cathode, not Cathode to the game" — the dependency arrow is **`game → Cathode` only**;
> Cathode stays host-agnostic. The example ships with the public Cathode repo; UPTIME adopts it as its
> pattern. The Cathode team owns the example — not agency, not the room-placer.

## Scope Boundary

This sprint makes Cathode's integration story **demonstrable + uniform**: all three integration seams
become "the host registers/implements INTO Cathode," shown by a minimal generic reference host that
ships with the plugin. Two seams are inverted *in this sprint* (the input-intent contract, the
targetable-surface contract) so the example can be faithfully host-agnostic; `game.*` is already
inverted.

**Owns:**
- **The example modules** — `CathodeExample` (Core-only: the reference bridge + the `game.*` screen)
  and `CathodeExampleDevice` (Engine-linking: the targetable example actor). Both host-agnostic, ZERO
  `Uptime*`: `CathodeExample` Core-only (Core/CathodeDom/CathodeComposition/CathodeSurfaces);
  `CathodeExampleDevice` = CoreUObject + Engine + the Cathode modules (+ the egress texture path for the
  `GetTexture`→MID sample).
- **The input-intent inversion (Cathode-owned):** `ECathodeInputIntent` (the canonical six), the
  intent→key map (moved INTO the plugin — resolves the 030 doc-vs-code gap), and
  `DesignateActiveSurface`/`ReleaseActiveSurface`/`DeliverIntent(ECathodeInputIntent)` on
  `FCompositionSurfaceOwner` (the intent layer atop S1's `DispatchKeyToSurface`).
- **The targetable-surface inversion (Cathode-owned):** `ICathodeTargetableSurface` (minimally
  `GetSurfaceId() → FSurfaceId`) — the host's targeting resolves a Cathode surface BY this, never
  naming the actor.
- **The reference example** demonstrating all three: `game.*` registration + intent delivery + the
  targetable device actor — one generic host, the arrow `game → Cathode` only.

**Does NOT own (named, with the owner):**
- **UPTIME's 030 wiring adaptation** — migrating `FUptimeSurfaceHandshakeAdapter` / the player's
  `ESurfaceInputKind` / `ISurfaceHandshakePort` to the new Cathode interfaces is the **player team's**
  follow-on (the PM coordinates). 031 ADDS the Cathode path beside the 030 path; **do not delete or
  repurpose the 030 host types** (the player still uses them — the transient coexists, retired on the
  player's migration).
- **The host's `IPlayerInteractable`** — host-side (entangled with Agency/mode/`FText`); stays the
  host's. The **integrator** implements it (the host player-ray resolving the Cathode surface BY
  `ICathodeTargetableSurface`); the host-agnostic plugin example actor implements
  `ICathodeTargetableSurface` ONLY — it can NOT implement `IPlayerInteractable` (that would dep
  UptimePlayer, the `Cathode → host` reject).
- **The `game.*` content** — the bridge's member set is the integrator's; sim-deferred for UPTIME's
  own game (`components-host-bridge.md`). The example uses a minimal generic state (a tick + named
  entities).
- **Re-proving the #28/030 mechanisms** — the example DEMONSTRATES registration/delivery minimally; it
  does not re-prove the `game.*` projection, the worker-posted keydown, or the device output.

## Problem

Cathode is a standalone library, but its three integration seams have **different inversion maturity**:
- **`game.*`** is cleanly inverted — Cathode owns `ICathodeGameBridge` (plugin); the host registers IN.
- **The handshake** is NOT inverted — the host owns `ISurfaceHandshakePort` (UptimePlayer) + the
  intent→key map (030's `FUptimeSurfaceHandshakeAdapter`); Cathode only takes a key string. This
  *contradicts* `components-input-routing.md` (Cathode's Input Routing owns the 6-intent→keydown
  delivery) — a doc-vs-code gap.
- **The device-targetability** is host-owned (`IPlayerInteractable`) + unbuilt (030's RQ2 gap):
  `ACathodeDeviceActor` is output-only, not targetable, no id.
So a host-agnostic example can't show all three uniformly today, and no reference example exists (the
plugin has zero `AActor`, no `examples/` C++).

## Goal

> PM (tcdent, 2026-06-25): "drive the inversion. It is not cross-team. Cathode owns the interfaces —
> `DeliverIntent` + the 6-intent enum + the targetable-surface interface. The example demonstrates the
> host implementing them. The player team adapts to what Cathode exposes; they are not the contract
> owner. Build the full inverted reference."

Finishing: a minimal generic host (shipped with the plugin) registers a game object into `game.*`,
delivers semantic intents into Cathode, and exposes a targetable in-world surface — all `game →
Cathode`, the canonical pattern UPTIME and any integrator adopt.

## Final State

- **`CathodeExample`** (Core-only plugin module) — `FExampleGameBridge : ICathodeGameBridge` (the
  reference host object: a settable+incrementing tick, an entity-name map) + a `game.*`-reading screen
  + the injecting `Install`.
- **`ECathodeInputIntent`** (in CathodeSurfaces, beside `FSurfaceId`) — the canonical six; the
  intent→key map + `DeliverIntent`/`DesignateActiveSurface`/`ReleaseActiveSurface` on
  `FCompositionSurfaceOwner`. `DispatchKeyToSurface` (raw key) stays public beneath `DeliverIntent`
  (the raw layer + the 030 transient).
- **`ICathodeTargetableSurface`** (in CathodeSurfaces, beside `FSurfaceId`/`ECathodeInputIntent` — the
  neutral surface vocabulary) — `GetSurfaceId() → FSurfaceId`.
- **`CathodeExampleDevice`** (Engine-linking plugin module) — a screen-mesh example actor sampling
  `FCompositionSurfaceOwner::GetTexture` onto a MID, implementing **`ICathodeTargetableSurface` ONLY**
  (host-agnostic — exposes `GetSurfaceId`). The host's `IPlayerInteractable` player-ray (resolving the
  Cathode surface BY `ICathodeTargetableSurface`) is the **integrator's** adaptation half, NOT the
  plugin — the plugin cannot dep UptimePlayer (the inversion's reject).

## In Scope (ordered slices)

- **Slice 1 — the `game.*` bridge example (in flight; the cleanly-inverted seam).** A Core-only
  `CathodeExample` module: `FExampleGameBridge : ICathodeGameBridge` (a settable+incrementing tick + an
  entity-name map) + a screen reading `game.tickCount`/`game.getEntityName` (authored `.iife.js` via
  the 025 pipeline; inline fallback) + the injecting `Install`. *Proof:* mutate the bridge's host state
  → drive → the SCREEN reflects it (GPU readback) + the **stub-swap discriminator** (a different bridge
  value yields a different render — the screen reflects THIS host's live state, not a constant; a
  hardcoded render REDs the swap). Host-agnostic: `CathodeExample.Build.cs` ZERO `Uptime*` (grep).
  DEMONSTRATES registration; does not re-prove the `game.*` projection.
- **Slice 2 — the input-intent inversion (Cathode owns the contract).** Add `ECathodeInputIntent`
  (CathodeSurfaces) + the intent→key map (moved into the plugin) + `DesignateActiveSurface`/
  `ReleaseActiveSurface`/`DeliverIntent` on `FCompositionSurfaceOwner` (mapping intent → the keydown →
  the existing worker-posted `DispatchKeyToSurface`). The example HOST calls `DesignateActiveSurface`
  + `DeliverIntent` (not a host port). *Proof:* deliver the six intents through Cathode's
  `DeliverIntent` → the surface `document` keydown fires with the canonical `e.key` (the 030
  delivery, now driven through Cathode's intent contract; the intent→key map Cathode-owned). The
  example host stays Cathode-only.
- **Slice 3 — the targetable device actor (Cathode owns the targetable-surface contract).** Add
  `ICathodeTargetableSurface` + a `CathodeExampleDevice` Engine-linking example actor (screen mesh,
  egress→MID) implementing **`ICathodeTargetableSurface` ONLY** (host-agnostic — exposes `GetSurfaceId`);
  the host's `IPlayerInteractable` (the player-ray) is the **integrator's** adaptation half, NOT the
  plugin (the plugin can NOT dep the host). *Proof:* the targeting resolves the example actor's surface
  BY `ICathodeTargetableSurface` → `DesignateActiveSurface` → `DeliverIntent` → the keydown fires (the
  joined chain, Cathode-owned end to end), proven DIRECTLY (resolve the interface + designate + deliver
  — NOT a heavy host-specific player-ray stub; the inversion's bite is "the host resolves the Cathode
  surface by Cathode's interface," not the ray). The plugin actor names **NO host type** (no
  `IPlayerInteractable` — host-agnostic, ZERO `Uptime*`).

## Non-Goals

- Migrating UPTIME's 030 wiring to the new interfaces — the player team's follow-on; **do not delete or
  repurpose the 030 host types** (the transient coexists).
- Moving `IPlayerInteractable` Cathode-side — it stays host-side; the **integrator** implements it (the
  host player-ray adapts to Cathode's `ICathodeTargetableSurface`). The host-agnostic plugin example
  actor implements `ICathodeTargetableSurface` ONLY — it can NOT implement `IPlayerInteractable` (that
  would dep UptimePlayer — the `Cathode → host` reject).
- The `game.*` member content — sim-deferred; a generic minimal state in the example.
- Re-proving the #28 projection / the worker-posted keydown / the device output — DEMONSTRATE, don't
  re-prove.
- Any `Cathode → host` arrow — the inversion's reject; the example deps no `Uptime*`.

## Starting Surface (the dev's audit, confirmed)

- **`game.*` (#28, host-agnostic):** `ICathodeGameBridge` (`CathodeDom/Public/CathodeGameBridge.h`),
  `InstallGameBridgeAsGlobal`, the two `FCompositionSurfaceOwner::Install` overloads, `GGameMembers`
  the single codegen+install source. No shippable non-test host bridge exists (only `FStubGameBridge`
  / WITH_DEV stubs).
- **The handshake (030, host-owned):** `ISurfaceHandshakePort`/`ESurfaceInputKind` (UptimePlayer);
  `FUptimeSurfaceHandshakeAdapter` (game/Source/Uptime) holds the intent→key map; Cathode's only
  surface is `FCompositionSurfaceOwner::DispatchKeyToSurface` (plugin, string-in).
- **The device actor (output-only):** `ACathodeDeviceActor`/`UCathodeDeviceSubsystem` (UptimeCathodeDevice,
  game/Source) sample egress→MID; NO `IPlayerInteractable`, no id, not targetable.
- **The plugin:** 11 modules, only `CathodeRuntime` links Engine; zero `AActor` in the plugin; no
  `examples/` C++.

## Method

- **The inversion is the invariant:** `game → Cathode` only; Cathode owns each interface, the host
  implements/registers. Every example dep is grep-checkable host-free (ZERO `Uptime*`).
- **Cathode-owned additions, not host refactors:** S2/S3 ADD Cathode-side interfaces beside the
  existing primitives; the 030 host path is untouched (the transient).
- **Demonstrate, don't re-prove:** the example exercises registration/delivery/targeting end-to-end;
  it does not re-run the #28 coherence, the 030 worker-post, or the device output in isolation.
- **The mutate-and-observe discipline:** each proof binds the rendered/delivered result to the host's
  live value (the stub-swap discriminator), not a constant.

## Acceptance

- **S1:** the host value reaches the screen (GPU readback) + the stub-swap discriminator (different
  bridge value → different render); `CathodeExample` host-agnostic (grep ZERO `Uptime*`); no
  regression.
- **S2:** the six intents through Cathode's `DeliverIntent` fire the surface `document` keydown with
  the canonical `e.key` (the intent→key map Cathode-owned, in the plugin); the example host calls
  `DesignateActiveSurface`/`DeliverIntent` (not a host port); host-agnostic; the 030
  `DispatchKeyToSurface` path intact (the transient).
- **S3:** the targeting resolves the example actor BY `ICathodeTargetableSurface` →
  `DesignateActiveSurface` → `DeliverIntent` → the keydown fires (the joined Cathode-owned chain),
  proven directly; the example actor implements `ICathodeTargetableSurface` ONLY and names **NO host
  type** (no `IPlayerInteractable` — host-agnostic, grep ZERO `Uptime*`); the host player-ray
  (`IPlayerInteractable`) is the integrator's adaptation half.
- **Inversion intact** (every example/interface dep `game → Cathode` only, no `Uptime*`); **no
  regression** (the full Cathode suite + the prior baselines); the 030 host types untouched.

## Review Questions (resolved forks — recorded)

1. **Drive-vs-example-what-exists — RESOLVED (PM/tcdent): DRIVE THE INVERSION.** Cathode owns the
   interfaces; the host implements; the player adapts (not the owner). Not cross-team.
2. **B-i `DispatchKeyToSurface` public-vs-private — RESOLVED (lead): both.** `DeliverIntent` is the
   intent contract; `DispatchKeyToSurface` (raw key) stays public beneath it (the raw layer + the 030
   transient). May go private after the player migrates.
3. **C-i example targeting direct-vs-player-stub — RESOLVED (lead): direct.** Resolve the
   `ICathodeTargetableSurface` + designate + deliver; the inversion's bite is the interface
   resolution, not a host-specific ray stub.
4. **Placement — RESOLVED (lead): `ECathodeInputIntent` in CathodeSurfaces (the id+intent vocabulary);
   the example actor in a new Engine-linking `CathodeExampleDevice` (keep CathodeRuntime pure).**
5. **The transient — RESOLVED (PM): the 030 path coexists; the player team migrates later; 031 does
   not refactor/delete the 030 host types.**

## Outcome

031 builds the host-agnostic reference example proving Cathode's standalone-library integration story —
all three integration seams are now "the host registers/implements INTO Cathode," shown by a generic
reference that ships with the public plugin. The build (S1+S2+S3) is delivered, each slice box-green,
independently reviewer-accepted, and scoped-committed:

| slice | commit |
| --- | --- |
| S1 — the game.* bridge example | `b195796` |
| S2 — the input-intent inversion (Cathode owns the contract) | `169c9fd` |
| S3 — the targetable device actor | `b5f6588` |

**The inversion is complete:** `game.*` registration (Cathode owns `ICathodeGameBridge`) + semantic-intent
delivery (Cathode owns `DeliverIntent` + the 6-intent enum + the intent→key map, moved off the host adapter
— resolving the 030 doc-vs-code gap) + the targetable device (Cathode owns `ICathodeTargetableSurface`). The
arrow is `game → Cathode` only — verified at every grain on the raw dep/include/inheritance block (ZERO
`Uptime*`, ZERO `IPlayerInteractable` inheritance; the host tokens are prose). The example modules
(`CathodeExample` Core-only + `CathodeExampleDevice` Engine-linking) ship host-agnostic with the public repo.

**Deferred / follow-on:** UPTIME's 030-wiring migration to the new interfaces (the player team's,
PM-coordinated — the transient coexists, the 030 host path untouched). The screen→host release signal
(delivery-only S2; the PM's listed interfaces). The two **integrator host-side halves** (documented, not
plugin code): the authored `game.*`-reading screen (`.ts` → the 025 gate → `.iife.js`; S1 proves the
registration it reads), and the player-ray targeting (the host's `IPlayerInteractable` resolving Cathode
surfaces by `ICathodeTargetableSurface`). **Open for the PM:** the example README/docs (per CLAUDE.md, not
created proactively). Full Cathode suite at close: **153/153, 0 Fatal.**

## Dev Retrospective (Mundo)

Durable lessons from building the 031 Cathode example integration (S1 `b195796` + S2 `169c9fd` + S3 `b5f6588`).

1. **A host-agnostic example is a maturity probe — it FORCES the inversion scope call.** The three seams had
   different inversion maturity: `game.*` cleanly inverted, the handshake + targetability host-owned. A
   host-agnostic example can't show a host-owned seam uniformly, so it turns an implicit ownership gap into
   an explicit scope decision ("example what EXISTS, or DRIVE the inversion?"). Audit the seams for "which
   side owns the contract" before promising a clean reference; if a seam can't be exampled host-agnostically,
   it isn't truly inverted.
2. **A firewall reverses silently through an INTERFACE IMPLEMENTATION — catch it even in your own grounding.**
   My grounding said "the example actor implements BOTH `IPlayerInteractable` + `ICathodeTargetableSurface`."
   But `IPlayerInteractable` is the host's — a plugin actor implementing it deps the host = `Cathode → host`,
   reversing the inversion. The audit flagged it, the grounding forgot it, the draft inherited it; my fidelity
   check + the reviewer independently re-caught it. The plugin DEFINES the interface the host implements,
   never the reverse; a hazard flagged in the audit MUST propagate to the grounding/draft.
3. **Verify host-agnostic on the RAW dep/include/inheritance block, comment-EXCLUDED.** A naive
   `grep Uptime`/`grep IPlayerInteractable` repeatedly false-flagged PROSE (the firewall rationale, the
   030-gap reference, the corrected-shape note). The real check is the dep STRING, the `#include`, and the
   class INHERITANCE — the comment that EXPLAINS the firewall is exactly what false-matches it.
4. **Resolve a doc-vs-code ownership gap by MOVING the code to the doc's owner.** `components-input-routing.md`
   said Cathode owns the intent→key map; 030 had put it host-side. S2 moved it Cathode-side, resolving the
   gap — the doc encoded the intended boundary; the code drifted. Move the code, don't amend the doc.
5. **Invert by ADDING beside, not refactoring — the transient keeps the consumer's migration off the critical
   path.** S2/S3 added Cathode-side interfaces BESIDE the existing primitives (`DispatchKeyToSurface` stayed
   public; the 030 adapter untouched). The consumer migrates on its own schedule; the inversion lands without
   a cross-team lockstep. The transient duplication is bounded and retired by the consumer.

**Integrator-doc notes (the README is a PM call per CLAUDE.md):** the example ships the CATHODE-facing wiring;
two halves are the integrator's HOST-SIDE adaptation, not plugin code — (a) the authored `game.*`-reading
screen (`.ts` → the 025 gate → `.iife.js`; S1 proves the registration it reads), (b) the player-ray targeting
(the host's `IPlayerInteractable` resolving Cathode surfaces by `ICathodeTargetableSurface`) + the actor's
mesh/material.

## Review Retrospective (uptime.browse-review)

All three slices were re-derived independently from current source + the settled box run; the
host-agnostic invariant was checked on the raw dependency/include blocks and the raw class
declaration, never a relayed claim or a filtered grep. Durable lessons:

- **Verify the inversion on the RAW dep + #include + INHERITANCE LIST, comment-excluded.** The
  load-bearing invariant is "the library deps ZERO host" (game → Cathode only). It is a property of
  the *quoted* dependency strings, the `#include` lines, AND the class's inheritance list — not a
  naive `grep "Uptime"`/`grep "IPlayerInteractable"`, which counts the explanatory PROSE comments
  (the rationale: "names NO host type", "the player-ray is the integrator's half") and false-flags.
  Read the raw blocks. A plugin type that *inherits/implements a host interface* is a host
  dependency (it needs the host `#include` + the host module dep) — so the inheritance list is part
  of the firewall check, the same grain as the dep block.

- **The Cathode→host arrow hides in a class implementing a host interface — catch it at premise,
  route the fork.** The drafted example actor implemented the host's `IPlayerInteractable` to be
  "targetable," which is a `CathodeExampleDevice → UptimePlayer` dependency — the public Cathode repo
  would then dep UPTIME, the inversion's reject — and an internal contradiction with the same draft's
  "ZERO Uptime*" claim. This was a premise-level design fork (where the targetability contract lives),
  load-bearing because the library ships standalone, so it was routed to the human, not absorbed. The
  resolution: the plugin actor implements `ICathodeTargetableSurface` ONLY; the `IPlayerInteractable`
  player-ray is the integrator's host-side half. A "ZERO Uptime*" claim sitting next to "implements a
  host interface" is the tell — they cannot both hold.

- **An example DEMONSTRATES, it does not RE-PROVE.** A reference example's job is to show registration
  (S1), intent delivery (S2), and targeting (S3) end-to-end using the *built* mechanisms — not to
  re-run their coherence. Re-proving #28's `GGameMembers` projection, driving the 030 worker-posted
  `DispatchKeyToSurface` directly, or re-proving the device's egress output would each be the reject
  (re-proving built coverage). The proof drives the host value / the intent / the resolved surface
  *through* the new seam to the screen/keydown; the downstream engines are consumed, not re-tested.

- **Resolve-by-interface is the inversion's bite.** The targeting proof holds the actor AS
  `ICathodeTargetableSurface*` (the Cathode interface, never the concrete `ACathodeExampleDeviceActor`
  type) and calls `GetSurfaceId()` — no actor-type naming, no host-specific player-ray stub. The bite
  is precisely "the host resolves the Cathode surface BY Cathode's interface." A proof that names the
  concrete actor, or stands up a host ray, tests the wrong thing (the host's adaptation, not the
  Cathode-owned contract).

- **The discriminator binds the render/delivery to the host's LIVE value, never a constant.** S1's
  stub-swap (a different bridge value → a different render; a hardcoded render REDs the swap) and
  S2/S3's per-intent map (each intent → its OWN canonical e.key; a constant/wrong/swapped map REDs)
  are the mutate-and-observe discipline — the rendered/delivered result is proven to track the source,
  not a baked value.

## Lead Retrospective (uptime.browse-lead)

1. **A host-agnostic-library sprint is a maturity probe — surface the scope fork it exposes.** The example
   forced the question the dev and reviewer both grounded: the three seams had different inversion maturity
   (`game.*` inverted; the handshake + targetability host-owned). I surfaced the genuine scope fork (drive
   the inversion vs example-what-exists) to the PM with a grounded lean and the cross-team cost — and the PM
   ruled drive-the-inversion *and reframed the ownership* (Cathode owns; the player adapts; not cross-team),
   which simplified it. A fork that touches another team's ratified contract is the human's to rule.
2. **Resolve a doc-vs-code gap toward the doc's owner.** The design-of-record said Cathode owns the intent→key
   delivery; 030 had put it host-side. The inversion resolved it by *moving the code to the doc's owner* (the
   map Cathode-side), not amending the doc — the doc encoded the intended boundary; the code had drifted.
3. **The two-direction doc check catches the spot a correction misses.** When the reviewer caught the
   `Cathode → host` arrow (the plugin actor can't implement the host's `IPlayerInteractable`), I corrected the
   draft in four places — and missed the "Does NOT own" bullet. Grepping every `IPlayerInteractable`
   reference before the close caught it. After a correction, grep every occurrence; the forward fix forgets
   the reverse spot.
4. **Concede and reframe when the human simplifies the call.** I'd weighted the inversion's cross-team cost as
   real; the PM dissolved it (Cathode owns the interfaces, the player conforms). I dropped my framing and
   re-scoped to the simpler reality rather than defending the cost I'd assigned.
5. **Hold the inversion invariant at every grain — including the raw inheritance list.** Each slice gated on
   reading the *raw* dep/include/inheritance block (the comment-false-match: the firewall rationale and the
   corrected-shape note name the very tokens a naive grep flags), and each commit re-verified the structural
   form (dep-string / `#include` / inheritance), never the bare word.
