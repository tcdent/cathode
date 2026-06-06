# Cathode JS Binding — component coverage & accountable deferrals

Durable accountability record for the JS↔Native Binding (sprint 007, no-build/no-run scaffold). It maps every
responsibility in [`docs/browse/components-js-native-bridge.md`](../../../../../docs/browse/components-js-native-bridge.md)
to its status: **COVERED** (slice + mechanism + proof test) or **DEFERRED** (reason · dependency · what
unblocks it · first-execution risk). The teardown model is a recorded **human decision** (Model A), not a
gap. Every in-tree reference (file / type / test) resolves.

This file is the accountability of record.

This is the first **vendor-confined** Cathode component (not couples-to-none): it includes `quickjs.h` and
calls quickjs-ng binding primitives, and needs 002's live `JSContext`. The swap boundary is the **module
boundary** — consumers (DOM, Host Bridge) go through `CathodeBinding`'s API, never raw quickjs. Per the
human's "expose the spec" ruling the public header exposes quickjs-ng's own conventions directly; the layer
invents no type system.

## Covered

| Responsibility | Slice | Mechanism | Proof |
|---|---|---|---|
| **Expose C++ to JS — objects** (C++ objects appear as JS objects) | S2 | `INativeClassRegistry::CreateWrappedObject` (a registered class instance carrying an opaque native pointer) | `Cathode.Binding.Registry.OpaqueRoundTrip` |
| **Provide the binding mechanism** (appendable: register class + opaque + finalizer) | S2 | `INativeClassRegistry` / `FNativeClassRegistry` (`CathodeNativeClassRegistry.cpp`): `RegisterClass` (`JS_NewClassID` 2-arg, `JS_NewClass`, `JSClassDef` + finalizer + `gc_mark` pass-through) → `JSClassID`; one generic path consumers append to; `CreateNativeClassRegistry` over 002's one context | `Cathode.Binding.Registry.OpaqueRoundTrip` |
| **Marshal values** (bounded vocabulary; reject exotic) | S3 | `ClassifyJsValue` accept/reject boundary (number/string/bool/null/undefined/array/record/NativeObject/Callable; rejects Symbol/BigInt/Proxy/Date/ArrayBuffer/typed-array/Error) + typed converters `Marshal`/`Unmarshal` Number/String/Bool (reject-on-mismatch); arrays/records compose the element converters; **no invented tagged-union** (`CathodeMarshal.cpp`) | `Cathode.Binding.Marshal.ClassifyVocabulary`, `Cathode.Binding.Marshal.PrimitiveRoundTrip` |
| **RAII over JS refcounts** | S1/S2 + S4 | `FJsValue` (`CathodeJsValue.cpp`): owns one ref; `JS_DupValue` on copy, transfer on move, `JS_FreeValue` on destroy, `Release` hands off; `FreeRT` (`JS_FreeValueRT`) is the finalizer-safe path | `Cathode.Binding.Value.DupFreeBalance` |
| **Held-callable** (a JS function held native-side, e.g. a listener) | S3 | `FCallableHandle` (`CathodeCallableHandle.cpp`): owns the function via `FJsValue`, `Call` = `JS_Call`, RAII-frees on destruction | `Cathode.Binding.Callable.InvokeAndBalance` |
| **Own cross-heap lifetime** (the #1 hazard — node↔listener cycle) | S4 | **Model A** (below): `FJsValue::Mark` (`JS_MarkValue`) for gc_mark cycle visibility + the class finalizer (`FinalizeNativeObject`) freeing the native side + `JS_FreeValueRT` finalizer-safe release; `JS_FreeRuntime` is the blanket dispose (002's ordered teardown) | `Cathode.Binding.Teardown.GcMarkAndFinalize` |

## The teardown model — a recorded human DECISION (not a gap)

The Slice-1 surface sketched an app-side `ITeardownRegistry` (a per-object held-ref registry with explicit
free-at-trigger). It was **dropped** by an explicit human ruling, **Model A (JS-refcount-governed)**. Note
this supersedes not just our Slice-1 sketch but the **component doc's own key-concern premise** — the doc
prescribes "explicit teardown on node and listener removal, plus finalization-driven cleanup as a backstop";
Model A overrides that premise itself (engine-native cycle collection replaces the explicit per-removal
teardown), so a doc-vs-COVERAGE reader sees the doc's prescription was the thing overridden, not merely the
sketch:

- A wrapped native object's lifetime is governed **solely** by its JS wrapper's refcount — no independent C++
  root, **no app-side held-ref registry** (that parallel bookkeeping is exactly what we were told not to
  rebuild; take maximum benefit from the engine).
- The cross-heap cycle is closed by **engine-native** mechanisms: (1) `gc_mark` makes held refs visible so
  -ng's collector reclaims the cycle (`FJsValue::Mark` → `JS_MarkValue`); (2) the **class finalizer** frees
  the native side when an instance is collected (`FinalizeNativeObject`; held refs released via
  `FJsValue::FreeRT`, since a finalizer runs with `rt` but no `ctx`); (3) `JS_FreeRuntime` is the blanket
  dispose at shutdown.
- GC-timed (non-deterministic) reclamation is the **accepted cost**.

Grounding note: the per-class `JSClassFinalizer` fires **per-instance** as each is collected (refcount-zero /
GC reclaim / `JS_FreeRuntime` graph teardown); it does **not** carry the "LIFO at the very end" guarantee —
that is the separate `JSRuntimeFinalizer` mechanism.

## Deferred — with accountability

### 1. Expose C++ FUNCTIONS to JS — the other half of "expose C++ to JS"
The component doc's "expose C++ to JS" has two halves. C++ **objects** → JS objects is COVERED (above,
`CreateWrappedObject`). C++ **functions** → callable JS functions is **deferred by human decision** — no
surface for how a C++ function ends up in the JS namespace is developed this sprint (no prototype, no
verification that a function appears in the JS namespace).
- **Reason (human, verbatim):** "We should keep in mind the convention we want for binding the C++ functions
  into the JavaScript namespace as we define the point of delineation between the JavaScript engine and our
  Cathode application." And: "We should not try to develop any surface that defines how a C++ function ends up
  in the JavaScript namespace yet."
