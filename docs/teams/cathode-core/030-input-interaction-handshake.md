# Sprint 030 — Input & Interaction (the key-events handshake adapter)

> **GREEN-LIT for S1+S2 (2026-06-25, PM; team-reviewed — dev FAITHFUL+BUILD-READY, reviewer
> PREMISE-INTACT with 3 discriminator sharpenings folded).** PM scope: the KEY-EVENTS HANDSHAKE
> ADAPTER ONLY — "the Controller JS emission, Input Routing dispatch, and in-world surface actor
> focus model." Pointer and hit-testing are OUT **by design** (a settled couch-controller exclusion,
> not a deferral). Browse builds the PRODUCTION IMPL of the player team's already-ratified,
> already-routed `ISurfaceHandshakePort` (player 006). **RQ1 RULED (PM): defer S3 — the focus-ring
> polish (`:focus` + focus/blur events) is a follow-on; ship the handshake.** **RQ2 (the cross-team
> device-id binding) routing cross-team — the PM is sourcing the owner of the in-world device
> targetability; S2 proceeds against a test `dev_*` id, the real targetability the cross-team
> follow-on.** Active scope: **S1 + S2** (S3 deferred).

## Scope Boundary

This sprint makes Cathode screens **interactive**: the player's resolved navigation intent reaches
the active surface's document as a standard `keydown`, and the screen reacts. We build the browse
half of the player↔Cathode handshake — the player side (`ISurfaceHandshakePort`, ratified to six
discrete intents) is built, stubbed, and routed to us as the contract to implement.

**Owns:**
- **The Cathode input-delivery primitive** — a worker-posted synthetic `keydown` on a surface's
  `document` root (the source feeding the already-built `DispatchKeyboardEvent` sink).
- **The Controller emission** — the six ratified intents `{Up,Down,Left,Right,Accept,Back}` mapped
  through a **fixed** intent→`e.key` table (`Up`→`ArrowUp` … `Accept`→`Enter`, `Back`→`Escape`) to
  that synthetic `keydown`.
- **Input Routing** — the delivered event reaches the **designated** surface's document (not a
  broadcast `DriveAll`); the handle→surface resolution.
- **The composition-root adapter** — `FUptimeSurfaceHandshakeAdapter : ISurfaceHandshakePort` in the
  game root (`game/Source/Uptime`), the production impl behind the player's stub→real one-line
  rebind (the `FUptimeConnectIntentAdapter`/submit-port precedent).
- **The firewall-preserving seam** — `ICathodeDeviceInput` declared in **UptimeState** (engine-free
  Core), the device implements it, the adapter consumes it; Uptime never arrows UptimeCathodeDevice.

**Does NOT own (named, with the owner):**
- **Pointer / hit-testing / world→surface mapping** — OUT by design (the couch-controller
  exclusion; `components-input-routing.md`).
- **The `ISurfaceHandshakePort` shape** — ratified by the player+designer (six discrete tokens,
  `NavAxis` dropped); we **implement** it, never redefine/re-ratify it (the reject).
- **The physical→semantic resolution** — Unreal Enhanced Input, the player resolves it; we take the
  already-resolved intent.
- **The in-world device's `dev_*` identity + targetability** — the Cathode device actor carrying its
  agency `dev_*` id and implementing `IPlayerInteractable` is a **player/agency/room** concern
  (**RQ2**, cross-team). Browse builds the registration + lookup against a test `dev_*` id.
- **The dev keyboard→semantic shim** — optional browser/Chromium dev scaffolding; a follow-on.

## Problem

The player's `ISurfaceHandshakePort` is ratified, stubbed (`PlayerSurfaceHandshakeStub.h`,
records-not-renders), and routed to browse — but the browse production impl is entirely unbuilt:
- **Controller emission** is the *deferred* Controller component (`components-controller.md:3` "zero
  code") — the `DispatchKeyboardEvent` sink exists (`CathodeDomNode.cpp:637`, the real
  capture→target→bubble walk) but nothing **sources** a synthetic `keydown` into it, and it must run
  worker-affine (posted, not inline).
- **Input Routing** is absent — no Cathode "active surface" concept (the registry only `DriveAll`s);
  active is player-driven, the adapter owns the handle→surface resolution.
