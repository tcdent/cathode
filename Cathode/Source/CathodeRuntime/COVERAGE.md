# CathodeRuntime — Coverage

The JS-runtime seam: the quickjs-ng engine wrapper (runtime/context lifecycle, single-instance guard),
the ordered tick (event loop / microtask drain / timers / rAF), the worker thread, the interrupt +
memory budgets, the diagnostic sink, and — as of sprint 016 — **real JS evaluation + a real draining
job queue**. Authored across sprints 002–016; 016 turned the two engine stubs real and proved them
green against the real engine (full Cathode gate 107/107).

> No COVERAGE.md existed for this module until 016 (the most foundational module had none — flagged by
> the dev's cross-module state audit). This is the ratified close record.

## Covered

| responsibility | sprint | proof |
|---|---|---|
| Ordered tick (inbound → due timers → rAF → microtask drain → relayout/emit), phase order | 002 | `Cathode.RuntimeCore.Tick.PhaseOrder` (single shared ordered effect-log) |
| Microtask drain-to-empty loop (N jobs → N+1 calls; terminates on NoPending) | 002 | `Cathode.RuntimeCore.Tick.MicrotaskDrainToEmpty` |
| Timer + rAF selection; idle-wait policy | 002 | `…Tick.TimerAndRafSelection`, `…Tick.IdlePolicy` |
| Real JS globals (setTimeout/clearTimeout/rAF/cancelRAF, console) on the live context | 012 | `Cathode.Runtime.ContractGlobals` (real `JS_Eval` on a real context) |
| Interrupt handler + wall-clock budget; memory/stack limits (vendor passthrough) | 002 | `…Safety.InterruptBudgetPolicy` |
| Single-instance guard + ordered teardown; message FIFO + thread affinity; global-assembly order | 002 | `…Lifecycle.*`, `…Threading.*`, `…Assembly.*` |
| Production worker thread (auto-starts at module load; runs the contract startup + the tick loop) | 002 | `FCathodeRuntimeWorker` (no end-to-end automation test — the tick is driven directly) |
| **Real `Evaluate` (JS_Eval, Global)** — runs real JS, reports `bThrew` + the error | **016 s1** | `Cathode.RuntimeCore.Eval.RealEvaluate` (throw / computed-throw "42" / syntax / inverted side-effect) |
| **Real `ExecutePendingJob`** — the one-job pump, the confirmed tri-state | **016 s2** | `Cathode.RuntimeCore.Drain.RealJobPump` (EMPTY / re-queuing DRAIN / host-job FAULTED / leak) |
| **The integrated drain** — the production `DrainMicrotasks` loop over the real primitive | **016 s2** | same test (a re-queuing Promise chain drains in order to empty via `FCathodeTick::Tick`) |

## The quickjs-ng job tri-state convention (carry-not-rediscover)

`ExecutePendingJob` maps `JS_ExecutePendingJob(JSRuntime*, JSContext** pctx)`'s **int** return, per the
**pinned upstream contract** — quickjs-ng **v0.15.1 / fd0a0210** (`QJS_VERSION` 0.15.1), verbatim:
*"return < 0 if exception, 0 if no job pending, 1 if a job was executed successfully"*:

| return | → | EJobPumpResult | notes |
|---|---|---|---|
| `r < 0` | → | `Faulted` | `*pctx` = the job's context; single-context ⇒ `*pctx` == our Context ⇒ `TakePendingError` retrieves the fault |
| `r == 0` | → | `NoPending` | `*pctx` = NULL |
| `r > 0` (==1) | → | `Executed` | |

**quickjs.c is NOT vendored** (only `quickjs.h`, which carries no return-convention comment), so the
convention is **upstream-grounded**, not header-derived. The **in-tree gate** is the empirical 3-way
test (`Cathode.RuntimeCore.Drain.RealJobPump`: NoPending / Executed / Faulted distinct, with the
faulted "42" bite). **On a future quickjs re-vendor: RE-CHECK this convention against the new pin's
`quickjs.c`** (the `<0/0/>0` mapping is fork-dependent) — the 3-way test is what re-empirically gates it.

## The `<0` Faulted reachability finding (016)

`<0` (Faulted) fires **only when the job FUNCTION returns `JS_EXCEPTION`**. A **author-JS** promise
reaction CATCHES a throwing `.then` handler (rejects the result promise → the job returns normally →
`Executed`/`1`); the unhandled rejection is surfaced via a **separate host callback**
(`JSHostPromiseRejectionTracker`), NOT the job return. So **`<0` is unreachable from author-JS
promises** — the only reachable trigger is a **host-enqueued raw job** (`JS_EnqueueJob` with a
`JSJobFunc` that returns `JS_Throw(...)`), which the FAULTED proof uses (test-side; no production seam).

## The `FEvalResult{bThrew, FJsError}`-no-value seam

`Evaluate` reports **only** whether the script threw + the error — never the produced `JSValue`. Value
handling (marshaling a JS value to native) is the **JS↔Native Bridge's** concern (CathodeBinding,
sprint 007), not Runtime Core's. Proof of execution is therefore via observable side effects + `bThrew`
(throw-on-success — a `bThrew==false` assertion is vacuous against an always-`false` stub). Errors carry
`TakePendingError`'s `JS_ToCString` form (`"Error: <message>"`, the toString), so proofs assert
`Message.Contains(<computed value>)`.

## Deferred / follow-ons (tracked, not in 016)

- **(a) Author-JS unhandled-rejection → diagnostic sink** — the user-meaningful "a promise threw"
  channel (`JS_SetHostPromiseRejectionTracker` → the sink), distinct from the Faulted tri-state.
  tcdent-deferred from 016 ("the queue drains" was 016's scope); a follow-on production seam.
- **(b) `Cathode.Runtime.ContractGlobals` self-isolation** — a pre-existing (sprint 012) test that does
  `CreateJsRuntime` without releasing the auto-started singleton, so it fails under a `Cathode.Runtime`-
  only NARROW filter (green in the full gate, where ordering frees it). Give it the
  `FScopedTestRuntime` idiom. Test-hygiene, not a product defect.
- **(c) Module-eval async-rejection proof** — `Evaluate` wires `JS_EVAL_TYPE_MODULE` per `EEvalKind`,
  but the MODULE path is **dormant** (no caller, no proof in 016): module eval is async (a module-level
  throw surfaces via promise rejection, not the immediate `JS_IsException` the Global path uses). Its
  own (more complex) proof shape.
- **End-to-end worker-thread test** — the worker auto-starts + runs the tick in production, but no
  automation test drives the actual thread end-to-end (the tick is driven directly, off-thread).
