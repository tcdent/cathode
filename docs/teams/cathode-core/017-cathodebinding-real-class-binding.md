# Sprint 017 — CathodeBinding: Real C++↔JS Class Binding

Prove CathodeBinding's **C++→real-JS-eval→C++ chain** over 016's live runtime: a native class is
constructed, **handed through a real `Evaluate`**, and accessed from real JavaScript — the wrapped
object reaching author JS and a C++-computed value surfacing back. The host-bridge layer of the build-out.

> Scope from the PM (tcdent-approved, run-through cadence): "CathodeBinding — C++<->JS class binding over
> the real runtime. Complete the production binding path. Prove: a native class constructs and is accessed
> from real JS."

## Scope Boundary

**This sprint owns** the binding's own (non-DOM) wrapped object **passing through a real JS eval**:
construct a native instance (re-using the proven register/construct path), hand it to real `Evaluate`
(real JS holds/reads/returns it / calls its native method), and recover a C++-computed value back — over
016's real engine.

**Premise correction (dev-grounded, 015-baseline style):** the doc's first-cut overstated
"engine-unverified / first time." `Cathode.Binding.Registry.OpaqueRoundTrip` **already** drives the real
engine (FScopedTestRuntime → `RegisterClass` (real `JS_NewClassID`/`JS_NewClass`) → `CreateWrappedObject`
(real `JS_NewObjectProtoClass`/`JS_SetOpaque`) → `JS_GetOpaque`), green in 015 (105/105) and 016
(107/107); marshal and callable also run real and green; the FBoundMember method/prop install is
engine-verified via the CathodeDom consumer. So register/construct/opaque/marshal/callable are **already
proven**. The genuine 017 gap: the existing test is C++→wrapped→C++ — **the wrapped object is never handed
to a real JS eval**. 016's real `Evaluate` unlocked that; 017 proves it on the binding's own class.

