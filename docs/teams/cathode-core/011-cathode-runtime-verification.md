# Sprint 011 — Cathode runtime verification & test-infra unblock

## Nature

The **second execution sprint** — the first that builds *on* the 010 fault inventory rather than
producing it. 010 proved the plugin compiles, links, and loads, and discovered that **structurally-
present-and-green is not real-primitive-proven**: 42/42 bespoke tests passed while every runtime
come-due proof stayed `checked=0`, and a test-infra seam blocked the entire JS-runtime test set. This
sprint **clears that seam** and **builds the runtime-verification capability** whose first job is to
**discharge the four unverified-by-construction integrations** — driving the *real* primitives and
asserting against their *own* output. It inherits the 010 acceptance bar verbatim:

> "A real-primitive discharge means DRIVE the real primitive and assert against ITS OWN output
> (`checked>0`) — a scripted/stub green is NOT a discharge. The three-way classification is the
> standing bar." (browse-pm, carrying the 010 reviewer/lead discipline forward.)

## Scope Boundary

**This sprint owns:**
- **[a]** the **finding-#1 test-infra unblock** — the harness stops spawning a parallel runtime / honors
  the production graceful-refuse, plus the `IsValid` guard — so the 11 blocked TUs run (critical path);
- **[b]** **building the runtime-verification artifact** (`docs/browse/components-runtime-verification.md`):
  faculty-(A) surface introspection + faculty-(B) capability verification + the structured self-report,
  **integrating + refactoring `observe/`** as its browse-owned in-engine home;
- **[c]** **discharging the four faculty-(B) first targets** — real litehtml layout output, decode-for-
  size, real observation geometry, cyclic `JS_RunGC` reclaim — each against the real primitive's own
  output. **[b] + [c] are one thread.**

