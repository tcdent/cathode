# 002 — Cathode Runtime Core scaffold

## Nature

002 authors the **Runtime Core** component (see `docs/browse/components-runtime-core.md`) — the
home everything else runs in: the one quickjs-ng runtime, the worker thread it lives on, the tick
that drives it, lifecycle, safety budgets, error surfacing, and the host-message boundary.

It is still a **no-build, no-execution sprint**. Verbatim:

> "No builds, no binary execution still stands. We are greenfield, creating the scaffolding. We
> will execute when I give approval to do so."

So 002 authors the component as proper C++ — real internal interfaces, the bespoke orchestration
logic, and the tests for that bespoke logic — but **nothing is compiled or run**. Behavioral proof
and build/log coherence are **inherited by the future execution-approval sprint**. This sprint
proves the *instrument* is honestly shaped to exercise production behavior later; it does not (and
cannot) certify behavior now.

The **whole Runtime Core is in scope** — one sprint, not split. Verbatim:

> "The whole runtime core is in scope for this sprint. There's no reason to break it into multiple
> sprints."

## Scope Boundary

**This sprint owns:** the Runtime Core component — our internal subsystem interface wrapping
quickjs-ng, runtime/context ownership, the worker thread, the ordered tick, global-environment
assembly (the registration/sequencing, not the globals' own implementations), safety budgets,
error surfacing, the host-message boundary, and startup/teardown lifecycle — plus the UE Automation
test specs for the bespoke logic in the above.

**This sprint does NOT own** (named, with the owner):
- **Any compile, link, build, or execution** — *execution-approval sprint (not yet opened)*. Tests
  are authored but not run.
- **The delegated components' implementations** — DOM, Layout & Style, Text & Fonts, Rasterizer
  (the web engine); the actual host API surface (**Host Bridge `game.*`**); texture upload
  (**Texture Egress**); cross-heap object lifetime (**JS↔Native Bridge**). Runtime Core provides
  only the **registration points and sequencing seams** for these; their internals are their own
  components/sprints.
- **The `game.*` API shape** — intentionally deferred by the architecture doc until the sim exists.
- **Building/vendoring the libraries** (done in 001; binaries are the execution sprint).

## Problem

Runtime Core does not exist. The plugin (from 001) has only the module shell
(`FCathodeRuntimeModule` with empty `StartupModule`/`ShutdownModule`, commented as 001 scaffold).
There is no runtime ownership, no worker thread, no tick, no safety, no host boundary, and no
internal contract isolating quickjs-ng — so today there is nothing testable and no swap boundary.

## Goal

Finishing 002 looks like: Runtime Core authored as a proper subsystem — our own internal interface
wrapping quickjs-ng at the **right altitude** (the primitive/moderately-primitive functionality we
need, not a 1:1 re-export, not a re-creation), behind which the vendor is **swappable**; the
bespoke orchestration (tick phases, lifecycle ordering, the message boundary, budget/error policy)
implemented; and **effective** Automation test specs authored for the bespoke, unit-testable
surfaces — all without a build or a run.

The wrapper-altitude philosophy, verbatim:

> "Real interfaces that wrap third party packages are a good pattern and just don't take it too
> far. We don't need to recreate the entire vendor package, but we do need to have our own internal
> contract for how we interact with subsystems. And so subsystems can wrap one or more vendor
> packages into a logical unit. And that will allow us to potentially swap out vendor packages in
> the future if we find better options, if one stops being maintained, or if we end up building our
> own subsystem because we find one to be inadequate. So the delicate balance is between
> overengineering our wrapper and just re-exposing the same functions basically versus exposing the
> primitive or moderately primitive functionality that we need internally which wraps the lower
> level functions exposed by the libraries."

The testing philosophy, verbatim:

> "with the testing surface it's like not about making it extensive it's about making it effective
> so anywhere that we do have unit testable surfaces functionality that we are implementing
> ourselves that we expect a certain behavior from we should expect to have test coverage on those
> out of the gate we can expect that vendor libraries have test coverage for their own functionality
> and if they don't we can also assume that the vendor library for the most part works and so we
> don't need to worry about testing code that we don't maintain but when we do develop our own
> bespoke implementations where applicable where it is possible to unit test them we do need to
> write tests as part of the development process"

## Final State

1. An internal **JS-runtime subsystem interface** (our contract) wrapping quickjs-ng at moderate-
   primitive altitude: runtime/context lifecycle, script evaluation, the pending-job pump, and the
   safety-budget hooks — expressed as *our* types, with quickjs-ng (`JS_*`) confined behind the
   implementation, swappable.
2. Runtime/context **ownership + lifecycle**: single-instance creation and clean teardown with a
   defined order.
3. The **worker thread** + the **host-message boundary**: single-thread ownership guarantee and a
   thread-safe inbound (events/state) / outbound ("frame ready") queue.
4. The **tick**: the ordered phases (drain inbound → fire due timers → run `requestAnimationFrame`
   → drain microtasks → let dirtied surfaces relayout/raster + emit "frame ready"), driven through
   the subsystem seam so the *ordering* logic is exercisable independent of a live engine.
5. **Safety budgets** (interrupt/memory/stack) and **error surfacing** (uncaught JS + `console.*`
   routed out) wired through the subsystem.
6. **Global-environment assembly** + the **delegation seams** (registration points/sequencing for
   the globals and the delegated components) — seams only, not their implementations.
7. **UE Automation test specs** for the bespoke, unit-testable logic (tick phase order, lifecycle
   ordering, the message queue, budget/error policy) — shaped to exercise behavior via the
   production path, **not run**, expecteds captured-from-real-output (TODO/placeholder now).

## In Scope — ordered slices

Each slice follows the Method. A slice is fed only after the prior meets its evidence. Within a
slice the dev owns the internal design (naming/types/structure); the contract fixes the
responsibility and the testability discipline, not the implementation shape.

### Slice 1 — Starting Surface audit + the subsystem wrapper contract
Audit (read-only) the 001 module shell, the quickjs-ng API surface we actually need, and the UE
Automation spec framework available in the project. Then **design our internal JS-runtime subsystem
interface** — the swap boundary — at the right altitude (the moderately-primitive ops we need:
runtime/context lifecycle, eval, job pump, budget hooks), confining `JS_*` to the implementation.
Output: the interface + a short altitude rationale (why these ops, why not 1:1, why not recreation).

### Slice 2 — Runtime/context ownership + lifecycle
Implement single-instance runtime/context creation and clean, ordered teardown behind the
subsystem. **Bespoke/testable:** single-instance guarantee, teardown-order/release discipline.

### Slice 3 — Worker thread + host-message boundary
Single-thread ownership guarantee and the thread-safe inbound/outbound message queue.
**Bespoke/testable:** queue ordering/hand-off, the single-thread invariant.

### Slice 4 — The tick + phase ordering
The ordered tick driving the subsystem. **Bespoke/testable (highest value):** phase order proven by
observable effect, drain-to-empty (microtask pump), due-timer/RAF selection. Design the tick to run
the production orchestration while a test can inject a recording subsystem impl (isolate the input,
never fork the tick code).

### Slice 5 — Safety budgets + error surfacing
Interrupt/memory/stack budget wiring + uncaught-JS/`console.*` routing. **Bespoke/testable:** budget
policy and error-routing logic that is ours (the raw `JS_Set*` calls are vendor passthrough — not
tested).

### Slice 6 — Global-environment assembly + delegation seams
The startup assembly orchestration and the registration/sequencing seams to the globals and the
delegated components. **Bespoke/testable:** assembly ordering; seams trace to the real production
entries (no presence-theater seam the tick never calls).

## Non-Goals

- **No build, compile, link, or execution.** Acceptance never depends on a run. Tests are authored,
  not run.
- **No 1:1 vendor re-export and no recreation of quickjs-ng** — the wrapper exposes only the
  moderately-primitive functionality we need.
- **Do not test vendor functionality** — assume quickjs-ng works / has its own coverage; test only
  our bespoke logic, where it is unit-testable.
- **No extensive-for-its-own-sake testing** — effective, not exhaustive; cover the bespoke surfaces
  where we expect specific behavior.
- **No hand-computed expecteds** baked into specs that never ran — capture from the real primitive
  at execution time (TODO now).
- **Do not implement the delegated components** (DOM/layout/raster/host-API/egress/cross-heap) — seams only.
- **Do not delete a fallback just because production does not hit it — trace callers and tests first.**

## Starting Surface

Audit first, producing the inventory before editing:
1. `game/Plugins/Cathode/Source/CathodeRuntime/` — the 001 module shell (entry point to extend).
2. The quickjs-ng API surface we need (confirmed present in `vendor/lib/cathode/quickjs-ng/quickjs.h`):
   `JS_NewRuntime`/`JS_NewContext`/`JS_FreeContext`/`JS_FreeRuntime`, `JS_Eval`,
   `JS_ExecutePendingJob`, `JS_SetInterruptHandler`/`JS_SetMemoryLimit`/`JS_SetMaxStackSize`.
3. The UE Automation spec framework available (how specs are declared/discovered in this project) —
   so the test specs are authored in the real gate's shape.

Inventory shape: `responsibility -> our subsystem op -> quickjs-ng primitive(s) wrapped -> bespoke? (testable y/n)`

## Method

No build/log coherence this sprint (nothing compiles or runs). Acceptance is **structural
conformance + testability honesty**, proven by reading current source:
- **Wrapper altitude:** the subsystem exposes the moderately-primitive ops we need, confines `JS_*`
  to the implementation, and is swappable — neither a 1:1 re-export nor a recreation.
- **Seam-to-production traceability:** every exposed seam routes to the real runtime/tick code it
  claims to expose; reject any seam the production path never calls, or a design that would let a
  test drive a divergent path. The subsystem interface is simultaneously the **swap boundary** and
  the **test-injection seam**: a test isolates the *input* (a recording subsystem impl) while
  running the *exact* production orchestration.
- **Test-spec shape:** specs for our bespoke logic drive the real primitive/production path and
  assert an observable effect, not presence (a name/key existing). Expecteds are captured-from-real-
  output at execution time — marked TODO/placeholder now, never hand-computed.
- **Explicit deferral:** behavioral proof and build/log coherence are inherited by the execution-
  approval sprint and are stated as deferred in every verdict.

## Acceptance — the gates that close the sprint

1. **Subsystem contract:** an internal JS-runtime interface wrapping quickjs-ng at moderate-
   primitive altitude exists; `JS_*` appears only in the implementation, not the interface; the
   vendor is swappable behind it.
2. **Runtime Core authored:** ownership/lifecycle, worker thread + message boundary, tick + phase
   order, safety budgets, error surfacing, global-assembly + delegation seams — all present per
   Final State, each seam traceable to its production entry.
3. **Tests authored for bespoke logic:** Automation specs exist for the unit-testable bespoke
   surfaces (tick order, lifecycle order, message queue, budget/error policy), shaped to exercise
   behavior via the production path, no hand-computed expecteds, vendor functionality not tested.
4. **No build/execution occurred** and none is required to verify 1–3; behavioral proof explicitly
   deferred.

## Review Questions (open forks)

1. **Subsystem boundary granularity:** is the JS-runtime subsystem one interface, or does Runtime
   Core warrant more than one swap boundary (e.g., the message/threading boundary as its own)? The
   doc notes Runtime Core may absorb the threading/messaging boundary — dev proposes, lead/human
   ratify if it becomes a real fork.
2. **Automation spec home/shape:** where the project's Automation specs live and the spec style to
   match (confirmed in Slice 1's audit).
3. **Tick cadence/idle policy** (event-driven wake vs fixed interval) is doc-deferred to the team —
   surfaced if it forces a structural choice this sprint; otherwise left for the execution sprint.

---

# Postmortem (sprint close)

Sprint 002 **closed ACCEPTED** — all six slices plus the start-trigger resolution independently
accepted by `uptime.browse-review`. The **whole Runtime Core** is authored as no-build/no-execution
scaffold: the `ICathodeJsRuntime` subsystem swap boundary (quickjs-ng confined behind it), runtime
ownership + ordered teardown, the worker thread + thread-safe host-message boundary, the ordered
tick (one `FCathodeTick::Tick()` driven by both the worker loop and the specs), safety budgets +
fault-contained error routing, and the global-environment assembly + lifecycle wiring (module owns
and now auto-starts the worker). Behavioral proof and build/log coherence are explicitly **inherited
by the execution-approval sprint** — nothing compiled or ran.

## Start-trigger resolution (auto-start)

The worker start trigger was the sprint's one human fork. The dev authored lazy/explicit start; the
human **ruled auto-start**, rationale captured verbatim:

> "we plan to expose the game state through a linked native object in the JavaScript namespace.
> That way, the game state that has been explicitly exported into the cathode state will be
> available. With the deferred startup, we also have to defer attaching that state, and that feels
> like it could create some weird cases. Even though deferring startup seems like the right choice,
> we can expect that if the library is included in the engine, it is actually being used"

So `StartupModule` now starts the worker (the ordered `FRuntimeStartup` sequence) on module load —
the future linked-native game-state object attaches **at startup**, not lazily. The `game.*` binding
itself stays empty-for-future (the JS↔Native Bridge sprint); auto-start only fixes *when* it attaches.
Re-verified: quarantine survives (plugin not activated, so `StartupModule` is authored-not-run).

## Lead retrospective — uptime.browse-lead

What worked: framing the whole Runtime Core into six ordered, gated slices kept a large component
from becoming a sprawling skeleton; routing the two hardest S4 acceptance criteria (no-forked-tick +
shared effect-log) to the dev *up front* meant the highest-risk slice was built right rather than
burning a review cycle; every design fork (wrapper altitude/scope/testing philosophy, the
start-trigger) went to the human and I routed rather than resolved.

Two process corrections I carried/absorbed: (1) I held the "sprint plan is a contract, not a devlog"
discipline from 001 — no mid-sprint edits to this plan, all corrections through coordination, lessons
here at close. (2) The human flagged that shared memories must be **browse-scoped** — an unscoped
testing/process memory steers devs on subsystems I have no purview over; I rescoped two memories
mid-sprint and will keep my footprint inside browse.

The reusable pattern this sprint proved: the subsystem interface is simultaneously the **vendor swap
boundary and the test-injection seam**, so a threaded, engine-bound system is unit-testable with no
live engine or thread (fake clock, recording runtime/sink, injected step actions) while still running
the *exact* production orchestration. Paired with the per-assertion expected-shape ruling
(design-invariant = assert now; engine-computed = TODO), it let a no-run sprint author real,
non-presence-theater tests.

## Developer retrospective — uptime.browse-dev

Authored the whole Runtime Core as no-build scaffold across six slices. Key shaping forces:
- **The no-fork discipline became the architecture:** because production and tests must drive the
  *same* orchestration unit, every ordered concern is ONE unit (`FCathodeTick`, `FRuntimeStartup`,
  `FGlobalEnvironment::AssembleGlobals`) that `Run()` and the spec both call, with injected seams and
  one shared effect-log. Generalized cleanly from S4 to S6.
- **Bespoke-vs-vendor split kept tests honest:** swap-boundary ops stayed thin (vendor passthrough);
  all unit-tested logic sits on top of the seam. Vendor functionality is never tested — that is the
  execution sprint's behavioral proof.
- **Injectable seams** (`ICathodeClock`, recording runtime/sink, fabricated affinity id) made the
  threaded, engine-bound system testable without a live engine or thread.
- **Stability-not-security** shaped S5: a wall-clock interrupt budget + reports-and-continues routing,
  zero threat-model machinery, no robustness shortcuts.

Friction: cross-slice edits accumulated on `Worker.cpp`/tick/teardown — managed by re-verifying the
accepted invariants from source after every edit and reporting that to the reviewer.

## Reviewer retrospective — uptime.browse-review

Re-derived every slice from source, never the dev report; re-grepped `JS_*` confinement at every
slice (held: code tokens only ever in `CathodeJsRuntime.cpp`) and traced each seam to its production
entry. Rulings: the **per-assertion expected-shape** line (design invariant = assert now; only an
engine-computed value stays captured-from-real-output/TODO) — kept "no hand-computed expecteds" from
being mechanically misapplied to pure orchestration; the **no-forked-tick + shared effect-log**
discipline (S4) re-applied to S6; **inventory-as-activity** (S1, not a durable file); **trusted /
no-attack-surface recalibration** (S5 judged anti-self-DoS + fault-containment, never security; no
reject for missing hardening); **quarantine survival** (S6, into a module that now owns/auto-starts a
worker). All behavioral proof + build/log coherence stated as **deferred** in every verdict.

## Execution-approval sprint (and future component sprints) inherit

- **First compile is the execution sprint:** uncomment the `Build.cs` ThirdParty include path (001
  deferred), build the 5 vendored libs from source, activate the plugin. The `#include "quickjs.h"` +
  the `JS_*` lifecycle/extraction/budget code get their first real compile then; build/log coherence
  finally applies.
- **Tests are authored-not-run:** the execution sprint runs them. Design-invariant assertions pass by
  construction; the vendor paths (`JS_New/Free`, `ExecutePendingJob`, exception extraction,
  memory/stack enforcement, the interrupt thunk firing) get their behavioral proof.
- **Empty-but-invoked seams for future component sprints:** `LoadAuthorScript` (bundle loader);
  `BeginTicking` (transition marker — the real loop runs after `Startup()` returns); `OnInstallConsole`/
  `OnInstallTimers` (JS↔Native Bridge binds console/`setTimeout`/rAF onto the real sink/scheduler);
  `OnInstallGameBridge` (Host Bridge / the linked-native `game.*` state object); `OnInstallDocument`
  (DOM mount root); `FSurfaceRelayoutSeam` (web engine / Surfaces relayout+raster → frame-ready). None
  are presence-theater; all are invoked by tick/startup/assembly in correct order.
- **Honest stubs owed real impl:** `ICathodeJsRuntime::Evaluate` and `ExecutePendingJob` (real
  `JS_Eval` / `JS_ExecutePendingJob` mapping), `TakePendingError` vendor extraction.
- **Placeholders to tune against real measurement:** default budgets (interrupt 1.0s, memory 64MB,
  stack 256KB); the event-driven-wake `FrameInterval` (hardcoded 1/60).
- **Boot model is auto-start:** module-load starts the worker + the ordered startup sequence; the
  linked-native game-state object attaches at startup.
- **Carried-from-001 open inference:** exact UE5.7 `IsEnabledByDefault` semantics for an unspecified
  field on a project plugin — moot (explicit `false`), still inference-until-confirmed; closable via a
  UE5.7 `Engine/Source/Runtime/Projects` grep on the Mac.
