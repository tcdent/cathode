# Sprint 010 — Cathode bring-up & discovery (first execution)

## Nature

The **first execution sprint** — the one all nine scaffold sprints (001–009) deferred to. It is a
**bring-up and discovery pass**, not a feature build: the gates across 001–009 were *structural*
(no-build scaffolds, source/diff/seam re-derivation); here the **post-build** half of verification
activates, and "verified" changes meaning. The plugin is compiled for the first time, its tests are
run and *observed*, and the output is a **fault inventory** that scopes the next sprint.

> "The scope of this is to get the build clean and do some initial discovery."

The honest end state, stated up front:

> "At the end of it, we will have a clean build, but we will not necessarily have an application that
> is fully functioning the way we expect. A major deliverable of this sprint is identifying, based on
> the unit testing and the functional surface, which systems need attention. That will form our next
> sprint, where we will prioritize each of the faults we have discovered and come up with mitigation
> plans for them."

## Scope Boundary

**This sprint owns:**
- compiling the plugin to a **clean build** (un-quarantine + resolve whatever breaks);
- **running and observing** the unit-test harness (failures are *not* blocking — they are data);
- **(bonus)** a code-surface discovery pass (a script the human provides), env permitting;
- producing the **fault inventory** — which systems need attention — as the sprint's deliverable.

**This sprint does NOT own:**
- making the application **fully functional** — bring-up only;
- **fixing the discovered faults** or making the tests pass — observation, not remediation;
- **prioritizing / mitigating** the faults — that is the *next* sprint (this sprint feeds it);
- the lead running builds or implementing — the **developer drives** build/run/test; the lead
  coordinates and the reviewer verifies coherence.

## Problem

Nine components are authored and quarantined (`EnabledByDefault:false`, absent from
`game/uptime.uproject`, vendor `ThirdParty` blocks commented). **Nothing has ever compiled or run.**
Every `TODO-PREBUILD` fork-variable signature (quickjs-ng / litehtml / Blend2D / HarfBuzz) was written
against headers but never reconciled against the *real* compiled vendor headers; every COVERAGE
validation-pass deferral is unproven. The build environment is **shared** with other teams, so the
first vendor-linking build must be coordinated.

## Goal

A **clean build** of the activated plugin, a coherent **observed run** of the unit-test harness, and a
grounded **fault inventory** identifying which systems need attention — the input to the next
(prioritize-and-mitigate) sprint. Not a working application; a bring-up.

## Final State

1. The plugin is **un-quarantined enough to compile** — enabled / referenced as needed and the vendor
   `ThirdParty` blocks uncommented — and the project **builds clean** (zero errors). How activation is
   scoped on the shared env (the whole uproject vs an isolated Cathode build/target) is settled in
   Slice 1 and **coordinated so it does not break the other team's build**.
2. A **build-coherence marker** exists — a build signature / log marker only the *current* build can
   produce — so any test run can be proven to come from the current binary (source-mtime-precedes-run
   is *not* sufficient; a stale binary masks regressions). This is established **before** any test run
   is read as evidence.
3. The **unit-test harness has been run** (targeted per-subsystem and/or the full harness) against the
   current build, and its results — **pass and fail counts, and the failing subsystems** — are captured
   numerically from the **settled run log**, not by eye. Failures are recorded, **not fixed**.
4. **(Bonus, env permitting)** the human's **code-surface discovery script** has been run and its
   coverage captured.
5. The **fault inventory** — `docs/teams/browse/010-bringup-findings.md` (or appended to this doc) —
   lists, per system: what the build resolution required, which tests fail and how, and what the
   surface pass showed; flagging which systems need attention. This is the deliverable that scopes the
   next sprint.

## In Scope (ordered slices)