- **The adapter** is unbuilt; the player binds the stub in its ctor with no public rebind seam.
- **The in-world device is unwired** — `ACathodeDeviceActor` exists (028/029) but is output-only
  (samples the egress texture), carries no `dev_*` id, isn't targetable, and has **zero refs** in
  `game/Source/Uptime`.

## Goal

> PM: "the key-events handshake ONLY — the Controller JS emission, Input Routing dispatch, and
> in-world surface actor focus model. The player team's side is already ratified and waiting."

Finishing: the player drives a nav intent in SurfaceFocus → the designated surface's `document`
receives the standard `keydown` → the screen's stock `addEventListener('keydown', …)` reacts. The
stub→real swap is the one-line composition-root rebind.

## Final State

- **`DispatchKeyToSurface(FSurfaceId, key, code)`** — a production composition-surface method that
  POSTS a worker task → repoints `globalThis.document` to that surface → resolves the root node →
  `DispatchKeyboardEvent`. The structured-keydown shape of the existing posted-worker path (not
  test-only arbitrary eval).
- **The fixed intent→key table** — a non-configurable const map in the adapter (the six tokens →
  canonical `e.key`); stock web, no Cathode-specific event API.
- **`ICathodeDeviceInput`** (UptimeState, engine-free) — `DispatchKeyToSurface(stableId, key, code)`
  + the device-id lookup; the device registers its impl in a UptimeState-side provider registry, the
  root adapter resolves by interface (Uptime gains only a Core-only Uptime→UptimeState arrow).
- **`FUptimeSurfaceHandshakeAdapter : ISurfaceHandshakePort`** (game root) — maps the intent → key,
  resolves `FSurfaceHandle.SurfaceId` (a `dev_*` id) → the registered `ICathodeDeviceInput` → dispatch;
  bound one-line at `UptimeGameModeBase.cpp` next to the submit-port bind.
- **`AUptimeCharacter::SetSurfaceHandshake(TUniquePtr<ISurfaceHandshakePort>)`** — the player rebind
  seam (the `SetSubmitPort` pattern), re-binding `OnSurfaceReleased` on swap.
- **The firewall intact** — `Uptime.Build.cs` gains **no** Cathode dep; the vendored engines stay
  confined to UptimeCathodeDevice/CathodeComposition.

## In Scope (ordered slices)

- **Slice 1 — the Cathode input-delivery primitive (fully browse-internal).** Two phases (one slice,
  one boundary). **P1 — make `FDomDocument` an EventTarget** (the audit/docs over-credited this:
  `document` is NOT an EventTarget today — `addEventListener` is bound only on `Node`, and the dispatch
  bubble ends at the root `<html>`, so the document is not in the bubble path; the ratified
  `document.addEventListener('keydown')` does not work). Add `addEventListener`/`removeEventListener`/
  `dispatchEvent` on the `document` global + splice the document at the **top of the bubble path** (a
  `keydown` at the focused element/body bubbles to `document`) — the correct DOM model (focused element
  → bubble → document, a real browser) and the ratified target; the d.ts gains `Document extends
  EventTarget`; **verify no regression to the existing node dispatch.** **P2 — `DispatchKeyToSurface`**:
  the worker-posted synthetic `keydown` delivered at the focused element/body, bubbling to `document`,
  via the existing `DispatchKeyboardEvent`. *Proof — the FULL NEW chain, not the built sink alone:* an
  intent-shaped synthetic `keydown` driven through `DispatchKeyToSurface` fires an author
  `document.addEventListener('keydown', …)` whose `e.key` == the canonical token; each of the six
  intents → its canonical `e.key`. (A test that drives `DispatchKeyboardEvent`→listener in isolation
  re-proves built coverage — the reject; drive intent→key→worker-posted-keydown→bubble→document
  listener. (b) deliver-to-`body` is OUT — deviates from the ratified `document.addEventListener`.)