**This sprint does NOT own:**
- **challenging the auto-start ruling** — the product guard is correct (010 verified); the unblock is
  test-architecture, not a product change (unless the settled fork explicitly chooses to revisit it,
  which is the human's call — see Review Questions);
- **external rendering parity** (`observation.md` — Cathode-vs-Chromium oracle) — that is a sibling
  concern that *shares the introspection mechanism* but is not this sprint's deliverable;
- **reimplementing the underlying reads** — geometry/cascade belong to Layout & Style, node/GC to the
  JS↔Native Bridge; this artifact **composes and reports**, it does not reimplement;
- the full **surface-drift CI gate** beyond emitting the reconcilable self-report (the gate's
  productionization is later);
- the lead running builds or implementing — the **developer drives** build/run/test via `uptime.env`;
  the reviewer verifies coherence; the lead routes + arbitrates.

## Problem

The 010 inventory, concretely:

1. **The finding-#1 seam.** Production auto-starts the one JS-runtime singleton at module load; the guard
   (`CathodeJsRuntime.cpp:220-231`) **gracefully refuses** a second instance (returns `IsValid()==false`).
   The no-build-era tests (`CathodeCallableHandleTests.cpp:40`) build their **own parallel** runtime, then
   deref its refused null context **unchecked** (`:41`→`:46` `Eval(nullptr)`) → access-violation process-
   killer. This **blocks 11 of 32 TUs** in the live editor (all 5 Binding, all 5 Dom, Runtime Lifecycle),
   including the cyclic `JS_RunGC`-reclaim proof.
2. **Four unverified-by-construction integrations** (`checked=0`, carry-forward not regression): the
   existing suite **cannot prove them by construction** — Layout tests are litehtml-free/scripted (real
   render `checked=0`), `ContainerRouting` asserts `RouteImageSize==0x0` (the deferral placeholder, no
   decode), observation runs on `FStubGeometrySource` (not litehtml boxes), and the GC proof sits behind
   the seam.
3. **No runtime self-report.** The build gate is static (author code ⊆ declared surface); nothing confirms
   the *live* exposed surface matches the declared contract, or that the real primitives *function*. The
   `observe/observer.js` prior art is a standalone Chromium-side harness, not yet a browse-owned in-engine
   faculty.

## Goal

The 11 blocked TUs **run** (seam cleared, product unchanged); a **browse-owned runtime-verification
faculty** exists in-engine (introspection + capability + self-report, `observe/observer.js` folded in);
and the **four first-target integrations are discharged** — each driving the real primitive and asserting
its own output, classified genuinely-exercised, not scripted-vacuous. The design-of-record rationale,
verbatim:

> "This artifact is folded into Cathode as a built-in capability; the `observe/` harness is transferred to
> browse-team ownership, refactored, and integrated as its in-engine home; the build is scoped next-sprint."
> (tcdent, 2026-06-14, ratifying `components-runtime-verification.md`.)

## Final State

1. The **finding-#1 seam is cleared** by the settled test-architecture mechanism + the `IsValid` guard:
   the 11 previously-blocked TUs (all Binding, all Dom, Runtime Lifecycle) **run in the live editor
   without the process-killer crash**, pass/fail captured coherently per-result-run. The auto-start ruling
   stands unless the human's settled fork explicitly revisits it.
2. The **runtime-verification faculty is built**, browse-owned: faculty-(A) enumerates the actually-exposed
   surface and reconciles it against the declared contract; faculty-(B) exercises the real primitives;
   both feed one **structured, machine-readable self-report** (the `observer.js` snapshot lineage + a
   capability section). `observe/observer.js` is **refactored into the in-engine home**; the introspection
   walk is **shared with `observation.md`, not duplicated**; `observe/README.md` + `observation.md`'s
   external-observer references are **reconciled** to the new ownership.
3. The **four faculty-(B) first targets are discharged**, each `checked>0` against the real primitive's own
   output: real litehtml layout output (real `FLiteHtmlLayoutEngine`, asserted against litehtml's own
   computed boxes); decode-for-size (real image-info decode over resolver bytes → real non-zero
   dimensions); real observation geometry (real litehtml boxes, not `FStubGeometrySource`); cyclic
   `JS_RunGC` reclaim (a real GC actually reclaims the node↔listener cycle). Each is reviewer-classified
   genuinely-exercised, not passing-but-scripted.

## In Scope (ordered slices)

1. **[a] Finding-#1 test-infra unblock (critical path).** Settle the test-architecture mechanism (Review
   Question 1 — browse-design/human owns the fork; settle *before* implementing), apply it + the `IsValid`
   guard, and confirm the 11 blocked TUs run without the crash. *This gates the cyclic-GC discharge and
   the rest of the JS-runtime test set.* Method: run the unblocked TUs via env, coherence-gated per-result-
   run; capture pass/fail. Resolve genuinely — no silencing, no parallel-path harness.
2. **[b] Build the runtime-verification faculty + integrate `observe/`.** Build faculty-(A) introspection
   (enumerate exposed surface → reconcile against the declared contract) and the faculty-(B) framework
   (exercise-and-report), emitting the structured self-report; settle the self-report schema and the
   introspection/probe mechanism (the team's "how"). Refactor `observe/observer.js` into the browse-owned
   in-engine home; reconcile `observe/README.md` + `observation.md` refs; do **not** duplicate the walker
   or reimplement the delegated reads.
3. **[c] Discharge the four faculty-(B) first targets.** Through faculty-(B), drive each real primitive and
   assert its own output (`checked>0`): real litehtml layout output, decode-for-size, real observation
   geometry, cyclic `JS_RunGC` reclaim (gated on slice 1). May carry lettered phases (one per integration)
   for risk; full review acceptance at the slice boundary. Each discharge is reviewer-classified
   genuinely-exercised.

## Non-Goals

- Do **not** challenge or revisit the auto-start ruling — the product guard is correct (010-verified); the
  unblock is test-architecture. (Revisiting auto-start is *only* on the table if the human's settled fork
  explicitly chooses it.)
- Do **not** conflate runtime-verification (internal self-report) with `observation.md` (external Chromium
  parity) — **share** the introspection mechanism, do not duplicate it; do not build the parity loop here.
- Do **not** reimplement the delegated reads (geometry/cascade → Layout & Style; node/GC → the Bridge) —
  compose and report.
- Do **not** count a scripted/stub green as a discharge — the three-way classification is the standing bar;
  a real-primitive discharge drives the real primitive and asserts its own output.
- Do **not** delete `observe/` pieces just because they roll in — trace callers + reconcile the
  `observation.md`/README references first (do not delete a fallback just because production does not hit
  it — trace consumers and tests first).
- Do **not** regress the accepted baselines — the S1 clean build and the 42/42 bespoke-green stay intact.
- The **lead does not run builds or implement**; the developer drives via `uptime.env`, the reviewer
  verifies coherence.

## Starting Surface

Audit first, before editing:
- **The seam:** the 11 blocked TUs; the finding-#1 source chain (`CathodeJsRuntime.cpp:220-231` guard,
  `CathodeCallableHandleTests.cpp:40-46` null-deref).