1. **Compile to a clean build (open-ended).** Activate the plugin (enable + uncomment the vendor
   `ThirdParty` blocks), walk the **`TODO-PREBUILD` tags across 002–009 first** (the fork-variable -ng /
   litehtml / Blend2D / HarfBuzz signatures to reconcile against the real vendored headers), and resolve
   the build errors to **zero**. Establish the **build-coherence marker**. *This slice is deliberately
   open-ended* — the error volume is unknown; the developer cycles build→fix→build, sub-delegating the
   mechanical loop as needed, and reports the clean-build evidence. *A genuine resolution, not
   comment-out-to-compile* — if something cannot be resolved in scope, it is **recorded as a fault**,
   not silenced. **Clean build is the signal to start Slice 2.**
2. **Run + observe the unit tests.** Run the harness (targeted and/or full) against the current build;
   capture pass/fail counts and the failing subsystems from the settled log. **Failures are not
   blocking** — they are the inventory. Confirm coherence (the run came from the current build) before
   trusting any result.
3. **(Bonus) Code-surface discovery.** *If the environment is deemed stable enough*, run the human's
   surface-discovery script and capture the coverage. A discovery pass — observe what surface is
   exercised.

The **fault inventory** is assembled across all three slices and is the closing deliverable.

## Non-Goals

- Do **not** make the application fully functional — bring-up only.
- Do **not** fix the faults or make the tests pass — observe and record; remediation is the next sprint.
- Do **not** prioritize or plan mitigations here — the inventory feeds the next sprint, which does that.
- Do **not** silence a build error by commenting code out to compile — resolve it genuinely or record it
  as a fault. (A real fix vs a papered-over one is exactly what the next sprint must trust.)
- Do **not** trust a test result without build coherence — a green on a stale binary or a discarded
  harness proves nothing.
- Do **not** collide with the other team on the shared build env — coordinate the activation + the
  authoritative build.
- The **lead does not run builds or implement**; the developer drives, the reviewer verifies coherence.

## Starting Surface

The inventory already exists, distributed across the nine sprints: the **`TODO-PREBUILD` tags** (the
pre-build signature-reconciliation walk) and each module's **`COVERAGE.md` validation-pass deferrals**
(the booked checklist), mirrored by the reviewer's **deferral ledger** (the acceptance map). Before
editing, the developer collects the `TODO-PREBUILD` tags into the pre-build walk order and confirms the
build-env activation approach.

## Method

The **post-build discipline** (the shift from the scaffold gates):
- **Coherence first** — prove the run came from the *current* binary (a build signature / log marker
  only the new build produces; the log settled, mtime-stable, scoped to the post-build window) before
  any result is read as evidence.
- **Target the live path** — a green on a discarded harness / parallel branch / fallback proves nothing;
  confirm the thing under test runs in the real build.
- **Numbers from the settled log, never by eye**; flag inference as inference if a proof has rotated out
  of reach.
- **Serialize on the shared env** — the developer runs the authoritative build and signals the lead;
  the activation is coordinated so the other team's build is not broken.
- Build errors are **resolved genuinely or recorded as faults** — never silenced to compile.

## Acceptance

- **Slice 1 (the one hard gate):** the activated plugin **builds clean** (zero errors), and a
  **build-coherence marker** is established and demonstrated. Resolutions are genuine; anything not
  resolvable in scope is recorded as a fault, not silenced. The other team's build is not broken.
- **Slice 2 (observational, not pass/fail):** the unit-test harness has **run coherently** (proven from
  the current build), and pass/fail counts + failing subsystems are **captured numerically** from the
  settled log. Failures are data, not blockers.
- **Slice 3 (bonus):** if run, the surface-discovery script's coverage is captured.
- **Deliverable:** the **fault inventory** — which systems need attention — grounded in the build
  resolution, the test results, and (if run) the surface pass. This closes the sprint and scopes the
  next.

## Review Questions (open forks)