- **Slice 2 — the handshake end-to-end (browse half; cross-team binding flagged).** The player rebind
  seam (`SetSurfaceHandshake`) + `ICathodeDeviceInput` (UptimeState) + the device registration + the
  `FUptimeSurfaceHandshakeAdapter` (intent→key, handle→device lookup) + the one-line composition-root
  bind. *Proof — the delivery chain end-to-end against a TEST `dev_*` id* (consumer-driven, the player
  S6-against-stubs precedent): `DeliverSemanticEvent(intent)` through the real port → the adapter →
  the registry lookup (test `dev_*`) → `DispatchKeyToSurface` → the surface's `document` `keydown` →
  an author listener fires; `DesignateActiveSurface`/`ReleaseActiveSurface`/`OnSurfaceReleased`
  round-trip; the stub→real one-line rebind; **`Uptime.Build.cs` zero Cathode dep** (the firewall).
  The real player-ray targetability (the device carrying its `dev_*` id + `IPlayerInteractable`) is the
  **RQ2 cross-team follow-on**, not this slice.
- **Slice 3 — focus-model polish (CONDITIONAL on RQ1).** The host `:focus` pseudo-selector + focus/blur
  event dispatch (so the focused element is host-styleable + focus is observable). *Proof:* `:focus`
  matches the `activeElement`; `element.focus()` dispatches a `focus` event. **Cuttable** — the
  handshake works without it (the app moves focus via `element.focus()`, which exists). In only if the
  PM rules it in (RQ1).

## Non-Goals

- Pointer / hit-testing / cursor / world→surface mapping — OUT by design.
- Redefining or re-ratifying the six-intent `ISurfaceHandshakePort` — implement as ratified.
- Breaking the Uptime-Cathode-free firewall — the adapter deps the engine-free `ICathodeDeviceInput`
  seam, never UptimeCathodeDevice/CathodeComposition (do not pull the vendored engines into the main
  module).
- Wiring the Cathode device actor to a `dev_*` manifest entity / making it targetable — RQ2,
  cross-team.
- The dev keyboard→semantic shim — follow-on.
- Re-proving built coverage — the `DispatchKeyboardEvent` sink (drive the full chain), the #27
  multi-surface fan, the ratified port.

## Starting Surface (audit — the dev's, confirmed)

- **The sink (BUILT):** `FDomNode::DispatchKeyboardEvent` (`CathodeDomNode.cpp:637-656`, real
  capture→target→bubble, `bBubbles` honored); `FDomKeyboardEvent{Event,Key,Code}` (`CathodeDom.h:69`);
  the KeyboardEvent JS class exposes live `e.key/e.code/e.type` (`:1816`); proven a host `keydown`
  fires an author listener (`CathodeDomEventTests.cpp:626`). *(The player doc cites `:325` — a stale
  line-drift; the real site is `:637`.)*
- **The contract (BUILT, ratified):** `ISurfaceHandshakePort` (`PlayerSurfaceHandshake.h:58`);
  `FSurfaceHandle{FString SurfaceId}` (`:15`); `ESurfaceInputKind{Up,Down,Left,Right,Accept,Back}` +
  `FSurfaceInputEvent{Kind}` (`:30`); the `[v1 stub]` records-not-renders. The player drives it from
  resolved nav (`UptimeCharacter.cpp:905`); the stub binds in the ctor (`:166`) — **no public rebind
  seam yet**.
- **The precedent (copy 1:1):** `FUptimeConnectIntentAdapter : ISubmitInteractionPort` in
  `game/Source/Uptime`, holds the provider non-owning, bound one-line at `UptimeGameModeBase.cpp:54`.
- **The firewall:** `Uptime.Build.cs:9-34` no Cathode dep; `CathodeComposition` publicly carries the
  litehtml/Blend2D engines (`:16`), `UptimeCathodeDevice` publicly deps `CathodeComposition` (`:14`) —
  so Uptime must never dep UptimeCathodeDevice. `ICathodeDeviceInput` in UptimeState (Core-only)
  inverts it.
- **The device (BUILT, unwired):** `ACathodeDeviceActor`/`UCathodeDeviceSubsystem` output-only (sample
  the egress texture); **no `dev_*` id, not `IPlayerInteractable`, zero refs in `game/Source/Uptime`.**
- **The id taxonomy:** the player HitObjectId is the `dev_<6 base32>` `FStateObjectId` join key
  (`StateObjectId.h`); `ARoomActor` stamps it from the authored manifest. The handshake uses that exact
  shape — no id to invent; the device just isn't bound to one yet (RQ2).