- **The four integration sites:** the litehtml-free Layout tests, `ContainerRouting`'s `0x0` assert,
  the `FStubGeometrySource` observation tests, the Binding Teardown GC proof.
- **The prior art + refs:** `observe/{observer.js, observe.mjs, diff.mjs, parity.sh, README.md}`,
  `observation.md`, and the declared contract surface (`spec-surface.md`, `components-dom-surface.md`,
  the generated `.d.ts`).

Inventory shape the developer produces before editing:
`site -> current-state (scripted/stub/blocked/placeholder) -> real-primitive target -> consumer -> proposed action`.

## Method

- **Coherence per-result-run** — the `[CATHODE-BUILD-SIGNATURE]` marker in each run's own settled log,
  current-stamp + fired-live, verified before any count is read (a stale/absent marker = reject before
  reading).
- **Build/run via `uptime.env`** — the developer requests cycles; neither lead nor dev touches build
  tooling; env coordinates run isolation on the shared tree (as in 010).
- **The three-way classification is the acceptance bar** — genuinely-exercised (real primitive,
  `checked>0`) vs passing-but-scripted/vacuous vs `checked=0`. **Source settles which primitive a test
  drives**, not log-verbosity.
- **Independent reviewer re-derivation** from the settled log + current source — never the developer's
  report; no lateral dev↔review coordination.