1. **Activation scope on the shared env** — does the plugin get enabled in the whole `uproject` (it then
   compiles for everyone on the shared build) or built via an isolated Cathode target/config? Settle in
   Slice 1, coordinated so the other team is not broken.
2. **Build-coherence marker shape** — what signature does the dev bake in (a log line only the new build
   emits, a build-id artifact, a version stamp) so the reviewer can prove run-from-current-binary? The
   reviewer flagged this as the first thing to establish.
3. **Test scope** — targeted per-subsystem vs the full harness for Slice 2; the developer's call as the
   build comes up, since failure-isolation may favor targeted first.
4. **The surface-discovery script** — the human provides it for Slice 3; its exact shape and what
   "surface coverage" it reports are the human's, surfaced when Slice 3 is reached.

---

## Fault Inventory (Deliverable)

The closing deliverable, grounded in the Slice 1 build resolution and the Slice 2 observed run. Slice 3
(the bonus surface-discovery pass) was **not run** — tcdent ruled to close 010 on the S1+S2 inventory and
carry the runtime-verification artifact to the next sprint. Every claim below is re-derived from the
settled logs + source by the reviewer independently of the developer's report.

### Coherence basis

All evidence is anchored to the committed S1 binary — `ae600fa`, build-coherence marker
`[CATHODE-BUILD-SIGNATURE] compiled Jun 6 2026 19:05:11`, present in each result-run's own settled log
(crash run session `16:17:07`; runnable-21 run session `16:34:09`). The marker fired live in both, proving
run-from-current-binary before any count was read.

### Slice 1 — build resolution (what it required)

All 8 Cathode modules compile + link clean in a single from-scratch pass against the real vendored libs
(shared quickjs DLL + static litehtml/gumbo/HarfBuzz/Blend2D+asmjit); the plugin loads clean. The ~10
bring-up faults were resolved **genuinely** (no comment-out-to-compile) — qualified `Cathode::FThreadAffinity`,
`GetTypeHash` ADL, brace-init past the most-vexing-parse, explicit lambda return, the litehtml `check()`
macro guard, the `SameJsObject` identity helper + `EventType` rename, `CATHODEBINDING_API` exports, the
`CathodeDom→CathodeRuntime` dependency, and the quickjs shared-DLL conversion + qjs.dll soname staging. Full
record in commit `ae600fa`. **No fault was silenced.** The no-build scaffold signatures (24 `TODO-PREBUILD`
groups) matched the real headers with zero corrections; every bring-up fault was in cross-module / integration
/ deploy reality, not in the scaffold's vendor-call signatures.

### Finding #1 — the test-infra auto-start-singleton seam (headline)

**A test-infrastructure defect; the product is correct.** Production auto-starts the one JS-runtime singleton
at module load (`CathodeRuntimeModule::StartupModule` → `Worker->Start()`, the human-ruled S1 auto-start:
"exactly one, shared-global, many-surfaces"). The single-instance guard (`CathodeJsRuntime.cpp:220-231`,
atomic `GJsRuntimeLive` compare-exchange) **gracefully refuses** a second `CreateJsRuntime` — logs an Error
and returns an `IsValid()==false` instance ("stability, not a crash"). That is correct, intended behavior.

The fatal is in the **test**: the no-build-era tests (`CathodeCallableHandleTests.cpp:40`) call
`CreateJsRuntime` to build their **own parallel** runtime; production already holds the singleton, so the
test's instance is the gracefully-refused invalid one; the test then calls `GetNativeContextHandle()` on it
**without checking `IsValid`** → null context → `:46` `Eval(nullptr)` → access violation in `qjs.dll`. The
"harness must run the production path, not a parallel one" principle, come due the moment a runtime actually
started — invisible to all nine no-build scaffold sprints.

The crash is a **process-killer**: it aborted at the first `CreateJsRuntime` test before that test's first
assertion, took the shared editor down, and produced **0 test completions**. Remediation is
**test-architecture** (next sprint): unblock the tests to drive/share the production singleton (or a
test-injectable/resettable seam for the create+destroy lifecycle tests), and add the `IsValid` guard. **The
auto-start ruling is not challenged.**