- **Focus (state BUILT, polish absent):** `element.focus()/blur()` (`:484`), `document.activeElement`
  (`CathodeDomDocument.cpp:503`), `FocusedNodeId` (`CathodeDom.h:361`) exist; **`:focus` pseudo absent,
  focus/blur events not dispatched** (RQ1).

## Method

- **Consumer-driven against a test id (the player-S6 precedent).** Slice 2 proves the browse-ownable
  delivery chain end-to-end with a TEST `dev_*` id; the real-device targetability binding is the RQ2
  cross-team follow-on (we do not block on it, the same shape the player used against our stub).
- **The firewall is dependency-inverted.** The adapter consumes `ICathodeDeviceInput` (engine-free,
  UptimeState); the device registers its impl; the root resolves by interface and never constructs a
  UptimeCathodeDevice type. Prove `Uptime.Build.cs` stays Cathode-free.
- **Prove the full chain, never the sink alone.** Slice 1's discriminator drives
  intent→key→synthetic-keydown→listener (the built `DispatchKeyboardEvent` is the downstream sink).
- **Prove it in PIE.** Slice 2's integrated leg: the player's resolved intent → the keydown on the
  designated surface → the screen reacts; a `[BROWSE-S030.x]` stamp in a settled, post-build run.
- **Worker-affine.** `DispatchKeyToSurface` posts to the worker (the doc lives there); the
  `SetPendingMutationForTest` posted-worker path is the shape, made production + structured.

## Acceptance

- **Slice 1 — the delivery primitive.** Each of the six intents, dispatched via `DispatchKeyToSurface`,
  fires an author `document` `keydown` listener whose `e.key` == the canonical token (`ArrowUp`/…/
  `Enter`/`Escape`); the full intent→key→synthetic-event→listener chain (not the dispatch unit alone).
  **The proof drives the WORKER-POSTED `DispatchKeyToSurface`** (post → repoint `globalThis.document` →
  resolve root → dispatch), **not** an inline `DispatchKeyboardEvent` call — an inline call is the
  sink-in-isolation reject (it skips the worker-affine post + the document-repoint, the actual new
  code). `e.key` is read off the LIVE `KeyboardEvent` in the listener, not asserted C++-side. No
  regression (the full Cathode suite).
