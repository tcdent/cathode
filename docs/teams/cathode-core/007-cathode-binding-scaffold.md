# Sprint 007 — Cathode JS Binding scaffold

## Nature

A **no-build / no-execution scaffold** sprint, continuing the per-component house pattern
(002 Runtime, 003 Rasterizer, 004 Text, 005 Layout, 006 Surfaces). The component is the
**JS binding layer** — the machinery that makes C++ objects and functions appear to JavaScript
as real JS values and keeps both heaps' lifetimes correct (`docs/browse/components-js-native-bridge.md`).

Unlike Surfaces, this component is **vendor-confined**: it includes `quickjs.h` and calls quickjs-ng
binding primitives, and it needs the live `JSContext` that 002 owns. It therefore is **not**
couples-to-none — the swap boundary for this module is the **module boundary itself**: consumers
(DOM, Host Bridge, Runtime Core) go through CathodeBinding's API, never raw quickjs.

## Scope Boundary

**This sprint owns** the generic JS binding/value/lifetime layer:
- value RAII over the JS refcount (`JS_DupValue`/`JS_FreeValue` discipline);
- **appendable** native-class registration (one generic path to register a native class, attach an
  opaque pointer, and run a finalizer — consumers append their own bindings to it);
- the two generic handle mechanisms quickjs-ng already provides: a **wrapped-native-object** handle
  (`JS_NewClass` + `JS_SetOpaque` + finalizer) and a **held-callable** handle (an owned JS function
  value);