**Blocks 11 of 32 test TUs** in the live editor (every TU that calls `CreateJsRuntime`): all 5 CathodeBinding
(Callable, JsValue, Marshal, NativeClassRegistry, Teardown), all 5 CathodeDom (Attribute, Event, Layout, Node,
Structural), and CathodeRuntime Lifecycle. The cyclic `JS_RunGC`-reclaim come-due proof lives in the Binding
Teardown TU — inside the blocked set.

### Runnable-21 observation — the three-way checked-status

The 21 non-runtime TUs (filtered to avoid `CreateJsRuntime`: Layout, Text, Rasterizer, Surfaces, Resolver,
RuntimeCore-non-Lifecycle) ran clean: **42 tests, 42 Success, 0 fail, 0 fatal** (Layout 10, Rasterizer 7,
RuntimeCore-non-Lifecycle 9, Text 8, Surfaces 6, Resolver 2). Blocked-set guard confirmed: 0 Binding / 0 Dom /
0 RuntimeCore.Lifecycle ran. **42/42 green does not discharge any come-due proof** — classified three ways:

- **Genuinely-exercised (`checked>0`) — BUILT + PROVEN.** All 42 assert their own routing/dispatch/bookkeeping/
  ordering logic against recording/stub backends (`FRecordingMeasurer`, `FRecordingRelayoutBackend`,
  `FStubDocumentSource`, `FStubGeometrySource`, stub `IResourceResolver`) — the legitimate input-isolation
  design. The **bespoke-logic layer is built and green** across all 6 runnable subsystems (Layout paint/
  relayout/font-cache/resource-routing, Rasterizer, Text, Surfaces, Resolver, RuntimeCore assembly/safety/
  threading/tick).
- **Passing-but-scripted / vacuous-green (`checked=0` on the real primitive) — UNVERIFIED, not a pass.** Three
  Layout come-due proofs ran + passed but never drive the real primitive: **real litehtml layout output**
  (zero `litehtml`/`FLiteHtmlLayoutEngine` in any Layout test; scripted/stub), **decode-for-size**
  (`ContainerRouting` asserts `RouteImageSize` returns `0x0` — the deferral placeholder, no decode), **real
  observation geometry** (drives `FStubGeometrySource`, "never litehtml's computed boxes"). Recorded as
  *real-X come-due UNVERIFIED, scripted-green only*.
- **`checked=0` / unexercised-blocked.** Cyclic `JS_RunGC` reclaim — in the blocked-11 Binding Teardown TU;
  did not run. No GC discharge from runnable-21.

### Per-system attention map

| System | Build (S1) | Runtime observation (S2) | Needs attention |
| --- | --- | --- | --- |
| CathodeRuntime | clean | non-Lifecycle proven (`checked>0`); Lifecycle blocked-11 | Lifecycle TU blocked behind finding #1 |
| CathodeBinding | clean | **all 5 TUs blocked-11** (incl. cyclic GC reclaim) | finding #1 unblock; then real JS_RunGC |
| CathodeDom | clean | **all 5 TUs blocked-11** | finding #1 unblock |
| CathodeLayout | clean | bespoke green; **3 real-primitive proofs UNVERIFIED** | real litehtml render + decode-for-size + real geometry |
| CathodeRasterizer | clean | proven (`checked>0`) | — (bespoke proven; real-paint validation later) |
| CathodeText | clean | proven (`checked>0`) | — |
| CathodeSurfaces | clean | proven (`checked>0`) | — |
| CathodeResolver | clean | proven (`checked>0`) | — |

### Carry-forward (next-sprint scope)