- **Slice 2 — the handshake end-to-end (browse half).** `DeliverSemanticEvent(intent)` through the
  REAL `ISurfaceHandshakePort` → the adapter → the registry lookup (test `dev_*`) →
  `DispatchKeyToSurface` → the surface's `document` `keydown` → an author listener fires (the screen
  reacts); `DesignateActiveSurface`/`ReleaseActiveSurface`/`OnSurfaceReleased` round-trip; the
  stub→real swap is the one-line rebind; **`Uptime.Build.cs` zero Cathode dep** (the firewall, grep).
  **The proof shows the real adapter is USED, not merely bound** — a delivered intent must LAND on the
  bound `FUptimeSurfaceHandshakeAdapter` and drive through to the surface `keydown` (the
  submit-lands-on-a-distinct-double discipline; a `SetSurfaceHandshake` that binds the adapter while
  delivery routes elsewhere passes a `bound==adapter` check yet is dead). **The test `dev_*` is an
  explicit STAND-IN** — the proof must NOT assert "the player-ray targets a screen" / wire a fake
  `IPlayerInteractable` and call targetability done (that is RQ2's real binding). The PIE integrated
  leg shows the player-driven intent landing on the designated surface.
- **Slice 3 — focus polish (if RQ1 in).** The NEW `:focus` pseudo-selector resolves to the built
  `activeElement` (a wrong/absent selector — matching nothing or the wrong node — REDs; non-circular:
  it asserts the new selector against the already-built focus state, not the state itself);
  `element.focus()` dispatches a `focus` event observed by an author `addEventListener('focus', …)`.
  (Out if RQ1 defers it.)
- **Dep-confinement intact** (Uptime Cathode-free); **no regression** (the full Cathode suite + the
  prior baselines); the ratified port unredefined.

## Review Questions (promoted to the PM)

1. **RQ1 — focus-model scope (sequencing).** The handshake is keydown-delivery + the app's own
   `element.focus()` (which exists); the host `:focus` pseudo + focus/blur events are polish the
   handshake works without. **Lead lean: defer them (Slice 3 a follow-on); ship the handshake.** In
   only if you want the host focus model (a styleable focus ring, observable focus) in 030. Sets Slice
   3 in/out. *(Non-blocking: Slices 1–2 are the core regardless.)*
2. **RQ2 — the cross-team device-id binding.** The id is real and shared (`dev_*` `FStateObjectId`),
   but the in-world Cathode device actor carries no id, isn't `IPlayerInteractable`, and has no
   manifest link — so the real player-ray-targets-a-screen path can't close browse-internal. **Who
   owns wiring the in-world Cathode screen to a `dev_*` manifest device** (carry the id + be targetable
   — the room placer / agency)? Browse builds the registration + lookup + adapter against a TEST `dev_*`
   id (Slice 2); the real targetability binding is this cross-team follow-on. Confirm the owner +
   whether it lands in/after 030.

## Forks resolved in-group (recorded)

- **Firewall (adapter placement) — preserve it.** `ICathodeDeviceInput` in UptimeState (engine-free),
  dependency-inverted registry; Uptime stays Cathode-free (the ratified confinement, components.md:136).
  Not a firewall break.
- **Dev keyboard→semantic shim — follow-on** (optional scaffolding, not the production path).
- **Slice 2 against a test `dev_*` id — yes** (the consumer-driven precedent; the real binding is RQ2).

## Outcome

030 builds the browse production impl of the player's ratified `ISurfaceHandshakePort` — the key-events
handshake adapter, making Cathode screens interactive. The build (S1 + S2; S3 deferred per RQ1) is
delivered, each slice box-green, independently reviewer-accepted, and scoped-committed:

| slice | commit |
| --- | --- |
| S1 — the document EventTarget + the worker-posted keydown primitive | `9ab7a7d` |
| S2 — the input handshake adapter through the dependency-inversion firewall | `1fe19d7` |

**The handshake works end-to-end:** the player's nav intent → the composition-root adapter (the fixed
intent→key map) → `Resolve(dev_*)` through the engine-free registry → the real device's `DeliverKey` →
`DispatchKeyToSurface` → the surface's `document` keydown → the author listener (six canonical `e.key`s).
The vendored-engine firewall holds — Uptime stays Cathode-module-free; the adapter drives the device only
through the engine-free `ICathodeDeviceInput` seam in UptimeState.

**Deferred / follow-on (per the rulings):** S3 (the `:focus` + focus/blur polish — RQ1, PM-deferred; the
handshake works without it). RQ2 (the cross-team device-targetability binding — the in-world Cathode device
carrying its agency `dev_*` id + `IPlayerInteractable`; the PM is routing the owner; S2 proves against a
test `dev_*` stand-in marked RETIRE-in-RQ2). The rebind seam's behavioral proof (a screen-initiated
`OnSurfaceReleased` → the player exit) is PIE/integration-level. The dev keyboard→semantic shim (a follow-on).

**Cross-team:** the `SetSurfaceHandshake` rebind seam is a small edit to UptimePlayer (the player's S6
anticipated the rebind), flagged to the PM. The cable red (`Uptime.Cable.Section.EndpointSlack.Dispatch`) is
provably out-of-S2-surface (no manifest file, no code path) — routed to the cable team via the PM. Cathode
suite at close: **150/150, 0 Fatal.**

## Dev Retrospective (Mundo)

Durable lessons from building the 030 input-interaction handshake (S1 `9ab7a7d` + S2 `1fe19d7`).

1. **A ratified contract can name an unbuilt capability — ground it against the CODE, not the doc.** The
   handshake's ratified target was `document.addEventListener('keydown')`, and the audit + docs read as
   though it worked — but the document was NOT an EventTarget and not in the bubble path. A ratified name is
   not a built feature; when a contract names a capability, verify the capability EXISTS in the code before
   scoping the work that consumes it.