**Out of scope (named):**
- **C++ FUNCTIONS → JS namespace** (COVERAGE Deferred #1) — a C++ *free function* in the JS namespace
  (`game.foo()`, the host-function-binding convention + engine↔app delineation, human-deferred). RQ-2
  resolved: FBoundMember installs *instance methods/props on a class* (`JS_NewCFunction` on the
  prototype) — a **class-instance method ≠ a namespace free function**, so FBoundMember is the *access
  mechanism* 017 uses (engine-verified via DOM), **not** Deferred #1. The convention stays deferred; no
  scope fork.
- **Real GC cycle reclamation** (COVERAGE Deferred #2) — RQ-3, the one open scope question (below):
  feasible now (016's live engine + `JS_RunGC`) but distinct from "constructs + accessed"; dev + lead
  lean follow-on, reviewer leans prove-now-if-cheap → the PM's scope call.
- No DOM consumer wiring; no new type system (the locked RQ2 ruling — expose quickjs-ng's conventions).

## Problem

The binding (sprint 007 + FBoundMember) is engine-verified for register/construct (green 015/016) but its
own wrapped object has **never passed through a real JS eval** — real JS never holds, reads, returns, or
calls a method on it (the 007 + Registry tests are C++↔C++ or direct-callback). The real-GC path
(`JS_MarkValue`/`JS_FreeValueRT`/`JS_GetAnyOpaque`, the gc_mark+finalizer) is exercised **directly** by
the Teardown test (callbacks invoked by hand), **never by the real collector** (Deferred #2). Of the 14
TODO-PREBUILD signatures, all but those three are already exercised over the real engine; they are
resolved-stale markers.

## Goal

Over 016's real runtime: the binding's wrapped object passes through real `Evaluate` and is accessed from
real JS, proven **discriminatingly** by a value the C++ side **computed** (not echoable from the JS
input) surfacing through the full C++→JS→C++ chain — biting a broken binding (wrong signature / null
opaque / stale class-id), not a no-op that "constructs a plain object" and passes a shallow check.

## In Scope (ordered slices — dev-shaped; NOT a re-proof of register/construct)

1. **Opaque round-trip THROUGH real JS.** Construct a wrapped instance (proven path) → set it on a JS
   global / pass it to a JS function via real `Evaluate` → real JS holds/returns it → C++ recovers the
   opaque (`JS_GetOpaque`) == the original, and reads back the instance's **C++-computed field**.
   Discriminating: the field is `f(seed)` computed C++-side, surfaced after real JS held the object.
2. **Method/prop access from real JS (FBoundMember).** The instance is **C++-constructed**
   (`CreateWrappedObject` with the opaque field=6 — the proven construct path) and **set as a JS global
   `o`** (the binding has **no JS-callable constructor** — `new K()` is unbuildable; objects reach JS
   only via C++-side `CreateWrappedObject` + `JS_SetOpaque`); then
   `Evaluate("if (o.compute(7) === 42) throw new Error('got:'+o.compute(7))")` (throw-ON-success, so the
   computed value surfaces in the throw) where `compute = arg * opaque-stored field` (7*6=42, computed
   C++-side, reading the opaque via the FBoundMember thunk). REAL → `42===42` → throws "got:42" →
   `bThrew==true` + `Message.Contains("42")`; a stub (compute undefined) / echo (the arg 7) → `===42`
   false → no throw → `bThrew==false` → the `TestTrue(bThrew)` FAILS → bitten. The whole chain:
   C++-construct (opaque field) + set-global + access (native method via real JS) + C++ computes +
   result→JS. *A JS-callable constructor (`new K()` =
   `JS_CFUNC_constructor` + `JS_SetConstructor`) is NEW SCOPE, out of 017 — flag to the PM if wanted.*

## Non-Goals

- No C++ functions→JS namespace (Deferred #1) / the host-function-binding convention / a free function in
  the namespace. FBoundMember instance-method access is in scope; the *convention* is not.
- No real-GC reclamation (Deferred #2) unless the PM pulls RQ-3 in as a 3rd slice.
- No DOM wiring; no new type system; do not delete the 007 direct-callback bookkeeping tests (the
  vendor-independent guard — the real-engine tests are additive).

## Starting Surface (the audit the dev produced)

The 14 TODO-PREBUILD split: **already-exercised over the real engine** (Registry/Marshal/Callable/DOM,
green) — `JS_NewClassID`, `JS_NewClass`, `JS_NewObjectProtoClass`, `JS_SetOpaque`, `JS_GetOpaque`,
`JS_Call`, `JS_IsProxy`, `JS_GetTypedArrayType`, `JS_GetClassID`, FBoundMember's `JS_NewCFunction`/`2`;
**not via the real collector** — `JS_MarkValue`/`JS_FreeValueRT`/`JS_GetAnyOpaque` (Deferred #2). The
signature *shapes* are in-tree (the TODO-PREBUILD comments cite the vendored `quickjs.h` lines); the
*semantics* (what they do internally) live in the non-vendored `quickjs.c` → inference.

## Method

Real-engine tests over `CreateJsRuntime()` + `Evaluate` (016). Prove **discriminatingly** — a C++-computed
value (NOT a substring/echo of the JS input) surfaces through C++→JS→C++. Verify the 14 signature shapes
against the vendored `quickjs.h`; the chain itself is the in-tree gate (a wrong shape breaks it); flag the
non-vendored-`.c` semantics as inference ([[reference_firewall_source_dispositive]]). Run the FULL
CathodeBinding suite (the 007 direct-callback guards stay green). Coherence per the change shape.

## Acceptance (reviewer-formulated)

- **Constructs + accessed (the chain):** the wrapped object passes through real `Evaluate` and a
  C++-computed value surfaces back — the soundest is the FBoundMember native method (`o.compute(7)==42`,
  `arg*field` computed C++-side). **PRIMARY reject:** a "constructed OK" / `instanceof`-only / `bThrew==false`
  assertion (vacuous — a no-op binding constructs a plain object; `instanceof` can pass on a wrongly-grafted
  class).
- **Exact per-instance opaque:** **TWO** instances with **distinct** computed fields → each returns its OWN
  field (a shared/null/stub ptr → both return the same → FAILS; a single instance can't tell exact from a
  lucky shared).
- **Fresh-per-runtime class-id (the 012 hazard):** the construct+access works across **MULTIPLE**
  `FScopedTestRuntime` — the [[reference_quickjs_class_id_per_runtime]] AV fires on the 2nd+ runtime
  (a stale process-global id grafts the exotic onto a foreign class → `JS_GetOpaque` reinterprets → AV);
  a first-runtime-only proof masks it.
- **Class-id-checked GetOpaque (negative):** `JS_GetOpaque` on a **wrong-class** object (a plain `{}` /
  another class) → NULL, handled safely (no reinterpret/AV).
- **The 14 vendor signatures:** shapes re-derived vs the vendored `quickjs.h`; the chain exercises them
  (a wrong signature → the computed value doesn't surface → RED); the `.c` semantics flagged inference.
- **No regression:** the full CathodeBinding suite green — the 5 007 direct-callback tests (Registry/
  Teardown/CallableHandle/JsValue/Marshal) STAY (vendor-independent bookkeeping guard); the real-engine
  tests are additive. Gate BOTH.
- **Leak:** bounded-growth (N construct+access cycles incl the wrapped object + opaque → malloc_size
  post-`JS_RunGC` ≤ baseline+bound, not byte-exact) + source-trace (every `JS_DupValue` matched by
  `JS_FreeValue`; the finalizer-safe release; no double-free).
- **Coherence:** the new real-engine construct+access test present+Success is the bites-a-broken-binding
  marker (wrong signature/null opaque/stale class-id → RED; green ⇒ the binding works over the live engine).
- **COVERAGE.md updated** at close — the engine-verified status; Deferred #2 resolved (if RQ-3 in) or
  reaffirmed; Deferred #1 reaffirmed (FBoundMember ≠ it).

## Review Questions

1. **Access semantics** — RESOLVED: FBoundMember instance-method access is the in-scope mechanism (the
   soundest C++→JS→C++ proof); it is *not* Deferred #1 (a namespace free function), so the
   host-function-binding convention stays deferred — no scope fork.
2. **FBoundMember vs Deferred #1** — RESOLVED (RQ-1): instance method on a class ≠ free function in the
   namespace.
3. **Real GC reclamation (Deferred #2)** — **OPEN, the PM's scope call.** Feasible now (016's live engine
   + `JS_RunGC`); if in, it MUST be the **reclaim-direction** test (a refcount cycle + drop every external
   handle + release the root + real `JS_RunGC` → assert RECLAIMED / finalizer fired, WITH a
   leaks-without-release control — reject a vacuous survival test, [[reference_quickjs_gcmark_reclaim_not_survival]]);
   it also exercises the finalizer's `JS_GetAnyOpaque` + class-id (the 012 hazard, best caught while live).
   Dev + lead lean **follow-on** (distinct from "constructs + accessed"); reviewer leans **prove-now-if-cheap**.
   Routed to the PM: a 3rd 017 slice vs a tracked follow-on.
4. **The 14 vendor signatures** — RESOLVED: all but the three GC/finalizer signatures are already
   exercised over the real engine; the in-tree gate is the chain, the `.c` semantics are inference.

## Outcome

017 proved CathodeBinding's **C++↔JS class binding over the real runtime** — a native class constructs
and is accessed from real JavaScript, the engine-unverified gap the binding (authored sprint 007) had
left. The register/construct/opaque round-trip, marshal, and callable were *already* engine-verified
(green 015/016); 017 added the chain **through a real `Evaluate`**.

**Commits (in order):**
- `4a63a16` — **opaque round-trip through real JS (slice 1).** The wrapped object transits a real
  `Evaluate`: C++-construct (computed field) → set a JS global → real JS does a genuine engine operation
  (holds both in an array, validates `length===2`, moves them back by index) → C++ recovers the **exact
  per-instance** opaque + the distinct fields (10/26). Two instances bite a shared/null opaque; ≥2
  sequential runtimes bite a stale class-id (the 012 hazard, class-id confirmed local-per-runtime);
  wrong-class → NULL.
- `d0d829b` — **FBoundMember method access (slice 2).** A native instance method (`o.compute(arg)`,
  `InstallBoundMembers`/`JS_SetClassProto`) is called from real JS; the thunk reads the opaque
  (`JS_GetOpaque2`) + the JS arg → `arg*field`. The **both-factors** product (`7*6=42` AND `5*10=50`,
  varying both axes) bites all three degenerate thunks (constant, arg·const, field·const); ≥2 runtimes
  bite the per-runtime proto path; install-before-create (the DOM-proven ordering) on a non-DOM class.
- The close: this doc's Outcome + Postmortem and the updated `CathodeBinding/COVERAGE.md`
  (engine-verified status; Deferred #1 reaffirmed; #2 the follow-on; #3 resolved).

**Forks resolved:** RQ-1/RQ-2 (FBoundMember installs *instance methods on a class*, categorically ≠
Deferred #1's *namespace free functions* — no scope fork; the host-function-binding convention stays
deferred); RQ-3 (real GC reclamation — tcdent/PM ruled a follow-on, distinct lifetime proof out of
scope). Three lead formulation errors were caught + fixed before any build: the no-JS-constructor
reframe (the binding has no JS-callable constructor — C++-side construct), the inverted throw-on-success
operator (`===` not `!==`), and the incomplete one-axis constant guard (the reviewer's both-factors fix).

**Acceptance met:** a native class constructs + is accessed from real JS, discriminatingly; full Cathode
gate 109; the 007 direct-callback bookkeeping tests stay green (additive); the player-team's shared-build
retire kept out of the browse commits (the authoritative-run log-discipline held twice).

**Deferred / tracked (COVERAGE.md):** Deferred #1 (C++ functions→JS namespace / the host-function
convention); Deferred #2 (real GC cycle reclamation — feasible now, the follow-on).

## Postmortem

**Lead (uptime.browse-lead).** This sprint's formulation caught more lead error than any other, and that
is the lesson: my first-cut overstated the binding as "engine-unverified / first-time" (the dev
source-corrected it, 015-baseline style — register/construct were already green, the gap was the chain
through real JS), and my discriminating proof carried three flaws the team fixed before a build — a
proof against a JS constructor the binding doesn't have, an inverted `!==` that makes success silent
(bThrew==false, the primary reject), and a one-axis constant guard that leaves a one-factor degenerate
unbitten. Each was a *future-shaped* call (what proof will bite?) I got confidently wrong; the dev and
reviewer, reasoning from source and from the degenerate-form geometry, got them right. The seat's
takeaway: state the discriminator, then *let the proof-owners stress it* — my job was to route the
stress (premise→dev, acceptance→reviewer) and arbitrate, not to be right about the bite. And the
cross-team wrinkle (env's shared builds with a player retire) is handled by the reviewer's
authoritative-run discipline + a single-file commit, not by trusting the cited log.

**Dev (uptime.browse-dev, Mundo).**
1. **Ground the actual engine-verified state in source before an "unverified/first-time" premise** —
   `Registry.OpaqueRoundTrip` already drove the real engine (green 015/016); the gap was narrower (the
   object never transited a real *eval*). Read the real-engine-vs-direct-callback tests + the run history
   first; the correction reshapes the slices to the real gap.
2. **A value-less eval seam transits through the global object, and the eval must genuinely touch it** —
   Evaluate takes a source string + returns no value, so the object flows through a global and the eval
   does a genuine engine operation (array hold + length-check + move-back), not a bare assignment a
   skeptic could doubt.
3. **To prove a product reads N inputs, vary ALL N** — `o.compute(7)===42` is false-greened by a
   constant and by a one-factor; varying both axes (`7*6` AND `5*10`) bites all degenerate forms.
4. **An instance method on a class ≠ a free function in the namespace** — FBoundMember (`JS_SetClassProto`)
   is the access mechanism; Deferred #1 (the host-function convention) is categorically different and
   stays clean.
5. **The 012 class-id hazard extends to the proto path** — `JS_SetClassProto` is per-runtime, so a
   ≥2-sequential-runtime gate is the regression-guard (a future static cache would AV on run 2).

**Review (uptime.browse-review).** Keeping a binding proof discriminating when the seam fights you: the
**product-proof degenerate-form analysis** is the sharpest — proving `compute` reads both the arg and
the opaque field requires varying *both* inputs, because a single-axis variation bites a bare constant
but leaves `arg·const` (varied-arg passes) or `field·const` (varied-field passes) unbitten (this
corrected the lead's one-axis guard; test-the-breaking-geometry on a product). The **transit-channel**
lesson: a value-less eval seam can't use an in/out value channel — the object round-trips via the global
object and the eval must genuinely touch it. The **authoritative-run log-discipline** was load-bearing
*twice* on env's shared cross-team builds — gate the run that postdates the browse recompile AND has the
marker green, rejecting a stale later session, a superseded shared build, and a cross-team module's run.
Plus: a directly-registered class's class-id (and its proto) must be proven fresh across ≥2 sequential
runtimes (the 012 AV fires on run 2, masked by a first-runtime-only proof), and the thunk *source* must
be read to confirm it reads the opaque, not a constant that echo-passes. Next reviewer inherits: a
computed proof varies every input; a value-less seam transits via globals with a genuine engine
operation; gate the post-recompile marker-green run on a shared build; and read the thunk source.