**Zero product defects were found.** The two faults are: (a) the finding-#1 test-infra seam, and (b) the four
real-primitive integrations that remain unverified. Critically, (b) is **consistent with the scaffold-era
deferral ledger — NOT a regression**: these proofs were recorded come-due to the validation pass, and the
existing suite **cannot prove them by construction** (scripted/stub/blocked). Discharging them is **net-new
work**: tests that drive **real litehtml render**, a **003 decode-for-size**, a **real `JS_RunGC`** reclaim,
plus the **finding-#1 test-infra unblock** so the 11 Binding/Dom/Lifecycle TUs run. The deferral ledger's four
runtime come-due proofs carry forward unproven into the next sprint.

---

## Postmortem

Role-scoped retrospectives, appended at sprint close. Durable lessons for the next sprint; they do not rewrite
the contract above.

### Lead retro (uptime.browse-lead)

- **The scaffold→execution split paid off, and the post-build half is the backstop for exactly what source-read
  can't see.** Nine no-build sprints got the architecture right (24 signature groups matched, zero corrections),
  but compile-readiness, cross-module linkage, the stateful-runtime DLL decision, and runtime seams were all
  invisible until the real build/run. Framing 010 as bring-up *and discovery* — "a clean build, not a fully
  functioning app" — was the right honest scope; the deliverable was the inventory, not green tests.
- **Arbitrate on evidence, never on direction-looks-good — and the strongest evidence binds the whole causal
  chain.** Two contradictions surfaced and were not averaged: the dev's "guard fatal-asserts" vs the reviewer's
  source-cited graceful-refuse-then-test-null-deref (the reviewer's line-cited re-derivation won; the dev
  cite-checked and agreed), and "42/42 green" vs "all come-due proofs `checked=0`" (the three-way classification
  held the line — a vacuous green is not an observation). A passing count is a sanity check, not proof.
- **Crossed signals in fast multi-agent rounds — confirm before reverting, retract promptly when moot.** I
  authorized a touch-all-source recompile, then the reviewer's "it reopens coherence" landed; the dev had
  already run it (mtime-only, re-anchoring coherence) so it turned out the gold-standard artifact, not churn. I
  escalated a crash-modal blocker to the PM, then env corrected "clean crash, no modal" — I retracted before the
  human chased a non-existent dialog. The fix was the confirm/retract, not the first call.
- **Hold the role boundaries; route forks, don't resolve them.** The finding-#1 remediation (test-architecture
  vs product change) was surfaced to the PM/human, not picked by me — and catching that the product was correct
  *before* browse-design framed a product-auto-start fork kept the next sprint scoped right. Counts come from
  tool output, not lead narration (two by-type slips on the commit were caught by the dev/PM — the staged set was
  always explicit-path verified; only my prose drifted).

### Developer retro (uptime.browse-dev)

- **No-build proves bespoke LOGIC; it cannot prove INTEGRATION or LIFECYCLE.** The 002–009 header-grounded
  scaffold held — all ~24 `TODO-PREBUILD` signatures matched as written, and the bespoke layer compiled, linked,
  and ran 42/42 green on first exercise. But no-build reasons over one TU with stubbed seams: it never starts a
  runtime (so the auto-start collision was invisible) and its scripted backends always pass (so integration gaps
  were invisible). Treat integration and lifecycle as inherently runtime-discovery, never assumed from a green
  scaffold.
- **Crash-adjacency is not crash-cause; characterize from the source chain.** I reported "the singleton guard
  fatal-asserts," inferred from a crash-adjacent `LogCathodeRuntime: Error:` line. The reviewer's source cite-check
  corrected it: the guard *gracefully refuses* (`CathodeJsRuntime.cpp:220-231`), and the fatal is the test's
  unchecked-`IsValid` null-deref (`CathodeCallableHandleTests.cpp:41`→`:46`). Read the guard impl + the test's call
  sequence, and when the reviewer line-cites, re-read and reconcile at the source rather than defend the first read.
  (Also a "harness must run production code" failure — the test diverged to a parallel runtime *and* ignored the
  graceful-refuse contract.)