2. **A "carry-trigger" prediction can be wrong — require a reclaim-direction PROOF for a gc_mark re-add.**
   The event-API binding seemed to meet the carried trigger, so the re-add looked due — but the required
   reclaim-direction proof crashed with the predicted UAF (the null-finalizer document outlives its wrapper),
   the narrowed listeners-only mark didn't close it (structural to the ownership), and it was UNNECESSARY
   (the document is always globalThis-rooted). A documented carry-trigger is a hypothesis; a GC-mark re-add
   demands a reclaim-direction proof — it catches a structural UAF a survival test passes.
3. **When the firewall forbids naming a type, invert via an engine-free interface + a registry keyed by the
   join-id.** No resolve-by-interface precedent existed; the obvious concrete-subsystem resolve would re-link
   the engines into the root. The forced firewall-safe seam: `ICathodeDeviceInput` + a `dev_*`-keyed registry
   in engine-free UptimeState — the device registers, the root resolves BY INTERFACE. Pair it with lifetime
   hygiene (overwrite-on-register, null-on-absent) to close the static-registry-of-raw-pointers hazard.
4. **A composition of unit-halves is necessary but not sufficient — the JOINED test drives the real wiring.**
   The split (adapter→a double; device→surface) covers the join only by composition; the joined test drives
   adapter→the REAL device→surface in one TU. Keep the isolating halves (they pin each link) AND add one
   joined test on the real production path — the composition-only inference is the masking risk.
5. **THE COST SIGNAL — a cross-firewall end-to-end test is expensive; budget for it.** The joined needed
   THREE test-only seams (a device→root test-dep, a non-Shipping include-path, a WITH_DEV factory). Each is
   principle-aligned; the ACCUMULATION is the signal — a strict firewall keeps production clean and the
   cross-firewall e2e test costly. Not a smell in any one seam; the price of the firewall — gate every seam
   test-only so production never pays.

**The unifying principle: never widen production's surface for a test.** Every seam resolved by it — the
test-only non-Shipping include-path (vs a permanent `Public/`), the WITH_DEV factory (vs UPTIME_API-ing the
concrete adapter), the firewall-grep on dep STRINGS not the word "Cathode" (the only "Cathode" in
Uptime.Build.cs is a comment). The lever is test-only gating (non-Shipping / WITH_DEV_AUTOMATION_TESTS): the
test gets what it needs, the Shipping surface gets nothing new.

## Review Retrospective (uptime.browse-review)

Both slices were re-derived independently from current source + the settled box run; S1's worker-posted
keydown chain was driven through the real worker (RestartWorkerForTesting), and the firewall was read off
the raw Build.cs, never a relayed claim. Durable lessons:

- **Drive the FULL chain, not the built sink in isolation.** S1's primitive (`DispatchKeyToSurface`) feeds
  the *already-built* `DispatchKeyboardEvent` sink, so the only honest proof drives the NEW source end to
  end — intent → the fixed key-map → the **worker-posted** keydown → `globalThis.document` repoint → the
  bubble → an author `addEventListener('keydown')` reading the **live** `e.key` (all six tokens, per-mapped).
  A test that hand-calls `DispatchKeyboardEvent` inline re-proves built coverage and skips the new
  worker-post + document-repoint code — the reject. The new code is the source; the sink is the downstream.

