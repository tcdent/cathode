# Cathode — Runtime Core (detail)

Detail for the **Runtime Core** component (see `components.md`). This goes a level deeper on
what the component is responsible for and why — still not prescribing internal design, which is
the team's. References to quickjs-ng APIs are grounding.

Runtime Core is the home everything else runs in: the single JavaScript runtime, the thread it
lives on, and the loop that drives it forward. If a component is "the engine that hosts the web
engine," this is it.

## Responsibilities

**Own the one runtime.** There is exactly one quickjs-ng runtime and one global context — the
shared world-JS state every surface reads from. Runtime Core creates it at startup and destroys it
at shutdown. *Why:* the shared-global, many-surfaces model is meaningless without a single shared
runtime instance; nothing else may create a second one.

**Own the worker thread.** The runtime is single-threaded, and all JavaScript, DOM mutation,
layout, shaping, and rasterization happen on this one thread. Runtime Core spawns and owns it and
is the guarantor that the JS heap is never touched from another thread. *Why:* quickjs-ng is
one-runtime-per-thread; keeping the whole pipeline here keeps it off the game's render/critical
path. *Ground:* `JS_NewRuntime` / `JS_NewContext`.

**Drive the tick.** Nothing advances unless Runtime Core pumps it. Each iteration moves the world
forward through ordered phases (see below) on the worker's own clock — not the game frame rate.
*Why:* author code, promises, timers, animation callbacks, and inbound host messages all need a
deterministic place and order to run; getting the phase order wrong silently breaks promise
resolution and animation timing.

**Assemble the global environment.** At startup, Runtime Core orchestrates installing the globals
the contract promises — `console`, the timer/`requestAnimationFrame` surface, the `game.*` bridge
object, the document/mount root — by wiring in the components that own them. *Why:* something has
to compose the JS global the moment the context exists; that's the host's job.

**Enforce the safety budgets.** Runtime Core installs the interrupt handler (an
instruction/time budget that aborts a runaway script) and the per-runtime memory and stack limits.
*Why:* a shared runtime is a single point of failure — one author's `while(true)` would hang
every surface — so bounded execution protects the whole system. *Ground:* `JS_SetInterruptHandler`,
`JS_SetMemoryLimit`, `JS_SetMaxStackSize`.

**Surface errors.** Uncaught JS exceptions and `console.*` output are caught here and routed out
(game log / dev overlay) with stack traces. *Why:* there are no DevTools; the system is only
debuggable if the runtime host reliably emits what went wrong.

**Own the host boundary.** Runtime Core receives thread-safe messages from game threads (events,
state updates) and emits "frame ready" notifications outward. *Why:* this is the only safe way for
the multi-threaded game to talk to the single-threaded runtime. (The components map notes this
messaging boundary may live here rather than as a separate component.)

## The tick

The heart of Runtime Core is an ordered loop. The phases matter; their internal mechanism does
not need specifying here. Conceptually, per iteration:

1. **Drain the inbound queue** — apply host events and state updates (which may run JS handlers).
2. **Fire due timers** — `setTimeout` callbacks whose deadline has passed.
3. **Run `requestAnimationFrame` callbacks** — the "about to produce a frame" hook.
4. **Drain microtasks** — pump the job queue so `Promise` continuations resolve. *Ground:*
   `JS_ExecutePendingJob` in a loop until empty; without this, promises never settle.
5. **Let dirtied surfaces relayout/raster and emit "frame ready"** (delegated to the web-engine
   and surface components; Runtime Core just sequences it).

This mirrors a browser event loop (tasks → microtasks → render) but simplified for our needs. The
cadence is the worker's own — idle when nothing is dirty, faster when animating — and decoupled
from the game's frame rate.

## Lifecycle

**Startup** has an order: create runtime/context → install safety budgets → install globals and
register host functions → load the (pre-bundled) author JS → begin ticking. *Why:* globals and the
host API must exist before author code runs.

**Teardown** must be clean: stop the tick, cancel timers, abort in-flight work, release every JS
value the core holds, then destroy context and runtime. *Why:* clean teardown is where the
JS↔native lifetime hazard bites hardest; a sloppy shutdown leaks or crashes.

## Boundaries

Runtime Core **owns** the runtime, the thread, the tick, lifecycle, safety, error surfacing, and
the host message boundary. It **delegates** everything substantive:

- the DOM, layout, shaping, raster → the web-engine components (it only sequences them in the tick)
- the actual host API surface → Host Bridge (it provides the registration point, not the API)
- texture upload → Texture Egress (render-thread side; Runtime Core only emits "frame ready")
- cross-heap object lifetime → JS↔Native Bridge (Runtime Core uses it for values it holds)

## Open / deferred (team's call)

- Exact tick cadence and idle policy (event-driven wake vs fixed interval).
- Whether the host messaging boundary is folded into Runtime Core or split out.
- Confirmation that one runtime serves all surfaces (the design's intent) vs any future need to
  partition — not expected, but the core is where that would live.