- **Resolve genuinely or record as a fault** — no silencing to pass, no parallel-path harness (the
  finding-#1 lesson: the harness must run the production path).
- **Hold the accepted baselines** — the S1 clean build and the 42/42 bespoke-green do not regress.

## Acceptance

- **Slice 1 (the gate):** the settled mechanism + `IsValid` guard are applied; the 11 previously-blocked
  TUs **run in the live editor without the crash**, pass/fail captured coherently per-result-run; no
  un-asked product change to auto-start. Resolution is genuine (no silenced/parallel-path harness).
- **Slice 2:** faculty-(A) emits a structured self-report enumerating the actually-exposed surface and
  reconciling it against the declared contract (surface-drift visible); the faculty-(B) exercise-and-report
  framework exists; `observe/observer.js` is refactored into the browse-owned in-engine home with the walk
  shared (not duplicated); `observe/README.md` + `observation.md` refs reconciled.
- **Slice 3:** each of the four integrations is **`checked>0` against the real primitive's own output** and
  reviewer-classified **genuinely-exercised** — real litehtml layout output (vs litehtml's own boxes),
  decode-for-size (real non-zero dimensions over real bytes), real observation geometry (real litehtml
  boxes), cyclic `JS_RunGC` reclaim (real reclaim of the node↔listener cycle). A passing-but-scripted
  result is **not** acceptance.

## Review Questions (open forks)

1. **Finding-#1 test-architecture mechanism — SETTLED at ratification (team-owned; no human fork).**
   browse-design's source-verified framing + browse-pm re-verification resolved this as a test-infra defect
   surfaced by a *correct* product guard: **auto-start is preserved** (not a product change); **(d) revisit
   auto-start is dropped** (no product decision; not invoked at "frame and start"); **(b) drive-the-shared-
   runtime is non-viable** (thread-affinity + the lifecycle/teardown tests need create+destroy of the
   singleton). The remediation direction is fixed: the **`IsValid` guard** (in regardless, defensive),
   **use-tests point at the real auto-started runtime**, and **lifecycle/GC tests use an injectable/
   resettable seam**. The one remaining choice — **(a) suppress auto-start under automation vs (c) a
   resettable-holder seam** — is the **team's "how," production auto-start unchanged either way**; the
   **developer's slice-1 audit picks it**. Gates nothing.
2. **Self-report schema** — the `observer.js` snapshot lineage + the capability section; the team's "how,"
   settled during the build (slice 2).
3. **`observe/` split extent** — how much of `observe.mjs`/`diff.mjs`/`parity.sh` stays Chromium-side
   (serving `observation.md` parity) vs what moves in-engine (the `observer.js` walk is the shared
   mechanism); the reconciliation boundary the refactor lands.

---

## Outcome

011 delivered, in order, across these commits:

- `5c251ba` — **Slice 1 [a]: finding-#1 test-infra unblock.** The 11 blocked JS-runtime TUs run real-path
  (the test holder drives the production runtime via a single-source teardown, never a parallel one);
  product auto-start + guard untouched.
- `96f730a` — **Link-time build-id.** The coherence marker reads CathodeRuntime's PE link-id instead of a
  per-TU `__TIME__`, so it advances on any relink of that module.
- `f5c4997` — **Slice 2 [b]: the runtime-verification faculty.** The pure verifier (faculty-A real
  introspection + the generic reconcile machinery, all axes dormant + faculty-B framework + the
  Cathode-own walk + the self-report) — Cathode's own observe-equivalent, authored fresh.
- `2bf7433` — **Cross-module aggregate build-id.** `coherence.buildId` is a live read of every loaded
  Cathode module's PE link-id at faculty time, so any module's relink advances the anchor — closing the
  cross-module coherence hole before [c]'s modifications.
- `e66ce1c` — **Slice 3 [c]: the four come-due discharges.** Each runtime come-due proof (`checked=0`
  since the scaffold era) driven against its real primitive's own output: cyclic `JS_RunGC` reclaim
  (measured finalizer-driven index drop), decode-for-size (real Blend2D decode → the PNG's own IHDR dims,
  garbage→0×0 control), real litehtml layout (litehtml first-light, real `get_placement` boxes on
  relational invariants), real observation geometry (the `GetBoundingClientRect` read path's composed rects,
  orphan-reads-false control).

## Carry-forwards (for next-sprint scoping)

- **`QueryComputedValue` → the CSS-declaration.** The per-property computed-style-string read stays stubbed
  — never one of the four geometry/decode/GC targets; it pairs with the CSS-declaration authoring (the
  litehtml typed→string mapping).
- **The CSS-declaration authoring task** — Cathode's own litehtml-grounded CSS capability declaration (in
  `Authoring/`), derived statically from the in-plugin litehtml source (`css_properties.h`/`types.h`/
  `style.cpp`), never panels'; subsumes the animation-fork + flex-gap design calls (tcdent's). Until it
  lands the faculty's CSS reconcile axis is dormant.
- **The install-components gap** — the global-assembly install seams (`OnInstall*`) are unwired, so the
  runtime exposes ~only quickjs built-ins; the contract's console/timers/DOM are declared but not installed.
  faculty-A quantifies the drift; building the install components is future scope.