- **The firewall comment-false-match: read the RAW dependency block, never a filtered grep.** Uptime.Build.cs
  carries zero Cathode-module deps, but its *only* "Cathode" tokens are an explanatory comment (the seam
  names `ICathodeDeviceInput`/`FCathodeDeviceInputRegistry` + the "deps ZERO Cathode" rationale). A naive
  `grep "Cathode"` counts=1 on that comment and false-flags a firewall break. The firewall is a property of
  the *quoted module strings* in `PublicDependencyModuleNames`, so it must be read off the raw block with
  comments excluded — the structural-check-reads-raw discipline (a content-filtered grep drops or
  mis-attributes the structure it's meant to check).

- **Composition COVERS; the joined STRENGTHENS — and the seam decides which.** S2's split proved the chain as
  a sound composition: (1) the adapter→a recording double, (2) the real device→the surface, (3) the registry
  resolve — composed via the shared `ICathodeDeviceInput` interface + the same dev_*. Because the interface
  IS the seam, no wrong impl passes the split yet fails the joined, so the split is not a method defect — it
  covers the end-to-end. But the joined test (DeliverSemanticEvent → the adapter → the **real** device →
  the surface, in one) *exercises* the production path the split only *inferred* (no double in the chain) —
  the prove-where-exercised bar. I recommended folding it as a strengthening (not a blocker); the lead took
  it. The reviewer line: a sound composition can be accepted, but a joined that removes the inference is the
  stronger proof and worth the fold when the cost is contained.

- **Cross-firewall test access: the 'never widen production for a test' bar holds at any cost.** The joined
  had to live in UptimeCathodeDevice (the GPU readback) yet reach the adapter in Uptime — and the firewall
  forbids Uptime→Cathode — so it forced **three** test-only seams: a non-Shipping reverse device→root dep
  (acyclic), a non-Shipping `PublicIncludePaths.Add` (an include-path, not a module dep), and a WITH_DEV
  factory returning `ISurfaceHandshakePort*` (the concrete adapter stays **unexported** — no UPTIME_API).
  Each was chosen over the production-widening alternative (UPTIME_API-the-class, move-header-to-Public/),
  and each is non-Shipping/WITH_DEV-gated so a Shipping build gains nothing. The accumulation ("small add" →
  three seams) is the real cost of a cross-firewall end-to-end test — acceptable precisely because every
  seam is production-untouched, verified by confirming each gate is Shipping-excluded.

- **A safety proof catches a UAF a survival test would miss.** S1's document-EventTarget binding *looked
  like* the gc_mark carry re-introduction trigger; re-adding gc_mark crashed the box (UAF 0x30). The required
  proof was not "the cyclic listener survives" (vacuous — unmarked refs over-retain) but the SAFE invariant:
  the listener on an always-globalThis-rooted document survives GC **without** UAF — which the re-add breaks.
  So the carry trigger was falsified by proof (a rooted document needs no mark; `Def.GcMark` stays nullptr),
  the over-retention test bites the re-add via the crash, and the shared gc_mark memory is reaffirmed:
  document.gc_mark STAYS REMOVED, re-add is UAF-unsafe.

## Lead Retrospective (uptime.browse-lead)

1. **Surface the design-direction fork before planning; check the documentation first.** The sprint arrived
   as "pointer and key events, hit-testing" — but `components-input-routing.md` defers pointing
   *indefinitely* (a settled couch-controller exclusion) and the player's ratified handshake is non-positional.
   Rather than plan on sand or refuse, I surfaced the fork to the PM with the grounding (the design-of-record
   disposes pointer the opposite way; the key-events handshake is ready and routed); the PM confirmed
   key-events-only. A fork the design-of-record settles is the human's to rule, not mine to barrel through.
2. **A recalled hazard is a hypothesis — require it AS A PROOF.** The gc_mark carry-trigger looked due when
   the event API bound; I required the reclaim-direction safety proof (not a vacuous survival test), and it
   crashed with the predicted UAF — catching a structural hazard before it shipped. The memory named the
   risk; requiring the proof, not the claim, is what caught it.
3. **Settle the in-group forks; raise the genuine ones with a lean.** The firewall pattern (the
   dependency-inversion registry) settled in-group — it's the *forced* firewall-safe mechanism, the
   design-of-record's confinement, not a free convention. The focus scope (S3), the input direction, and the
   cross-team touchpoints (the rebind seam, RQ2, the cable red) went to the PM — sequencing and cross-team
   coordination are theirs.
4. **Refuse the production-widening shortcut at every grain.** The cross-firewall joined test surfaced three
   cheaper alternatives — move-header-to-`Public/`, UPTIME_API-the-class, a naive firewall grep — each
   widening production or false-flagging. I refused each on "never widen production for a test"; test-only
   gating (non-Shipping / WITH_DEV) gives the test what it needs and production nothing new.
5. **Concede and re-weigh when the cost surfaces — but don't revert a sound proof for its scaffolding.** I
   folded the joined test (prove-where-exercised); the dev then revealed it needs three test-only seams. I
   re-weighed rather than reflexively holding the fold *or* reverting: the seams are all test-only and
   principle-aligned, so the cost is acceptable and the accumulation is a retro lesson (cross-firewall e2e
   tests are expensive), not a reason to drop the stronger proof.