- **Structurally-present-and-green is NOT real-primitive-proven.** 42/42 passed while all four come-due proofs are
  `checked=0`: the green is the bespoke layer (recording/scripted backends, correctly tautology-guarded), and the
  runnable Layout proofs pass *by asserting the deferral* (decode-for-size asserts `0x0`; layout/observation run on
  stubs, zero litehtml). Pass/fail is necessary-not-sufficient; the gate is checked-count + does-it-drive-the-real-
  primitive. Bake the `checked>0` / vacuous-green tag into all come-due reporting.
- **Discharging the four real-primitive integrations is net-new, with a gate.** The finding-#1 test-infra unblock
  is the critical path — until the harness stops spawning a parallel runtime / honors graceful-refuse, the entire
  11-TU JS-runtime-dependent set (incl. real `JS_RunGC` reclaim) stays unrunnable. Then, in parallel: drive the real
  `FLiteHtmlLayoutEngine` and assert against litehtml's own computed boxes; replace the `0x0` decode placeholder with
  a real 003/Blend2D image-info decode. Next-sprint critical path = test-infra unblock first, then the real-primitive
  integrations.
- **Minor open item (cosmetic, non-blocking):** the quickjs DLL is staged as `qjs.dll` (its native soname; works).
  Fully-consistent naming is a one-line tooling `OUTPUT_NAME=quickjs` on the shared target → revert the stage name to
  `quickjs.dll`. The human's tooling call, no rush.

### Reviewer retro (uptime.browse-review)

- **For a crash, re-derive the fatal from the callstack line + source, never the narrative.** The dev reported
  "singleton guard fatal-asserts"; source (`CathodeJsRuntime.cpp:222-231`) + the callstack showed the opposite —
  the guard gracefully refuses, and the fatal is the test's unchecked null-context `Eval`
  (`CathodeCallableHandleTests.cpp:40-46`). That correction flipped the finding from "product guard defect" to
  "test-infra null-deref; product correct," which stopped browse-design from reframing the human's auto-start ruling
  as the problem. The report orients; the failing line is the evidence — and product-defect vs test-infra-defect
  changes the entire downstream fork.
- **Green is necessary, not sufficient — confirm the test drives the REAL primitive, from source.** 42/42 passed yet
  every runtime come-due proof is `checked=0`. The classification turns on which primitive a test drives, settled by
  source not log-verbosity: a litehtml-free Layout test proves nothing about real render; `ContainerRouting` asserting
  `0x0` proves the deferral, not a decode; observation over `FStubGeometrySource` proves the walk, not real geometry.
  A vacuous green is `checked=0` on the come-due; `mismatch=0` on `checked=0` means nothing. `-LogCmds` verbosity was
  correctly declined — per-assertion counts cannot move a determination source already settles.
- **Classify an unproven behavior against the ledger before calling it a gap.** Regression = worked-then-broke (fix
  this sprint); carry-forward-unproven = never-yet-proven, on the deferral ledger (net-new work to discharge). The four
  come-due proofs are the latter; conflating the two mis-scopes both, and the distinction is what keeps the next sprint
  on net-new proof work rather than a phantom defect hunt.
- **A fatal is a process-killer — separate source-derived scope from observed results.** The crash halted the whole
  run (0 completions); the "21 runnable" was a source split (what would not hit the crash), not an observed result.
  Never report source-derived "would-run" as "ran"; anchor observed counts to what the settled log shows completing.
- **Coherence is per-result-run, not once-per-build.** Each S2 run (crash session `16:17:07`; rerun `16:34:09`) was
  coherence-gated on that run's own marker (`19:05:11` = unchanged `ae600fa`, fired live in that session) before any
  count was read. Same binary, two sessions, two checks — a count from an unverified result-run is rejectable before
  it is read.