- **The faculty production-assembly forward-seam** — when the install components land, the faculty test must
  run the production assembly via a shared extraction (share, don't fork).
- **The manifest-staleness cleanup** — `capability-manifest.toml` + the `.d.ts` "generated from manifest"
  comment are superseded 001-era scaffolding vs the interface-as-source model; retire/repoint them.
- **The Host Bridge sprint (012, green-lit + held)** — the DOM JS member bindings + the standard globals +
  the single-source generate-from-interface projection; faculty-A's DOM/globals reconcile completes on its
  descriptor (lockstep; the descriptor shape is calcified in `CathodeVerify/SCHEMA.md`).

## Postmortem

Role-scoped retrospectives, durable lessons for the next sprint.

### Lead retro (uptime.browse-lead)

- **A moving principle is a coordination hazard; route resolutions through one channel and calcify them.**
  The observe and CSS threads each whipsawed (modify-in-place → bring-in → self-containment convention →
  dissolve; CSS dormant → live → dormant) as tcdent's principle *evolved* mid-slice — and twice a stale
  disposition reached the dev before its reversal. What contained it: browse-pm's commitment that every
  resolution flows through the lead before the dev builds, and calcifying the truth on-disk (the descriptor
  shape in SCHEMA.md, the carry-forwards here) so nothing re-surfaced as "undefined." Surface evolving-
  principle churn fast; never let the dev build from a disposition that's one message from reversing.
- **Tree-verify before relaying a cross-area claim.** The CSS reversal turned on `panels/css-subset.json`
  existing — a relayed "it doesn't exist" (a Cathode-scoped audit miss) cost a whole dormant→live→dormant
  cycle. After that, I tree-verified the file myself before re-directing. Relayed facts about another dev's
  area are claims, not facts, until checked.
- **Hold the discharge bar; a stub is acceptable only when provably out-of-scope.** The arc's spine was the
  same rule at every gate: a green is a discharge only if it drives the real primitive against its own output
  (`checked>0`), and a deferral is legitimate only when it's structurally separable from the proof — never a
  vacuous half of it (the `QueryComputedValue`/T3 safeguard, confirmed at the source level, not by assertion).
- **Coherence is the foundation; harden the anchor before the work that needs it.** The per-TU stamp gap →
  link-time build-id → cross-module aggregate trajectory each surfaced one change later; sequencing the
  cross-module aggregate as [c]'s *first* step (before the modifications that evaporate the net-new shortcut)
  is why every [c] discharge had a real coherence discriminator.
- **Selective commits on a shared tree are explicit-path or nothing.** Every [c] commit landed against
  ~120 other-team WIP entries; staging the exact named files and verifying `--cached` count (never `git add .`)
  is the only safe path, and worth re-verifying the excluded set, not just the included.

### Developer retro (uptime.browse-dev)

- **A "deferral" in the ledger is usually a MISSING PRIMITIVE, not an untested one.** 3 of 4 [c] targets
  were implement-then-verify (build the real litehtml geometry read-out + the NodeId↔element handle; build a
  new Blend2D decode + the dep edge + a fixture); only the GC proof was verify-mostly. The audit-first
  per-target current-state classification (stubbed/absent vs just-untested) is what let each discharge be
  scoped as build+verify and risk-sequenced. **Inherit:** classify current-state per target before
  estimating; expect to build the primitive.
- **For a first-ever vendor compile, a header-grounded feasibility audit + an armed-but-unspent escalation
  de-risks without stalling.** The litehtml first-light audit cross-checked every call against the *vendored*
  headers (not assumptions) → all matched; the NodeId↔element crux resolved via one `data-node-id` handle; the
  feared TODO-PREBUILD drifts never materialized and the tripwire correctly held. **Inherit:** ground audits
  against the real vendored source; arm the escalation but don't pre-pay it.
- **Oracle-first, with an anti-fake negative control on every discharge.** Each proof asserted the primitive's
  *own* output, never a hand-typed expected — relational invariants (litehtml nesting/stacking/enclosure), the
  image's IHDR dims from the real decode + a non-image→0×0 control + a non-square transpose-catch, a measured
  live-node 1→0 reclaim across a real `JS_RunGC` (distinct from the 008 gc_mark *visibility*), an orphan node
  reading false. Define the oracle (what real output; what would fail if faked) before building.
- **A coherence anchor's blind spot only shows when a change exercises it — pre-empt the next slice's.** Per-TU
  `__TIME__` → link-time build-id → the cross-module aggregate: each gap was surfaced by the next change (the
  per-TU stamp went stale on a non-marker relink; the single-module link-id couldn't witness a sibling relink,
  and would evaporate once [c]'s edits became modifications). The aggregate was built as [c]-prep, before [c]
  could expose the hole. **Inherit:** the cross-module aggregate is the standing anchor.
- **Internalize-by-authoring; the build box carries the project only.** Internalizing another area's tool =
  author Cathode's *own* conformant version from its intention, leaving the source area (another dev's)
  untouched — never modify-in-place, never verbatim move/copy/repoint (tripped first on `observe/`, corrected to
  author-fresh). And the Windows build box carries the UE *project only* (no repo-root level), so repo-root
  paths (`panels/`, `observe/`) are unreachable at build *and* runtime → plugins must be self-contained, and
  internalize-by-authoring is the only portable answer (the decode uses an embedded byte-literal fixture,
  `panels/` untouched). **Inherit:** internalize-by-authoring + self-contained-plugin as standing conventions.
- **Process: one logical change per build (a revert IS a content change); dep visibility by use-site.** A revert
  rode into the step-3 relink demo and confounded it (CathodeRuntime relinked alongside CathodeLayout) — settle
  each change in its own build. And `CathodeLayout→CathodeRasterizer` must be PRIVATE (the decode is .cpp-only);
  a public dep over-propagated a ~40-TU CathodeDom rebuild, and the negative proof (`CathodeRaster.h` touch →
  CathodeDom 0 recompiles) validated the private fix. **Inherit:** public-header-use → public dep, .cpp-only →
  private; surface findings and hold for the ruling rather than over-commit to a still-forming framing (the CSS
  dormant→live→dormant churn rode tcdent's evolving principle; the box-oracle stayed stable throughout).

### Reviewer retro (uptime.browse-review)

- **The per-target oracle bar is the subtle half of `checked>0`.** "Drove the real primitive + green" is
  necessary, not sufficient — a real-primitive test is still hollow if it asserts a hand-computed expected (a
  tautology) or a constant. The hard, per-target work is naming *what* the oracle is and the trap: litehtml =
  relational/structural invariants of the engine's own boxes (a hand-typed pixel is the trap); decode = the
  image's intrinsic IHDR dims, non-zero necessary-not-sufficient + a garbage→0×0 control (a constant 7×3 fails
  the garbage case); GC = a measured finalizer-driven index drop across a real `JS_RunGC` distinct from gc_mark
  *visibility*, plus the leaks-before-GC necessary half (without it the reclaim is hollow — the cycle may never
  have leaked); observation = the read path's own composed rect + an orphan-reads-false control. **Pre-stating
  the oracle bar before the build (align-before-build) eliminated reject-on-method round-trips.**
- **A stub is legitimately out-of-scope only if the in-scope proof is structurally *incapable* of routing
  through it.** The `QueryComputedValue`/T3 safeguard was answered not by "does T3 happen to avoid it" but
  "*can* T3 be hollowed by it" — structurally: two separate methods, two vendor reads, the box field
  independent of the style field, separation holds by construction. Verify that, don't trust the test's current
  shape.
- **Re-anchor coherence whenever a change's blast radius exceeds the anchor's scope (per-TU < module < plugin).**
  per-TU `__TIME__` → link-time build-id → the cross-module aggregate, each blind spot exposed by the next
  change; plus the net-new-module deductive proof (an untracked module's found+passing test proves the new
  binary loaded — the no-prior-DLL analog). Escalate the hardening with the right (plugin-wide) framing *before*
  it bites; the aggregate is the standing per-run anchor (a repeated aggregate on a should-be-fresh run is
  reject-incoherent).
- **Verify coverage against the proofs; split a partially-discharged ledger entry.** On accepting a deferral,
  name the unblocker, re-tie it to its axis (the dormant CSS axis), and re-derive that no accepted proof
  secretly needs it — and when a bundled `COVERAGE` entry is partly discharged, split it (the box read → covered,
  only the style-string read deferred), or a discharged read left listed as deferred rots the ledger.
- **Discipline that held:** re-derive from source + the settled post-build run, never the dev report (caught the
  loose "settle = 4 C++ clean" framing, the rotated negative-control rerun, the public-vs-private gated-run gap);
  coherence *before* counts (every discharge gated on the aggregate segment advancing first); honest residuals
  named not buried; byte-level grounding when it mattered (decoding the PNG IHDR, the leaks-before/reclaimed-after
  contrast, the `CathodeRaster.h` diff = decl-only). **Next inherits:** the aggregate build-id as the standing
  anchor; the per-target oracle bar + verify-coverage safeguard as the standing discharge gate; the dormant CSS
  axis + `QueryComputedValue` as one style-fidelity track (a ledgered carry-forward, not a gap); the faculty's
  reconcile machinery built+dormant pending the 012 descriptor (activates with no reshape).