- bounded value **marshaling** (an accept/reject boundary over quickjs-ng's existing value types);
- the **teardown policy** — the deterministic registry of held refs freed at trigger points, with the
  class finalizer as backstop (the heart of the component).

**This sprint does NOT own** (named, with the owner):
- the **DOM surface** or the type "node" — that is the DOM component (a consumer). The binding layer
  provides the generic wrapped-native-object path; DOM *appends* its node class to it.
- the **host / `game.*` API** — that is the Host Bridge component (a consumer). It *appends* its host
  functions and game-object classes to the same registry.
- the **actual teardown trigger points** (`removeChild` / `removeEventListener` / node-destroy) — the
  DOM provides those; they are a **deferred sibling** (DOM does not exist yet). The binding layer
  builds its own side (the held-ref registry + the explicit-free mechanism + the finalizer hook) now.

**Sibling evolution:** 002 Runtime Core owns runtime/context **lifecycle + tick + budgets**. This
sprint evolves 002 with exactly **one** new public accessor that hands out its context as an opaque
`void*` so the binding layer can reach the live `JSContext` without leaking `JS_*` types into 002's
public header.

## Problem

002's `ICathodeJsRuntime` seam is deliberately value-free and vendor-handle-free — its `FEvalResult`
carries no produced `JSValue`; its own comment states *value handling belongs to the binding layer,
Runtime Core only needs to know whether evaluation threw*. So **all** binding surface (class IDs,
opaque pointers, finalizers, value RAII, marshaling) is **greenfield** — nothing in Runtime Core does
it. Author code (DOM nodes, the `game.*` API) needs C++ things to appear and behave as ordinary JS,
with correct cross-heap lifetime. That lifetime is **the system's #1 correctness hazard**: a native
node that holds a listener closure capturing the node's JS wrapper forms a reference cycle spanning
both heaps that quickjs-ng's collector cannot see through — mishandled, it leaks or frees a C++ object
while JS still points at it.

## Goal

A thin, correct binding layer that **exposes quickjs-ng's own conventions** — generic appendable
native-class registration, value RAII, bounded marshaling, and a deterministic teardown policy —
authored complete, tested against our own bookkeeping, with the DOM trigger-wiring and the
GC-observable behaviors deferred.

> "binding not bridge with the ability to append bindings"

> "We are exposing the spec that is provided by the JavaScript runtime that we're using"

The layer does not invent a type system or enshrine consumer-specific types; it provides the one
generic mechanism every consumer appends to. On evolving the accepted 002 scaffold:

> "I'm not concerned about evolving 002's scaffolding as we learn more. I'm more concerned with
> designing a correct system that's not overly verbose but is still well defined."

On the wrapper-vs-hand-roll decision: both mature C++ wrappers (quickjscpp, quickjspp) target Bellard's
**original** quickjs, not our vendored **-ng** fork, and break on signature divergences confirmed
against the staged header (`JS_NewClassID` is 2-arg in -ng, `JS_SetOpaque` returns `int`). No
maintained standalone -ng wrapper exists. "Assemble" would mean fork-patch-and-maintain a ~2200-line
wrapper built for the wrong fork as a new vendored dependency — against a small, specific surface.
**The layer is hand-rolled** as a thin RAII exposure of the staged -ng signatures; quickjspp is kept
as a reference pattern, not a dependency.

## Final State

1. **Module `CathodeBinding`** established — vendor-confined (includes `quickjs.h`); `Build.cs` depends
   on `Core` + `CathodeRuntime` and references the vendored quickjs-ng ThirdParty block, which stays
   **commented** per the no-build convention (first compile is the execution sprint). Quarantine
   intact: `.uplugin` `Modules[]` gains `CathodeBinding`; plugin `EnabledByDefault:false`; `Cathode`
   absent from `game/uptime.uproject` `Plugins[]` — disabled-not-compiled.
2. **002 gains exactly one** public accessor returning its context as an opaque `void*`; 002's public
   header stays free of `JS_*` types; the rest of accepted 002 is byte-identical.
3. **Value RAII** — a holder that owns a `JSValue` and enforces `JS_DupValue`/`JS_FreeValue` on
   copy/move/destruction (manual refcount leaks are the classic embedding bug; RAII makes it safe).
4. **Appendable native-class registration** — one generic mechanism to register a native class (class
   ID + `JSClassDef` + finalizer), attach an opaque pointer, and create a wrapped JS object; consumers
   append their classes/methods to it. The layer names no consumer-specific type.
5. **Held-callable handle** — owns a JS function value, invokes it, and RAII-manages its refcount
   (e.g. a listener held native-side).
6. **Bounded marshaling** — convert the accepted vocabulary to/from `JSValue`: number, string, bool,
   null/undefined, array, record, plus the two generic handle mechanisms; reject at the boundary with
   a clear error: Symbol, BigInt, typed-array/ArrayBuffer (buffer data crosses Surfaces/Egress, not
   this layer), Date, RegExp, Proxy, cyclic records, arbitrary class instances.
7. **Teardown policy** — a per-native-object registry of the JS refs that object holds; at a teardown
   trigger the layer explicitly frees those refs, breaking the cross-heap cycle at a known point
   (deterministic primary). The **class finalizer** is the secondary backstop (it cannot break a live
   cycle alone, so explicit teardown is the real fix). The trigger **callers** are deferred (DOM).
8. **Tests** (recording/bookkeeping, 006-style) for the bespoke logic: class-registration round-trips
   an opaque pointer; value RAII balances dup/free across scope/copy/move by our free-count; held-
   callable balances its refcount; marshaling accepts the vocabulary and rejects each exotic kind;
   the teardown registry frees an object's held refs on a simulated removal trigger by our registry
   bookkeeping. **Zero assertions on quickjs-ng GC internals; zero assertions on vendor output.**
9. **`CathodeBinding/COVERAGE.md`** — every doc responsibility covered-or-deferred with
   reason/dependency/unblocker/first-exec-risk. Deferred: the **finalizer-fires-on-GC** behavior and
   **real cycle collection** → validation-pass (needs a running engine); the **DOM trigger-wiring** →
   integration; **first-build compile** → execution.
10. **`TODO-PREBUILD` tags** on the fork-variable -ng signatures the impl authors but never compiles
    (`JS_NewClassID` arity, `JS_SetOpaque` int-return/const-correctness, `JS_NewClass`, the finalizer
    typedef, `JS_NewObjectProtoClass`).

## In Scope (ordered slices)

1. **Neutral surface + 002 accessor contract.** Define CathodeBinding's public API shapes — the value
   RAII holder, the appendable class-registration API, the wrapped-native-object and held-callable
   handle types, the marshaling accept/reject boundary signature, and the teardown-registry interface
   — and the signature of 002's new opaque-context accessor (002 public header stays `JS_*`-free).
   *Method:* compose 002 (one shared context, never a second runtime); settle whether the public
   header exposes `JSValue`-holders or wraps them opaquely (the module-as-swap-boundary detail).
2. **Module + 002 accessor + value RAII + class registration.** Establish the `CathodeBinding` module
   (vendor-confined, ThirdParty commented, depends Core + CathodeRuntime); quarantine-safe `.uplugin`
   edit; add 002's opaque-context accessor (the blessed 002 evolution). Build the value RAII holder and
   the appendable class-registration mechanism (class ID / `JSClassDef` + finalizer / `JS_SetOpaque` /
   wrapped-object creation). *Method:* shadow nothing live; author to the staged -ng signatures.
3. **Held-callable handle + bounded marshaling.** Build the held-callable (own / invoke / RAII a JS
   function) and the marshaling over the accepted vocabulary with explicit exotic rejection.
4. **Teardown policy (the heart).** Build the per-object held-ref registry, the explicit free-at-trigger
   mechanism, and the class-finalizer backstop hook. Test cycle-break by our bookkeeping; the DOM
   trigger callers are deferred. *Method:* a recording native object registers held refs; a simulated
   removal trigger must free them by our registry/free-count, never by reading -ng GC state.
5. **COVERAGE + finalize.** Author `COVERAGE.md` (covered-or-deferred per responsibility; the three
   deferrals with reason/dep/unblocker/first-exec-risk); place the `TODO-PREBUILD` tags; confirm the
   close-state checklist (tests present, vendor confined to impl, quarantine intact).

## Non-Goals

- Do **not** define the DOM surface or the type "node" (DOM component, consumer) — provide the generic
  wrapped-native-object path it appends to.
- Do **not** define the host / `game.*` API (Host Bridge component, consumer).
- Do **not** wire the actual teardown trigger points — the DOM provides them; deferred sibling.
- Do **not** adopt or vendor a C++ wrapper library — quickjscpp/quickjspp target original quickjs and
  break on -ng; hand-roll the thin layer.
- Do **not** re-invent a type system — expose quickjs-ng's own conventions; the layer names no
  consumer-specific type.
- Do **not** stand up a second runtime or context — compose 002's one confined context.
- Do **not** use a native `FinalizationRegistry` — quickjs-ng has **no** such C-API; the native backstop
  is the **class finalizer**. (WeakRef exists only as a JS-author-facing intrinsic.)
- Do **not** build, run, or link. Do **not** delete a fallback just because production does not hit it —
  trace callers and tests first.

## Starting Surface

The framing audit is done (002 seam boundary + staged quickjs-ng binding primitives with line cites
and fork-variance). Before editing, the developer produces the inventory in the shape:

```
quickjs-ng primitive -> our wrapper type -> consumer (DOM / Host / Runtime) -> proposed action
```

so every authored call maps to a staged -ng signature, a CathodeBinding type, and the consumer that
will append to it.

## Method

The no-build discipline every slice follows: author the functionality complete per the component doc
even though it cannot be run yet (can-not-validate is never a reason to defer). Vendor types are
confined to CathodeBinding's impl translation units; consumers go through its API, not raw quickjs —
the module is the swap boundary. Tests assert **our** bookkeeping (free-counts, the held-ref registry,
dup/free balance, accept/reject outcomes) and **never** quickjs-ng GC internals or vendor output.
Quarantine stays disabled-not-compiled. The accepted 002 baseline stays byte-identical except the one
blessed opaque-context accessor.

## Acceptance

- `CathodeBinding` module exists, vendor-confined, quarantine intact (`.uplugin` `Modules[]` +=
  `CathodeBinding`; `EnabledByDefault:false`; `Cathode` absent from uproject `Plugins[]`; no
  Binaries/Intermediate).
- 002 gains exactly **one** opaque-context accessor; 002 public header stays `JS_*`-free; the rest of
  002 is byte-identical to its accepted baseline.
- Value RAII, appendable class registration, held-callable, bounded marshaling, and the teardown
  registry are all authored per Final State.
- Tests for the bespoke logic assert our bookkeeping only — **zero** quickjs-ng-GC-internal assertions,
  **zero** vendor-output assertions; exotic-kind rejection is exercised.
- `COVERAGE.md` complete; references resolve in-tree; the three deferrals carry
  reason/dep/unblocker/first-exec-risk (finalizer-on-GC + cycle-collection = validation-pass; DOM
  triggers = integration; first-build = execution).
- `TODO-PREBUILD` tags on the fork-variable -ng signatures.
- No build / run / link occurred and none is required to verify the above.

## Review Questions (open forks)

1. **Module name token** — "binding, not bridge" is settled; `CathodeBinding` is the proposed token
   (follows the per-component convention). The developer finalizes the exact token.
2. **Public-header vendor exposure** — does CathodeBinding's public header include `quickjs.h` and
   expose `JSValue`-holder types directly, or wrap them in an own opaque type? The module is the swap
   boundary either way; settle in Slice 1.
3. **Teardown registry granularity** — per-native-object (the proposed deterministic primary) vs any
   per-context aggregation; confirm in Slice 4.

---

# Postmortem (sprint close)

Sprint 007 closed with all five slices accepted. It is the first **vendor-confined** Cathode
component (it includes `quickjs.h` and uses 002's live context), so the couples-to-none rule of
002–006 **inverts**: the swap boundary becomes the module boundary, vendor types are legitimate in
the impl (and, per the human's "expose the spec" ruling, the public header), and the gate is "no
vendor leak into a *consumer*." The sprint's center of gravity was the **teardown re-scope** — the
custom per-object registry framed in Slice 1 was dropped for engine-native cycle handling under the
human's **Model A** ruling.

## Lead — uptime.browse-lead

- **The forks were the work this sprint, and the order mattered.** I had the dev audit the built
  sibling (002) *and* the staged vendor header before consensus, which converted three forks from
  speculation into grounded calls (hand-roll over wrappers; the one opaque-context accessor; the
  seam placement). I then surfaced each fork to the human as a concrete decision rather than absorbing
  it — naming, the "expose the spec" coupling consequence, and the teardown model. Lesson I'm keeping:
  for a component over a vendor *and* a sibling, the first developer assignment is "audit both, ground
  the forks," and the lead's job is to route those as crisp A/B choices, not to settle them.
- **The teardown fact-check is the decision I'm proudest of.** The dev's grounding concluded "drop the
  registry, it's redundant," and it would have been easy to lock that on the human's GC-lean steer.
  Instead I ran the belt-and-suspenders reviewer cross-check I'd used on 006 — and it caught that
  "redundant" was **design-contingent, not a header fact**: it holds only if native lifetime is
  JS-refcount-governed, not C++-rooted. That turned a rubber-stamp into a real fork (Model A vs
  C++-rooted), which the human decided. Lesson: when a dev's vendor-grounding carries a *conclusion*
  (not just cites), verify the conclusion follows before acting on it; route the contingent design
  fork up.
- **Ground a contradiction in the tree, don't average it.** When the reviewer's close verdict
  re-listed two S3 must-fixes the dev had reported applying, I read the actual file before routing —
  they were already landed, so I routed only the genuinely-outstanding `#3` rather than send the dev
  two done fixes. Crossed signals are resolved by reading, not by splitting the difference.
- **The close gate earned its keep.** The reviewer's own-map completeness re-derivation caught a named
  doc responsibility (C++-functions→JS-callable) mislabeled-covered behind a green table — and rather
  than quietly build or quietly drop it, the scope call went to the human, who deferred the surface
  until the engine↔app delineation is defined. The deferral plus rationale is now durable.

## Developer — uptime.browse-dev

- **Audit the built sibling AND the staged vendor header before consensus** — the highest-leverage move
  again. Real line cites turned every fork into a grounded call (confirmed -ng's `JS_NewClassID` 2-arg
  / `JS_SetOpaque` int-return, which broke the mature wrappers and flipped assemble→hand-roll;
  confirmed 002's seam is value/handle-free, forcing the context-accessor fork).
- **Surfacing the vendor's native capability beats building the bespoke thing.** The doc said the cycle
  was one "the GC cannot see through," but the header has the traversal hook (`JSClassGCMark`/
  `JS_MarkValue`). Flagging it — and exposing `GcMark` pass-through/undecided early — is what let the
  Model A re-scope land cleanly, with the custom registry dropped and no rework.
- **When evolving an accepted interface, a defaulted method preserves "rest byte-identical"** far
  better than a pure virtual that ripples to every implementer (the 002 accessor).
- **Corrections:** cite the *exact* typedef a header line belongs to (the `JS_FreeRuntime` LIFO comment
  governs `JSRuntimeFinalizer`, not the per-class finalizer); and a "reader-beware" header comment can
  invert across forks — quote it precisely or you write a self-refuting rationale (`JS_IsArray` no
  longer punches through proxies in -ng).

## Reviewer — uptime.browse-review

- **Read the swap boundary first — it decides which gate applies.** couples-to-none (003–006) and
  vendor-confined (007) are *different* gates; the couples-to-none reflex (reject `quickjs.h` in the
  header) would have wrongly rejected the human's ruling. For a vendor-confined layer the gate is "no
  vendor leak into a consumer," verified by comment-stripped grep across all modules.
- **A green count is suspect unless you can name the invariant it proves.** The teardown test's
  `RtFrees==2 / CtxFrees==0` split is a *structural proof* of the finalizer-safe discipline, not just a
  count — and I verified nothing else fed the counters. GC-timed behavior (real reclamation,
  finalizer-fires-on-GC) must be deferred to the validation pass, never faked.
- **Separate "the cite is true" from "the conclusion follows."** The dev's teardown cites were all
  real, but the "registry is redundant" conclusion was a design inference contingent on the lifetime
  model — routed up, not endorsed.
- **The close gate is a completeness gate against YOUR own map, with direction-awareness** — a green
  table, resolving refs, and passing tests still concealed a named responsibility (C++→JS-callable)
  mislabeled by a wrong-direction primitive. "Callable" is ambiguous; held-callable (JS→C++) is not the
  same responsibility as exposing a C++ function (C++→JS). Reject-on-completeness even when every
  counter is green.
- **Calibrate under-tagging to the convention the dev already established** — `JS_GetAnyOpaque`
  untagged on the production finalizer path while its sibling `JS_GetOpaque` was tagged is a real gap by
  the dev's own standard, even though the signature is correct.

## Inheritance for the next sprint

- **Model A is binding on the DOM:** node lifetime = the JS wrapper's refcount, source of truth = the
  JS wrapper graph, **no independent C++ root**. A node holding JS refs registers a `gc_mark` (marks
  them via `FJsValue::Mark`) and frees native via the finalizer (held refs via `FJsValue::FreeRT`,
  never the ctx-dtor). A DOM that C++-roots nodes reintroduces exactly the leak/UAF Model A prevents —
  the #1 inheritance to enforce.
- **C++-functions→JS-callable is an accountable deferral** (Option B): no surface yet for how a C++
  function enters the JS namespace — it waits on the host-function-binding convention + the engine↔app
  delineation point (Host Bridge sprint). (C++ holding a JS function *is* covered — `FCallableHandle`.)
- **Deferred to validation-pass** (GC-timed, needs a running engine): actual cycle reclamation /
  finalizer-fires-on-GC. The mechanisms are authored + tested by direct callback invocation; only the
  GC *timing* is unproven until it runs. First-build compile defers to execution.
- **Vendor-confined coupling is by design:** consumers are quickjs-ng-coupled at the type level (the
  "expose the spec" ruling); future consumers inherit that.
- **Noted future-option:** precise rejects for the predicate-having builtins (RegExp/Map/WeakMap/
  WeakSet/DataView), bounded out by the data model today; a re-review-requiring hardening.