- **Dependency:** the host-function-binding convention + the engine↔app delineation point are not yet defined.
- **Unblocks:** a later sprint (e.g. Host Bridge) that settles that convention.
- **First-execution risk:** no C++ host/DOM function is callable from author JS until that mechanism exists.
  (Direction note: this is C++ → JS-callable. The OPPOSITE direction — C++ holding/invoking a JS function — is
  covered as **Held-callable** (`FCallableHandle`), not this.)

### 2. Actual GC reclamation / cycle collection / finalizer-fires-on-GC
- **Reason:** these are GC-timed runtime behaviors of quickjs-ng; no engine runs this sprint. The tests
  invoke our `gc_mark` / finalizer callbacks **directly** and assert our mark/free/dtor bookkeeping — they do
  not (cannot) assert that the collector actually reclaims a cycle or that a finalizer fires on a real GC.
- **Dependency:** an execution-sprint build + a running quickjs-ng; the engine owns reclamation timing.
- **Unblocks:** the validation-pass (execution-approval sprint).
- **First-execution risk:** whether a real cross-heap cycle is genuinely reclaimed (and finalizers fire) is
  proven only once the engine runs; our mechanism (mark visibility + finalizer native-free + rt-release) is
  authored + tested, the GC behavior is assumed correct.

### 3. First-build compile of the module
- **Reason:** no-build/no-run scaffold; the module has never been compiled. The `quickjs.h` include + the
  ThirdParty link lines stay commented until the execution sprint builds quickjs-ng from source.
- **Dependency:** the execution-approval sprint's build.
- **Unblocks:** the execution-approval sprint.
- **First-execution risk:** the usual first-compile surface (the `TODO-PREBUILD`-tagged fork-variable -ng
  signatures); each is grounded against the staged header but unverified until built.

### 4. DOM JS-refcount-governed lifetime (Model A inheritance)
- **Reason:** Model A commits consumers to JS-refcount-governed lifetime — the source of truth is the JS
  wrapper graph, with no independent C++ root. The DOM is a future consumer that **must honor** this (register
  a `gc_mark` for nodes holding JS refs; free native via the finalizer; no parallel C++ ownership root).
- **Dependency:** the DOM component (a consumer).
- **Unblocks:** the DOM sprint.
- **First-execution risk:** a DOM that roots nodes in C++ independently of the wrapper would reintroduce the
  leak/use-after-free the model prevents; the DOM sprint inherits this constraint.

## Noted future-option (not scheduled)

Precise rejection of the predicate-having builtin object kinds (`RegExp` / `Map` / `WeakMap` / `WeakSet` /
`DataView` — predicates at `quickjs.h:925-931`) via explicit `ClassifyJsValue` checks. Today they fall through
to `Record` but are **bounded out by the data model** (the DOM / `game.*` boundary never carries them), so no
explicit reject is required. Adding them is a strictly-more-robust hardening (a behavior change wanting
re-review) — recorded for a future hardening pass, not done here.

## Resolved doc open-items

- **Adopt a C++ wrapper vs hand-roll** → **hand-roll.** Both mature wrappers (quickjscpp, quickjspp) target
  Bellard's original quickjs and break on -ng's diverged signatures (`JS_NewClassID` 2-arg, `JS_SetOpaque`
  int-return), confirmed by source inspection; no maintained standalone -ng wrapper exists. Adopting would mean
  fork-patch-and-maintain a ~2200-line wrapper built for the wrong fork as a new vendored dependency, against a
  small specific surface. The thin RAII layer is hand-rolled against the staged -ng signatures; quickjspp is a
  reference pattern, not a dependency.
- **The marshaling vocabulary** → the bounded **accept/reject boundary** (`ClassifyJsValue` + the typed
  element converters), exposing quickjs-ng's own value kinds; the layer invents **no** type system.
