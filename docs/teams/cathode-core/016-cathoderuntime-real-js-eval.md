# Sprint 016 — CathodeRuntime: Real JS Eval

Complete the two quickjs-ng vendor stubs in the JS-runtime seam — `Evaluate` and `ExecutePendingJob`
— so **real JavaScript executes** and the **job/microtask queue drains** against the real engine, not
the recording mock. The first of the engine-primitive sprints (Layer 2 of the Cathode build-out).

> Scope from the PM (tcdent-approved): "CathodeRuntime — real JS eval. Complete Evaluate/
> ExecutePendingJob (today TODO stubs). Prove: real JS executes, job/microtask queue drains."

## Scope Boundary

**This sprint owns** turning the two TODO stubs into real quickjs-ng calls and proving them
behaviorally against the real engine: `Evaluate` runs a script and reports whether it threw + the
error; `ExecutePendingJob` pumps one real job and returns the tri-state; the tick's drain loop
(already built, today proven on a mock) drains a real microtask queue to empty.

**Out of scope (named):**
- **Value surfacing** — `FEvalResult` carries only `{bThrew, FJsError}`, not the produced JSValue
  (the JS↔Native Bridge's seam, sprint 007). 016 does NOT surface eval values.
- **The Module-eval async-rejection proof** — `JS_EVAL_TYPE_MODULE` is async: a module-level throw
  surfaces via promise **rejection on the job queue**, not the immediate `JS_IsException` the Global
  path uses. 016 **wires both flags** (per `EEvalKind`) but **proves discriminatingly on GLOBAL**; the
  Module async-rejection proof is its own (more complex) shape — flag if the team wants it in 016.
- **The interrupt-handler wiring** (`SetInterruptHandler` TODO-PREBUILD) — the safety budget; no 016
  proof needs it (all proofs run short scripts). Out.
- **The drain LOOP** — `ExecutePendingJob` is the one-job primitive; the drain-to-empty loop is the
  tick's bespoke logic (`CathodeTick`), already built. 016 makes the primitive real, not the loop.
- **Cross-module wiring** (the integration phase).

## Problem

`Evaluate` (CathodeJsRuntime.cpp:87) returns `FEvalResult{}` (`bThrew=false`) and `ExecutePendingJob`
(:94) returns `NoPending` — both TODO stubs, **always the benign value**, vendor calls in comments.
So real JS does not execute and the real job queue never drains; the tick/budget logic is proven only
against a recording mock (`CathodeRuntimeTickTests`). Grounded already: the `JS_Eval` shape
(`JSValue JS_Eval(JSContext*, const char* in, size_t len, const char* name, int flags)`, quickjs.h:1013;
`JS_EVAL_TYPE_GLOBAL=0`/`_MODULE=1`, :428) is exercised in `CathodeContractGlobalsTests`;
`TakePendingError` is implemented (JS_GetException → FJsError, single context); `CreateJsRuntime()`
stands up a real runtime+context.

**The tri-state is CONFIRMED, not guessed** (quickjs.c is not vendored — only the header — so verified
against the pinned upstream **quickjs-ng v0.15.1 / fd0a0210**, verbatim contract: *"return < 0 if
exception, 0 if no job pending, 1 if a job was executed successfully"*):
`ret == 0 → NoPending` (`*pctx`=NULL); `ret > 0 → Executed`; `ret < 0 → Faulted` (`*pctx`=the job's
context; Cathode is single-context, so `*pctx`==our Context and `TakePendingError` retrieves the
fault unchanged). The existing `DrainMicrotasks` loop (CathodeTick.cpp:76-98) already switches
NoPending/Faulted/Executed and consumes this primitive unchanged (N jobs → N+1 calls).

## Goal

`Evaluate` runs real JS (Global) and reports `bThrew` + the error; `ExecutePendingJob` pumps one real
job and returns the confirmed tri-state; the tick loop drains a real Promise chain in order to empty.
Each proven **discriminatingly via throw-on-success** — the proof bites the always-false stub.

## Final State

- **`Evaluate`**: `JS_Eval(Context, <utf8 Source>, len, <utf8 Name>, Kind==Module?MODULE:GLOBAL)`; on
  `JS_IsException(Result)` → `bThrew=true` + `TakePendingError` → `FJsError`; `JS_FreeValue(Context,
  Result)`. Both flags wired; proof on the Global path. **The Module flag is wired-but-DORMANT** — a
  one-line `Kind==Module?MODULE:GLOBAL` mapping with NO 016 caller and NO proof (its async-rejection
  proof is OUT); a known dormant path, proven by a future Module-eval sprint, not an unflagged gap.
- **`ExecutePendingJob`**: `JSContext* C=nullptr; int r=JS_ExecutePendingJob(Runtime, &C);` → the
  confirmed tri-state → `EJobPumpResult{NoPending(0), Executed(>0), Faulted(<0)}`.
- **The drain**: a real Promise chain enqueued via `Evaluate` → the tick loop calls real
  `ExecutePendingJob` until `NoPending` → the chain resolves in order, queue empties.

## In Scope (ordered slices)

Evaluate FIRST (the drain proof enqueues its Promise chain *via* Evaluate, so Evaluate must be real for
the drain discriminator to bite — RQ-1/RQ-2 resolved):

1. **`Evaluate` (real JS_Eval, Global).** Complete the stub. *Discriminating proofs — ALL
   throw-on-success (bThrew==TRUE carrying a read-back value; a `bThrew==false` assertion is VACUOUS,
   the stub passes it):* a plain `throw new Error('boom')` → `bThrew==true` + `Message=="boom"`; a
   **COMPUTED** throw `throw new Error(String(6*7))` → `bThrew==true` + `Message=="42"` (the engine
   computed it — **42 is NOT a literal in the source**, so it bites both the stub AND an echo); a
   **syntax error** → `bThrew==true` (the parse path); a cross-eval **side effect (inverted)** —
   eval1 `globalThis.__x=7` (setup, not asserted), eval2 `if(globalThis.__x===7) throw new
   Error('took:'+globalThis.__x)` → real: `bThrew==true` + `Message=="took:7"` (state persisted across
   evals on the shared context); stub: eval2 → `bThrew==false` → the `bThrew==true` assert FAILS.
2. **`ExecutePendingJob` + the real drain.** Complete the stub (the confirmed tri-state). *Proofs —
   the 3-way tri-state distinct test + the inverted drain:*
   - **NORMAL/drain (inverted, bites ran+ordered+complete):** `Evaluate` enqueues
     `Promise.resolve().then(()=>__log.push('a')).then(()=>__log.push('b'))`; the tick loop drains via
     real `ExecutePendingJob`; check eval `if(globalThis.__log.join('')==='ab') throw new
     Error('drained:'+__log.join(''))` → real: `bThrew==true` + `Message=="drained:ab"`; stub:
     NoPending immediately → `__log` empty → `bThrew==false` → FAILS. **Drain-to-empty** proven by
     `JS_IsJobPending(Runtime)==false` (quickjs.h:1203) / final `ExecutePendingJob`==NoPending — NOT a
     max-iteration cap.
   - **FAULTED (the load-bearing tri-state bite):** a throwing job carrying a computed message
     `Promise.resolve().then(()=>{ throw new Error(String(6*7)); })` → `ExecutePendingJob` returns
     `Faulted(<0)` → the tick routes to a recording Sink → assert the Sink received the error with
     `Message=="42"` (DISTINCT from NoPending; a `Faulted↔NoPending` swap silently drops the fault —
     the "42" never surfaces — so this bites the swap AND the stub).
   - **EMPTY:** `ExecutePendingJob` on an empty queue → `NoPending` immediately, no job runs (a
     `NoPending↔Executed` swap would infinite-loop → caught by termination).

## Non-Goals

- No value surfacing (the Bridge's seam) — proof via throw-on-success, never a returned value.
- **No `bThrew==false`-on-success assertions** — vacuous against the always-false stub (the PRIMARY
  reject; caught independently by dev + reviewer). Every discriminator is throw-on-success.
- No Module async-rejection proof (wire the flag, prove on Global); no interrupt-handler wiring.
- No re-authoring the drain loop; the recording-mock loop tests STAY (vendor-independent loop guard —
  do not delete them to "use the real engine").
- No cross-module wiring; no `FEvalResult` produced-value field (the Bridge seam).

## Starting Surface

Audit done (the dev's grounding): the two stubs (CathodeJsRuntime.cpp:87, :94); the confirmed JS_Eval
shape + flags + the tri-state convention (pinned upstream v0.15.1/fd0a0210); `TakePendingError`
(implemented); the drain loop (CathodeTick.cpp:76-98); the recording mock (CathodeRuntimeTickTests);
`JS_IsJobPending` (quickjs.h:1203). Inventory shape:
`stub → confirmed vendor call → result mapping → throw-on-success proof that bites the always-false
stub → proposed action`.

## Method

Real-engine tests via `CreateJsRuntime()` (not the mock). Prove **only via throw-on-success**:
`bThrew==true` carrying a value the engine **computed/read back** (the computed value never a source
literal — specified≠computed on the error channel); **no `bThrew==false` assertion** (the always-false
stub satisfies them). The tri-state convention is **upstream-grounded** (v0.15.1/fd0a0210, the verbatim
contract — quickjs.c is NOT vendored, so this is an upstream read, not an in-tree one),
**version-pin-corroborated** in-tree (`QJS_VERSION` 0.15.1 at quickjs.h:1410 == the pin; the header
signature + `JS_IsJobPending`), and — the load-bearing in-tree gate — **empirically gated by the 3-way
distinct test** (the computed-fault-message bites a Faulted↔NoPending swap; drain-to-empty bites a
NoPending↔Executed swap). Rest the gate on the empirical bite, not an in-tree convention read
([[reference_firewall_source_dispositive]] discipline — name the upstream binding as the at-risk one).
Run the FULL CathodeRuntime suite — the mock loop/budget/lifecycle tests STAY green (re-run
them if the loop code is touched — the shared-path lesson [[feedback_empirical_regression_shared_path]]).
Coherence per the change shape ([[reference_fresh_binary_marker_by_change_shape]]).

## Acceptance

- **Real JS executes:** the throw-on-success proofs — plain throw → `bThrew==true`+message; **computed
  throw** → `Message=="42"` (bites stub AND echo, 42 not a source literal); syntax error →
  `bThrew==true`; the inverted cross-eval side effect → `Message=="took:7"`.
- **Job queue drains:** the inverted drain → `Message=="drained:ab"` (ran + IN ORDER + complete);
  **drain-to-empty** by NoPending-termination — the production `DrainMicrotasks` loop stays UNCHANGED
  (terminates on `ExecutePendingJob==NoPending`; 016 does not re-author it). `JS_IsJobPending(Runtime)
  ==false` is a TEST-SIDE corroborating assertion only (redundant with NoPending — `ret==0` *is*
  list-empty upstream — never wired into the loop), distinguishing empty-termination from a cap. The
  FAULTED job's computed message `"42"` surfaces via the Sink (the tri-state `<0` bite).
- **Tri-state correct:** the 3-way distinct test (Normal/Faulted/Empty) — the convention matches the
  pinned upstream; a swapped convention is bitten (Faulted↔NoPending drops the "42"; NoPending↔Executed
  infinite-loops).
- **Discriminating:** every assertion is `bThrew==true`-carrying-a-read-back-value; **zero
  `bThrew==false`-only** assertions (the reject).
- **No regression:** the full CathodeRuntime suite green (the mock loop/budget/lifecycle tests stay).
- **Leak hygiene (BOTH):** source-trace — every `JS_Eval` result `JS_FreeValue`d, every
  `JS_GetException` freed, the Faulted-path exception handle freed; AND an **empirical** guard — N
  evals+drains incl a faulted job → **BOUNDED, not byte-exact** (assert no *unbounded* growth — quickjs
  internal caches/GC timing make `JS_ComputeMemoryUsage` non-byte-stable, so a "memory equal" assert is
  flaky — or a clean `JS_FreeRuntime` teardown, which trips on a dangling value).
- **Coherence (additive + bites-the-stub):** the discriminating real-engine test's Success IS the
  fresh-binary marker — a stale stub binary returns `bThrew=false` → the computed-throw assert FAILS
  (RED); green ⇒ the real engine ⇒ fresh.
- **Close record:** add a brief `CathodeRuntime/COVERAGE.md` — the carry-not-rediscover home for the
  verified tri-state convention (so a future quickjs re-vendor doesn't re-guess `<0/0/>0`) + the
  `FEvalResult`-no-value seam rationale.

## Review Questions (resolved at formulation)

1. **Slice structure** — RESOLVED: Evaluate first, then ExecutePendingJob+drain (the drain enqueues
   via Evaluate, so Evaluate must be real first).
2. **The discriminating proof** — RESOLVED: throw-on-success only (`bThrew==true`+read-back value); no
   `bThrew==false` assertion (vacuous against the always-false stub) — caught independently by dev +
   reviewer.
3. **The mock** — RESOLVED: the recording-mock loop tests STAY (vendor-independent loop guard); the
   real-engine tests are ADDITIVE.
4. **Interrupt handler** — RESOLVED: OUT (no proof needs it). The tri-state is in scope + confirmed.
5. **Module-kind eval** — wire both flags; prove on Global; the Module async-rejection proof is a
   separate concern (out of 016 unless the team adds it).
6. **CathodeRuntime COVERAGE.md** — add it as the close record (the most foundational module currently
   without one), the carry-not-rediscover home for the tri-state convention. (Dev + reviewer recommend;
   lead concurs; PM ratifies with the green-light.)

## Outcome

016 turned the two CathodeRuntime engine stubs **real** — the runtime now evaluates real JavaScript and
drains its job/microtask queue against the live quickjs-ng engine, not the recording mock.

**Commits (in order):**
- `bd5a551` — **real `Evaluate` (slice 1).** `JS_Eval` on the Global path; `JS_IsException` → `bThrew` +
  `TakePendingError`; `JS_FreeValue` every path; Module flag wired-but-dormant. Proven discriminatingly
  via throw-on-success (the computed `String(6*7)` → `Message.Contains("42")` biting stub and echo, the
  load-bearing proof and coherence marker; plus plain throw, syntax error, inverted cross-eval side
  effect). Uses the established `FScopedTestRuntime` RAII idiom.
- `ba0a585` — **real `ExecutePendingJob` + the integrated drain (slice 2).** The confirmed tri-state
  (`<0` Faulted / `0` NoPending / `>0` Executed, v0.15.1); the production `DrainMicrotasks` loop
  (unchanged) drains via the real one-job pump. Proven: a re-queuing chain → `"drained:ab"` with
  drain-to-empty (NoPending-termination; `JS_IsJobPending` test-side only); EMPTY → NoPending; **FAULTED
  via the reachable host-job** (`JS_EnqueueJob` → a `JSJobFunc` returning `JS_Throw(Error("42"))` → `-1`
  → the pre-existing Faulted→Sink routing surfaces "42") — *not* an author `.then` (a phantom: quickjs
  catches it → Executed + unhandled-rejection); a bounded-growth leak guard over 50 cycles.
- The close: this doc's Outcome + Postmortem, and **`CathodeRuntime/COVERAGE.md`** (the module had none;
  the carry-not-rediscover home for the tri-state convention + its re-vendor re-check trigger).

**Forks resolved:** the discriminating-proof rule (throw-on-success only — no `bThrew==false`,
independently caught by dev + reviewer); the FAULTED seam (tcdent ruled **B'** — prove the tri-state
*mapping* via the reachable host-job, not the unreachable author path, no new seam).

**Acceptance met:** real JS executes + the queue drains, each proven discriminatingly; full Cathode gate
**107/107**; the vendor-independent mock loop/budget/lifecycle guards stay green; tri-state grounded vs
the pin, not guessed; leak hygiene (source-trace + bounded empirical).

**Deferred / tracked (in COVERAGE.md):** (a) the author-JS unhandled-rejection → sink surface
(tcdent-deferred, a distinct fault-surfacing seam); (b) `ContractGlobals` self-isolation (a pre-existing
sprint-012 narrow-filter test-hygiene item); (c) the Module-eval async-rejection proof (the dormant
Module flag); the end-to-end worker-thread test.

## Postmortem

**Lead (uptime.browse-lead).** Two evidence forks defined this sprint and both were caught *before* a
build was spent. The proof discipline: with a value-less seam (`FEvalResult={bThrew,Error}`) and an
always-benign stub, my first-cut's `bThrew==false`-on-success proofs were vacuous — and the dev and
reviewer *independently* caught it, converging on throw-on-success. The FAULTED seam: I specified a
proof (author `.then` → Faulted) that was a phantom; the dev held at the seam rather than building it,
and I resisted ruling the fork until the mechanism was pinned — the reviewer verified it independently
(with honest evidence-levels) *and* found the host-job path that made the real deliverable provable.
The genuine intent fork (prove the tri-state mapping vs wire the user-meaningful fault surface) went to
tcdent with a lean and a non-blocking framing; he ruled B'. The seat's lessons: a proof built on an
*assumed* vendor behavior is a phantom — pin the mechanism (route the verification to the reviewer)
before ruling a fork; and an RQ-3 narrow-filter artifact (ContractGlobals) is a disposition to make,
not a foreign test to fix on env's first diagnosis — accept the full-gate as the real acceptance, track
the hygiene as a follow-on.

**Dev (uptime.browse-dev, Mundo).**
1. **Ground a vendor BEHAVIOR against the real engine before building a proof on it.** The FAULTED
   spec assumed author `.then` → Faulted; the pinned quickjs proves the reaction *catches* the throw →
   Executed + unhandled-rejection, so Faulted is unreachable from author JS. A proof on a wrong vendor
   assumption is a phantom — catch it at the seam, surface the fork, don't build it.
2. **Throw-on-success is the only discriminator for a no-value seam.** The stub returns `bThrew=false`
   for every eval (incl the check eval), so any `bThrew==false` assertion is vacuous. Every proof is
   `bThrew==true` (or recording-Sink content) carrying a value the engine computed and *not* in the
   source (`42`=`String(6*7)`, biting echo too).
3. **A pre-existing test failing under a narrow filter is an RQ-3 disposition for the lead, not a fix.**
   Env's first diagnosis blamed my new test; the FScopedTestRuntime fix did *not* resolve it, proving
   ContractGlobals' own pre-existing non-isolation. Test pre-existing-vs-mine first (re-run without my
   change), then route the disposition up.
4. **The host-job is the reachable `<0` path** (the grounding that unlocked B') — `JS_EnqueueJob` with a
   throwing `JSJobFunc` proves the `<0`→Faulted mapping + the Sink routing with no new production seam;
   the author-JS unhandled-rejection surface is a distinct channel, a tracked follow-on.
5. **Carry-not-rediscover the vendor convention** — the COVERAGE.md pins v0.15.1 + the re-check-on-
   re-vendor trigger (quickjs.c not vendored → upstream-grounded; the empirical 3-way test re-fires it).

**Review (uptime.browse-review).** The durable lesson is proving an engine primitive behind a
deliberately value-less seam when the stub returns the benign value for everything: throw-on-success —
every discriminator is `bThrew==true` (or a recording-Sink fault) carrying a *computed* value not in the
source (biting stub AND echo); a `bThrew==false`-on-success assertion is vacuous and the primary reject
(dev + I both caught a vacuous positive in the first-cut). The sharpest turn was the FAULTED phantom:
re-derived from the pinned source that quickjs catches a handler-throw (Executed, not Faulted), reported
with honest evidence-levels (the tri-state convention confirmed source-dispositive; the reaction
catch-and-reject corroborated-*inference* since the body lives in the non-vendored `.c`; the
rejection-tracker confirmed in-tree). That unblocked B' — when the obvious author-input proof is a
phantom, prove the actual deliverable (the `<0`→Faulted mapping) via the reachable path (a synthetic
host-enqueued job), not the unreachable author path, not a new seam. And the channel discipline: the
FAULTED discriminator is the recording-Sink content, a different non-vacuous channel than the drain's
`bThrew`. Next reviewer inherits: for a value-less seam force a computed value through the
throw/error/sink channel and reject every benign-stub-passes assertion; verify a vendor seam with honest
evidence-levels (flag non-vendored-`.c` bindings as inference); and when the spec makes the obvious
proof a phantom, find the reachable path to the actual deliverable.
