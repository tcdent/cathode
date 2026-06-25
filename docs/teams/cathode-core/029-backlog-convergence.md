# Sprint 029 — Backlog Convergence

> **GREEN-LIT (2026-06-24, self, per PM delegation "green-light yourselves"; team-reviewed — dev FAITHFUL,
> reviewer PREMISE-INTACT, all folds incorporated).** ONE SPRINT, all ten remaining backlog items, built
> slice by slice, correctness/crash first (PM: "ONE SPRINT. All items. Build slice by slice, report at
> completion."). Order: #18 → #19 → #29 → #30 (early, de-risk the new integration) → #app→body (PM-added
> 2026-06-24: the 025 example screens mount to `#app`, contradicting the locked body-mount contract [027 RQ1]
> — correct them to `document.body`; applying what's ruled, not a new decision) → #28 → #4 → #24 → #25 → #27. **#20 DROPPED (2026-06-24, tcdent): production BYPASSES the buggy `bl_array_assign_data` path
> (`CathodeRasterBlend2D.cpp:66`, the external-data form never calls it) — the patched path is never hit, so
> no fix/proof is needed; the bypass is sufficient.** (9 active items.) Items: #20 (Blend2D heap-overflow fix) · #18 (author-JS
> unhandled-rejection → host Sink) · #19 (GC cycle reclamation proof) · #29 (author-gate CI) · #28 (game.*
> host bridge — the convention) · #4 (background-image rendering) · #24 (egress lock-free triple-buffer) ·
> #25 (GPU-readback banding) · #27 (multi-surface) · #30 (end-to-end authoring-pipeline demonstration).
> **#6 DROPPED** (stale — the status mirror was retired at 025 S3). **#26 KEPT** (the documented forward
> author-JS seam, reframed not removed — lead's PM-delegated call). ~16-24 slices (an epic, ruled one sprint).
> **#33 ADDED (2026-06-25, PM): the body{background-color} CONTAINER CRASH — fold the CathodeLayout
> root/canvas-background null-deref fix into 029 BEFORE the close commit (a real crash on gate-allowed CSS,
> surfaced at #4).** (10 build slices + #33.)

## Scope Boundary

This sprint converges the remaining Cathode backlog into one slice-by-slice effort across three bands:
**correctness** (the vendored-lib hardening #18/#19/#20, the CI #29), **capability** (the game.* bridge #28,
background-image #4, multi-surface #27, the egress optimization #24/#25), and **demonstration** (an end-to-end
authoring-pipeline proof #30). Each item is independent or locally-coupled; built one slice at a time,
correctness/crash first.

**Owns:**
- **#20** Blend2D `bl_array_assign_data` under-alloc heap-overflow fix (written patch applied via an
  idempotent `setup.ps1` `git apply` hook; proven by the crash→survive flip driving `assign_data` directly).
- **#18** author-JS unhandled-rejection → host Sink (`JS_SetHostPromiseRejectionTracker`, the `is_handled`
  tri-state, the caught-vs-unhandled discriminator).
- **#19** GC cycle reclamation proof (CathodeBinding — a synthetic safe-gc_mark'd cycle, reclaim-direction +
  leaks-without-release control).
- **#29** the author-gate CI (the repo's first `.github` workflow, PM-authorized — runs `npm run gate`, and
  it BITES in CI; Node author-gate only).
- **#28** the **game.* host bridge convention** (PM: "build the convention — stub a real-shaped game object +
  the binding mechanism that wires it into Cathode's JS env; the bridge works when a real object is handed to
  it; the sim wires in later"). Build the C++ bind mechanism + a stub real-shaped game object → project the
  `.d.ts` game.* surface **Model-A from the (stubbed) bridge** (FBoundMember-equivalent), NOT from the doc.
- **#4** background-image rendering (the composition `DrawImage` seam → resolver → Blend2D draw).
- **#24** the egress lock-free triple-buffer (an **optimization** — the HolderLock is already correct).
- **#25** the GPU-readback green-count banding (hardening; coupled after #24).
- **#27** multi-surface (the relayout seam fans to N surfaces; per-surface isolation).
- **#30** an end-to-end **authoring-pipeline demonstration** (PM: "show ANY app running on a screen — it
  doesn't need to be game content — demonstrate the authoring pipeline end to end"). Pick a simple app,
  author it through the 025 gate → the IIFE → the 028 worker/device → show it running on the device.

**Does NOT own (named, with the owner):**
- **#6** capability status-lockstep — DROPPED (PM-confirmed): the Present/CathodeComputed status it asserts
  was RETIRED at 025 S3; it would re-prove a retired mirror (the reject).
- **#26** LoadAuthorScript — KEPT (lead's PM-delegated call): the documented FORWARD production author-JS
  seam (`PROVENANCE.md:15` + `CathodeDomMountProofTests`), not vestigial dead code — reframed in the
  reconcile, not removed.
- **The real sim** (#28) — the bridge is built with a STUBBED real-shaped game object; the real sim→bridge
  wiring is a separate cross-team effort (the bridge works when the real object is handed to it).
- **Real game-screen content** (#30) — #30 demonstrates the pipeline with ANY app; real game screens are a
  later content/design effort (no screen-surface design exists yet — `docs/ui-surfaces/` absent).
- **The UE-automation CI** (#29) — the CI runs the Node author-gate only; the UE suite needs a self-hosted
  runner (a stock runner has no UE/MCP — `Makefile:269-272`) — a follow-on.

## Problem

Ten backlog gaps stand, each pre-scoped or PM-framed but unbuilt:
- **#20** browse 022 shipped on the BYPASS (`CathodeRasterBlend2D.cpp:66`) over the UNPATCHED Blend2D; the
  fix patch (`patches/blend2d-assign-data-underalloc.patch`) is written but not applied.
- **#18** `JS_SetHostPromiseRejectionTracker` is not installed (`quickjs.h:1134` decl only); an author's
  unhandled rejection is silent.
- **#19** cross-heap cycle reclamation is unproven (`CathodeBinding/COVERAGE.md:94`); the `JS_RunGC` that
  exists is a drain/steady-state context, not the reclaim direction.
- **#29** no `.github` exists repo-wide; the author gate (025) is unenforced in CI.
- **#28** the game.* host bridge is an EMPTY FUTURE STUB (`CathodeGlobalEnvironment.h:29 OnInstallGameBridge`,
  no `GetGameBoundMembersForCodegen`); game.* is omitted from the `.d.ts`. The convention (bind mechanism +
  a stub object) is unbuilt.
- **#4** the composition `DrawImage` seam falls through (`CathodeComposition.cpp:197 default: break`);
  background-image is in the allow-list (declared) but never drawn.
- **#24** the egress publish uses a HolderLock + deep-copy (correct, but contended); the lock-free buffer is
  unbuilt.
- **#25** the production GPU-readback uses `>0`; the green-count is unbanded.
- **#27** the relayout seam serves one surface (`CathodeCompositionOwner.cpp:149`); N-surface is unbuilt.
- **#30** no real app has been run through the full pipeline end-to-end (025 gated fixtures, 028 displayed a
  first-party DOM, but no authored app IIFE → device).

## Goal

> PM: **"ONE SPRINT. All items. Build slice by slice, report at completion."**
> PM on #28: **"Build the convention. The C++ game bridge hook doesn't need the real sim to exist — stub in a
> real-shaped game object and build the binding mechanism that wires it into Cathode's JS environment. The
> point is the bridge works when a real object is handed to it. The sim wires in later."**
> PM on #30: **"Show any app running on a screen. It doesn't need to be game content — it needs to demonstrate
> the authoring pipeline end to end. Pick something, author it, show it running on the device."**

Finishing: the ten items are each delivered and proven, slice by slice, with the correctness/crash items first.

## Final State

- **#20** — the patch git-tracked + applied via the idempotent `setup.ps1` hook; Blend2D rebuilt from patched
  source; the `assign_data` under-alloc path driven DIRECTLY survives (crash→survive flip, marker on the
  assign_data-survival); the BYPASS untouched.
- **#18** — `JS_SetHostPromiseRejectionTracker` installed (respecting `is_handled`); a genuinely-unhandled
  rejection reaches the Sink; a caught `.then` (sync- and later-caught) does not.
- **#19** — a synthetic safe-gc_mark'd cross-heap cycle reclaimed after the root release (finalizer-fired) +
  the leaks-without-release control fires.
- **#29** — a `.github` workflow runs `npm ci && npm run gate` (path-filtered on `Authoring/**`); the CI step
  goes RED on a banned authorship (the gate's `screens/banned/` bite reaches the CI exit code) — not
  clean-only.
- **#28** — a C++ bind mechanism wires a stub real-shaped game object into Cathode's JS environment
  (`game.*` reachable from author JS); the `.d.ts` projects game.* **Model-A from the stub's bound surface**
  (a `GetGameBoundMembersForCodegen` that IS the install set the bind mechanism wires into JS — so
  author-JS-reachability == the `.d.ts` game.* by construction; a reachable-but-un-projected binding REDs,
  the S1 circular-guard trap), regen-vs-committed coherent; the bridge works when a real object is handed to
  it (the stub is swappable).
- **#4** — `DrawImage` renders: an authored background-image is ACTUALLY DRAWN (ink in the image's box,
  blank in the margin), driven through the resolver → Blend2D, not declared-only.
- **#24** — the egress publishes via a lock-free buffer; the game-thread drain never observes a torn
  (half-written) frame (the consistent-snapshot invariant holds); framed as the optimization it is.
- **#25** — the GPU-readback green-count is banded (the per-band count matches expected); coupled after #24.
- **#27** — the relayout seam fans to N surfaces with per-surface isolation: surface A's content never
  appears on surface B's texture; each renders independently.
- **#30** — a simple STATIC authored app (any content, a "status card") runs end-to-end: authored
  `.ts`+`.css` → `npm run gate` → the IIFE → the 028 worker/device → the scene-capture binds the app's
  DISTINCT authored content at its layout-computed position (a no-op pipeline OR a 026-first-party-DOM
  re-run REDs); the IIFE-built DOM (driven by the AUTHORED JS) is the source.

## In Scope (ordered slices)

Order: **correctness/crash first** → the quick CI win → the **end-to-end demonstration (de-risk the one new
integration)** → the bridge → features/perf. #30 moves EARLY (the gate→IIFE→028-device path is the one
genuinely-unproven integration — prove it before the feature investment; modest de-risk, the features don't
depend on it). The dev splits an item into phases. Locally coupled: #24 → #25 (the readback observes #24's buffer).

- **Slice 1 — #20: Blend2D heap-overflow fix — DROPPED (2026-06-24, tcdent).** Production BYPASSES the buggy
  `bl_array_assign_data` path (`CathodeRasterBlend2D.cpp:66`, the external-data form never calls it) — the
  patched path is never hit, so no fix/proof is needed. The whole-box-crash RED (a 022-class crash, tcdent's
  reserved call) is moot; the bypass is sufficient. The written patch + the idempotent hook stay on the tree
  as an un-wired follow-on (apply if a future path ever calls `assign_data`).
- **Slice 2 — #18: unhandled-rejection → Sink** (~2). (a) install the tracker (the C-thunk + this-opaque
  seam) → `ICathodeDiagnosticSink::ReportError`, firing on the tracker's `is_handled` **bool** (the
  genuinely-unhandled, `is_handled=false`-never-handled) — distinct from `JS_ExecutePendingJob`'s tri-state
  return (`<0`/`0`/`1`), the channel NOT to read (a throwing-but-caught `.then` returns `1`/Executed). (b) the
  discriminating pair: a genuinely-unhandled rejection REACHES the Sink, a caught `.then` (sync- and
  later-caught) does NOT.
- **Slice 3 — #19: GC cycle reclamation** (~1-2). A synthetic safe-gc_mark'd JS↔native cycle (own wrapper,
  NOT the `document` model) + drop the JS handle + release the root + `JS_RunGC` → RECLAIMED (finalizer-fired
  preferred) + the leaks-without-release control. A survival test is vacuous.
- **Slice 4 — #29: author-gate CI** (~0.5-1). The repo's first `.github/workflows` job (setup-node →
  `npm ci && npm run gate`, path-filtered on `Authoring/**`). PROOF: the CI step goes RED on a banned
  authorship (`screens/banned/`) — not a clean-only CI. Node author-gate only (UE suite → self-hosted
  follow-on).
- **Slice 5 — #30: end-to-end authoring-pipeline demonstration** (~2-3, moved EARLY to de-risk the one new
  integration). Author a simple STATIC styled app (a "status card" — a styled `<div>` tree with a label +
  info rows, built in the `.ts`, rendered once; any content, NOT game) → `npm run gate` → the IIFE → the 028
  worker/device. PROOF: the 026 scene-capture binds the AUTHORED app's DISTINCT content — a specific
  element/text/color the app's OWN `.ts`/`.css` produces — at its LAYOUT-computed position
  (measurement-independent, the 023 observable-geometry shape); so a no-op pipeline (a fixed/default render,
  the IIFE never drove the DOM) OR a 026-first-party-DOM re-run (a C++-built DOM, not the authoring pipeline)
  REDs. The IIFE-built DOM (driven by the AUTHORED JS) is the source. GOTCHA (the worker idles after compose,
  no rAF/timer self-wake — `[[reference_cathode_worker_idle_needs_wake]]`): pick a STATIC render (renders
  once, reliable) — NOT a timer/clock app (renders once then freezes, the timer never fires); a click-driven
  update (an input event PostInbound wakes the worker) is the alternative if interactivity is wanted.
- **Slice 6 — #28: the game.* host bridge convention** (~3-4). Build the C++ bind mechanism (the
  FBoundMember-equivalent for game.*) wiring a STUB real-shaped game object into Cathode's JS env
  (`CathodeGlobalEnvironment` `OnInstallGameBridge` → a real install) + a `GetGameBoundMembersForCodegen` +
  the `.d.ts` game.* projection **Model-A from the stub's bound surface** (regen-vs-committed). LOAD-BEARING
  DISCRIMINATOR (the 025 S1 circular-guard lesson): `GetGameBoundMembersForCodegen` MUST be the SAME surface
  the bind mechanism INSTALLS into JS (the install set) — so author-JS-reachability == the `.d.ts` game.* BY
  CONSTRUCTION; the proof bites a reachable-but-un-projected binding (add a member to the stub's bound
  surface → it is BOTH reachable from author JS AND in the `.d.ts`, never one without the other — the
  cancelAnimationFrame-class miss). NOT a parallel hand-list/doc (Model-B, the reject). The stub is swappable
  (a different real-shaped object wires in unchanged — the "bridge works when a real object is handed to it"
  bite).
- **Slice 7 — #4: background-image rendering** (~2-3). Build the `DrawImage` composition seam
  (`CathodeComposition.cpp:197`) → the resolver → Blend2D draw. PROOF: the image is ACTUALLY DRAWN — ink in
  the image's box, blank in the margin (a relational property the real raster satisfies; a fixed-output stub
  doesn't discriminate), bound to the resolved image bytes.
- **Slice 8 — #24: egress lock-free triple-buffer** (~2-3). Replace the HolderLock publish with a lock-free
  triple-buffer. PROOF: the consistent-snapshot / no-torn-read invariant (the game-thread drain never
  observes a half-written frame) via the publish/acquire fence ORDERING — NOT a throughput benchmark (a
  stress race proves nothing). Framed as the optimization it is (the HolderLock is already correct).
- **Slice 9 — #25: GPU-readback banding** (~1-2, coupled after #24). Band the production readback green-count
  (like the scene-capture). PROOF: the banded per-band count matches expected; the readback observes #24's
  buffer.
- **Slice 10 — #27: multi-surface** (~3-5). Fan the relayout seam (`CathodeCompositionOwner.cpp:149`) to N +
  per-surface real layout + the egress surface-map. PROOF: N-surface ISOLATION — surface A's mutation never
  appears on surface B's texture; each renders independently (the load-bearing property, not "2 surfaces
  exist").
- **Slice 11 — #33: body background-color crash fix (PM-added 2026-06-25, before the close).** Fix the
  CathodeLayout null-deref on the root/canvas-background path: ordinary, GATE-ALLOWED CSS
  (`body { background-color: ... }`) NULL-derefs CathodeLayout's root/canvas-background path → the device
  crashes (EXCEPTION_ACCESS_VIOLATION reading 0x0; surfaced at #4 by the abandoned blue-body-fill). PROOF —
  the **crash→survive flip**: a `body { background-color }` render that CRASHED now SURVIVES and DRAWS the
  background (the box completes the run, 0 Fatal, + the bg-color renders at its expected location; a
  still-crashing build can't complete + emit the marker — the crash-fix freshness shape, NOT "up to date").
  The fix is a real null guard at the deref site (not a masking that skips the draw); bound to the
  gate-allowed CSS (an author writing valid `background-color` no longer crashes the device); no regression
  (the full suite).

## Non-Goals

- **Do not remove the Blend2D BYPASS** (`CathodeRasterBlend2D.cpp:66`) on the patch landing — trace first.
- **Do not remove #26 LoadAuthorScript** — it's the documented forward author-JS seam (kept, reframed).
- **#28 does NOT build the real sim** — a stub real-shaped game object; the real sim wires in later.
- **#30 does NOT author real game-screen content** — any app demonstrating the pipeline; real screens need a
  surface-design spec (absent), a later content effort.
- **#29 does NOT add UE-automation CI** — Node author-gate only (UE suite → self-hosted runner follow-on).
- **#24 is NOT a correctness fix** — the HolderLock is already correct; #24 is the contention optimization.
- **#6** (stale status-lockstep) — dropped, not built.
- Re-proving existing coverage (the drain `JS_RunGC` is not the cycle-reclaim; the 025 DOM `.d.ts` is not the
  game.* projection).

## Starting Surface (audit before editing)

- **#20:** `patches/blend2d-assign-data-underalloc.patch`; `setup.ps1` Cmd-Vendor (`:350` source-check,
  `:392-397` recipe, `~:401+` build loop); `CathodeRasterBlend2D.cpp:66` (bypass); `CathodeRasterizer/COVERAGE.md:33`.
- **#18:** `quickjs.h:1134`; `CathodeJsRuntime.cpp:45-48`, `:174` (thunk pattern); `CathodeDiagnostics.h:22`
  (`ReportError`); `CathodeRuntime/COVERAGE.md:56` (channel-separation).
- **#19:** `CathodeBinding/COVERAGE.md:94`; `CathodeBindingRealJsTests.cpp:155`; `CathodeDom/COVERAGE.md`
  (`document.gc_mark` STAYS REMOVED — #19 builds its OWN safe cycle).
- **#29:** no `.github` (the first); `game/Plugins/Cathode/Authoring/` (`npm run gate`, `screens/banned/`);
  `Makefile:269-272` (the UE suite needs MCP/Windows → not in CI).
- **#28:** `CathodeGlobalEnvironment.h:29` (`OnInstallGameBridge`, empty stub) + `:46`; `CathodeAuthoringTables.h`
  (4 DOM accessors, no game); `CathodeAuthoringDts.h:14` ("game.* omitted"); `docs/simulation/public-api.md`
  (the conceptual sim surface — a shape reference for the STUB object, NOT the projection source); the 025
  FBoundMember/codegen pattern (`CathodeAuthoringDts.cpp`).
- **#4:** `CathodeComposition.cpp:197` (the `default: break` seam); `CathodeLayoutEngine.cpp:163` (emits
  DrawImage); `CathodeRasterBlend2D.cpp:299` (decodes); the IResourceResolver (`:40-43, :66`).
- **#24/#25:** `CathodeEgress.cpp:253` (the HolderLock — already correct), `:149` (the readback / S5 milestone).
- **#27:** `CathodeCompositionOwner.cpp:149` (the relayout seam); the egress surface map; 028 S6 live layout.
- **#30:** the 025 `Authoring/` toolchain + `screens/` (the gate → IIFE); the 028 worker/device
  (`CathodeDeviceActor`, the surface-load flow); the 026 scene-capture hard-gate.

## Method

- **Per-item discriminator** — a green is acceptance only if a wrong impl REDs it: #20 the crash→survive flip
  driving `assign_data` directly (not the bypass); #18 the `is_handled` caught-vs-unhandled pair; #19 the
  reclaim-direction + control; #29 the CI bites (RED on a banned authorship); #28 the `.d.ts` matches the
  bound surface by construction + the stub is swappable; #4 ink-in-box/blank-margin; #24 the no-torn-read
  ORDERING (not a benchmark); #25 the banded count; #27 N-surface isolation (no bleed); #30 the app VISIBLY
  runs (the scene-capture).
- **No-drift carries** (#28): the game.* `.d.ts` projects FROM the bound surface (the stub bridge), never the
  doc — Model-A, like 025; a hand-list/doc projection is the retired Model-B anti-pattern (the reject).
- **Split channel** by toolchain: the C++/codegen + the box automation suite through env; the Node gate
  (#29's CI shape, #30's author build) dev-machine + the reviewer's own `npm run gate` re-run.
- **Freshness** by change shape; the box build carries the change (vendor rebuild for #20; the additive
  markers for the tests). One slice at a time; no regression to the full suite at each slice boundary.

## Acceptance

Per item (each at its slice boundary): the Final-State property proven with its discriminator (above); no
regression (the full Cathode suite + the prior baselines); the BYPASS / #26 seam / retired status untouched.
The sprint closes when all ten slices are accepted + committed, the scaffolding retired, and the role
retrospectives appended.

## Review Questions (resolved forks — recorded)

1. **The split — RESOLVED (PM): ONE SPRINT, all items.** Built slice by slice (correctness/crash first).
2. **#28 authority fork — RESOLVED (PM): build the convention, Model-A from a STUBBED bridge.** Not
   Model-B-from-the-doc; the bridge mechanism + a stub real-shaped game object; the sim wires in later.
3. **#30 — RESOLVED (PM): any app, end-to-end.** Demonstrate the pipeline with any content (not game
   content); real game screens are a later content effort.
4. **#26 — RESOLVED (lead, PM-delegated): KEEP** (the documented forward author-JS seam); reframe, not remove.
5. **#6 — RESOLVED (PM): DROP** (stale — the retired status mirror).

## Outcome

029 converged the remaining Cathode backlog in ONE sprint — eleven slices delivered (ten backlog items
+ the PM-folded `#app→body` correction and `#33` crash fix), each box-green, independently
reviewer-accepted from the real source + the box run, and scoped-committed:

| item | commit |
| --- | --- |
| #18 author-JS unhandled-rejection → host Sink | `0c016fe` |
| #19 GC cycle reclamation proof | `01741ec` |
| #29 author-gate CI (the repo's first `.github`) | `567aec8` |
| #30 end-to-end pipeline demo (the status-card) | `d85e0d2` |
| #app→body example-screen correction | `b6ee54f` |
| #28 game.* host bridge convention | `646e3aa` |
| #4 background-image rendering | `a2bf956` |
| #24+#25 egress lock-free triple-buffer + banded readback | `7d5f5df` |
| #27 PART 1 egress-readback isolation (the #30 root-cause fix) | `63332d5` |
| #27 PART 2 multi-surface fan | `29d8c99` |
| #33 body{background-color} container-crash fix | `25c3e3c` |

**Dropped / kept (per the rulings):** #20 (tcdent — production bypasses the buggy `bl_array_assign_data`
path; the written patch parked un-wired on the tree) and #6 (stale — the status mirror retired at 025 S3)
DROPPED; #26 (the documented forward author-JS seam) KEPT, reframed not removed.

**Follow-ons:** the egress-readback isolation was RESOLVED in #27 PART 1 (the #30 floor restored to 0.85,
the band-aid removed); #31 litehtml-spill WITHDRAWN as spurious (an over-lenient detector, disproven by
the data); #33 RESOLVED (PM-folded before the close). The one open item is #20's parked Blend2D RED-crash
(tcdent-gated). Full Cathode suite at close: **145/145, 0 Fatal.**

## Dev Retrospective (Mundo)

1. **Ground the founding before locking a multi-X premise.** I proposed N isolated JS contexts for the
   multi-surface fan; it CONTRADICTED the founding (one runtime, one global object, surfaces intentionally
   NOT isolated — `architecture.md:74-83`). The lead caught it against the doc; the reframe (one global +
   per-surface documents) was correct AND made the proof's discriminator the right one (both surfaces read
   the SHARED value, not isolated). The obvious isolation model can be the exact opposite of a deliberate
   shared-state design. INHERIT: multi-surface = one-runtime / one-global / per-surface-documents.
2. **A bright stray that varies across runs at fixed content is uninitialized-GPU residual — fix the
   source, not the floor.** #30's readback green-count was order-sensitive (256→498→198→633): CreateTransient
   with no clear + dirty-rect-only upload, so the un-uploaded margin read prior-test GPU memory. Wrong fix =
   widen the floor (band-aid); right fix = clear-on-create → deterministic-0, floor RESTORED to 0.85. A
   cross-run-varying bright stray (a texture/init bug) is a different phenomenon from faint
   bundle-independent detector noise (an instrument bug). INHERIT: egress textures clear on create.
3. **A non-idempotent per-call registration is latent under single-use; only multi-use exposes it.** Axis-2's
   first surface never re-composed: RegisterClass allocated a fresh quickjs class id per call,
   CreateDomDocument re-registers per document, so the 2nd document reassigned the process-global
   GDomNodeClassId and ORPHANED surface A's node wrappers → A's appendChild thunk failed the class-id check
   and threw (swallowed by Evaluate) → never marked dirty. FIX: idempotent-by-name per-runtime registry (NOT
   a process-global if-id-not-zero guard, which re-introduces the stale-across-runtimes crash). Captured in
   the class-id-per-runtime memory (now both mirror traps).
4. **Chasing the real property surfaced a bug a shortcut would have buried.** On axis-2's failure I leaned a
   build-time shared read (robust, proven path); the lead overrode to fix the real reactivity — the
   founding's DEFINING property is the reactivity, not just the sharing. The build-time read would have
   rendered the markers via the WORKING build path, hiding the broken post-mutation re-compose (the class-id
   bug) entirely. When a proof's mechanism breaks, fix the mechanism — routing around it defers an essential.
5. **Diagnose before patching a runtime-ordering bug — and check your own diagnostic's confounds.**
   Instrumented per-surface counters (eval vs emit) split 'didn't run' from 'ran but didn't re-compose'; a
   distinct-color readback ruled out a leak — but my OWN same-position markers occluded, so a
   distinct-POSITION readback removed the confound; then the source trace landed it. Bounded discriminating
   runs beat a guess, and your diagnostic can carry its own confound.
6. **Don't over-claim a concurrency opt past what the ordering guarantees.** For the egress map I first
   claimed no-structural-lock (frame-ordering serializes it); building it I caught a TMap rehash race — the
   worker's FindOrAdd of a NEW surface reallocs the buckets concurrent with the game thread's find of a
   DIFFERENT surface; ordering only orders the SAME key. Walked it back to a structure lock + thread-safe
   shared-ptr, per-surface publish/acquire still lock-free (#24 preserved).
7. **Gate-allowed input that crashes via a vendored lib's unguarded deref is a real defect — fix the value
   we control + render it, not a skip (#33).** `body { background-color }` crashed because WE passed
   litehtml's draw a nullptr clip and its root-bg branch derefs it unconditionally (the ordinary path is
   null-guarded; the root branch isn't). The vendored lib is headers + a prebuilt lib, so the fix is at OUR
   call site: pass the valid value the lib's contract expects (a non-null viewport clip) — which also DRAWS
   the bg, proving the defect is closed, not masked.

## Review Retrospective (uptime.browse-review)

Every slice was re-derived independently from the current source + the settled, post-build box run
(never the dev report), and the dev-machine/Node half was **re-run on my own channel** (`npm run gate`
for #29's CI bite, #30's gate-IIFE, the #app→body correction, the S4/S5 fixtures) — for evidence that
lives off the box, re-running the gate yourself is the strongest independence available.

**Where a verdict turned on the method, not the count:**
- **#28 — a structural single source beats a guard.** `GGameMembers` is one `const FBoundMember[]` read by
  BOTH the composer install AND the codegen, so install-set == codegen-set *by construction* — a
  reachable-but-un-projected binding is impossible (no second list to drift). This is strictly stronger
  than the 025 S1 names-array guard; the reject would have been a codegen reading a parallel hand-list or
  the doc (Model-B), which a green regen-vs-committed coherence cannot catch.
- **#4 — carry-not-rediscover is a reject even with the right pixels.** The layout resolves the image once
  (with the baseurl) and carries the bytes; composition decodes the *carried* bytes. The rejected fork (a)
  re-resolved in composition — it would have drawn the image correctly yet double-resolved and emptied the
  baseurl (a partial feature). A downstream re-derivation of a fact the boundary already holds is the
  defect, independent of the output.
- **#24 — prove concurrency by ordering, framed as the optimization.** The proof is the fence-pairing
  argument (the release/acquire pair == the FScopeLock's happens-before edge → torn frame impossible by
  construction) + a single-threaded slot-logic unit test — NOT a throughput/stress race (which proves
  nothing). And because the HolderLock was already correct, #24 is the contention *optimization* (the
  existing Egress tests pass unchanged → behavior preserved), never dressed as a correctness fix.
- **#30 — distinct content at the layout position, and root-cause over band-aid.** "Something rendered" is
  not the proof; the scene-capture binds the authored app's *distinct* color at its *layout-computed* box,
  and the concentration discriminator bites a no-op, a full-fill, AND a real spill (not a loosening). The
  razor-thin floor (0.802 vs 0.80) was traced — I confirmed #24 did not change #30's content (the in-box
  stayed 2560/DENSE; only the out-box rose), so the escalation was warranted — and #27 PART 1 fixed it at
  the root: the egress texture was reading *uninitialized GPU memory* in its margin (a real production
  bug), and clearing on create made the margin deterministic-0 and *restored* the floor to 0.85.
- **#27 — per-runtime scoping and a discriminator that bites the wrong model.** The first-surface
  re-compose failure was a foundational binding-layer bug (a per-call class id overwriting the
  process-global → orphaned wrappers → a swallowed throw). The gate turned on the cache being
  **per-registry** (a member, fresh per runtime), NOT a process-global `if(id!=0)return` — the latter
  passes a green test yet stays stale → collides/UAF on a new worker runtime. And the multi-surface proof
  is two-axis: render isolation (no cross-texture bleed) AND **shared-state reactivity** — both surfaces
  reflecting the host's shared `game.tickCount=100` is the discriminator, because an isolated model would
  read the stub's 42. A one-axis "A-never-on-B" proof would have asserted the *wrong* (isolated) frame.

**What I carry forward:**
- **Ground the founding doc for an architecture fork — don't propagate a handed frame.** I carried an
  "isolation" framing into #27 from the re-plan; `architecture.md:72-81` settles it the other way (one
  global object, surfaces *not* isolated — intentional). The dev re-grounded; I owned it. For any
  architecture question, the founding doc disposes, not the first frame you're given.
- **Retract the moment the data turns.** I affirmed a "litehtml text-spill" finding at #30; the corrected
  diagnosis (an over-lenient test green-detector miscounting faint bundle-independent noise) disproved it,
  and I retracted. Standing is in being right after checking, not in never being challenged.
- **A test instrument that flips a red to green is the suspicious case** — verify it's a grounded re-tune
  (the #30 detector tightening, the floor restore) with the load-bearing guard unchanged (DENSE), never a
  loosening that hides a failure.
- **No production-widen-for-test:** #27's host-settable bridge was injected through the existing #28 hook
  (the stub-swap precedent), not a new test-only door.
- **A crash-fix proves BOTH survive AND render — never just no-crash (#33).** A `body { background-color }`
  NULL-deref'd litehtml's root/canvas-background path (a real crash on gate-allowed CSS — the
  gate-vs-device gap surfaced at #4). The gate turned on two halves: the crash→survive flip is the
  *freshness* (the box completing the run + the additive marker emitting proves the fixed binary ran, where
  a still-crashing build faults and can't emit — not "up to date"), AND the bg-render half must be green (a
  blue FillRect with *positive area*) — a skip-the-draw "fix" also survives but leaves the bg unrendered, so
  no-crash alone is a masking reject. (A real valid-clip null-handling at our call site, not a skip.)

## Lead Retrospective (uptime.browse-lead)

1. **Check the founding architecture before treating a fork as the human's.** #27's multi-surface arrived
   framed as "DOM isolation," the dev offering N isolated contexts as a "major architecture call."
   `architecture.md:72-81` settled it the other way — one global object, surfaces intentionally NOT isolated.
   I redirected the dev to the founding rather than escalating; a fork the design-of-record already settles
   is not the human's call. The interim single-surface code made a settled architecture look like an open
   question — the founding disposed it, and the corrected premise made the proof's discriminator the right
   one.
2. **Don't defer an essential and ratify the deferral.** On #27 axis-2's first-surface bug the dev leaned a
   build-time shared read (faster, proven path) and to flag the re-render as a follow-on. I overrode to build
   the real reactivity — the founding's DEFINING property *is* the reactivity, not the sharing; the shortcut
   would have silently renamed "shared-state reactivity" to "shared-state read." Chasing the real property
   surfaced a latent foundational class-id bug the shortcut would have buried. The defer-and-ratify pattern
   is the failure mode; build the essential (with a bounded leash for the depth risk).
3. **A flagged precision check earns its friction.** On the egress-map sync I flagged a concurrent read+add
   on the Frames TMap as a possible rehash race; the dev's "no structural lock" claim didn't hold (a real
   UB on a concurrent add of a different key), and the dev corrected to the ruled design (structure lock +
   thread-safe shared-ptr). Arbitration at the finest grain caught a UB before it shipped.
4. **Pin the mechanism before ruling a fork.** On #28's bridge placement my "fill the OnInstallGameBridge
   seam" instruction was a phantom assumption; the dev pinned the actual mechanism (`document`, the exact
   analog, installs via the composer, the seam empty/vestigial). I conceded and ruled to follow the real
   mechanism. A fork argued against an assumed implementation is a phantom — confirm what the code does first.
5. **A band-aid that saturates is the signal to fix the root — escalate the root, not another widen.** The
   #30 contamination was surfaced (a false litehtml-spill alarm I held pending the box numbers, then
   withdrew), stop-gapped (the floor 0.85→0.80 at #4), and escalated when it saturated (0.802 at #24+#25). I
   escalated the root-cause isolation as urgent rather than widening again; the PM folded it into #27, which
   fixed it at the source and restored 0.85. Don't keep widening a band-aid that's run out of room.
6. **Scope the late add, don't let it drift the close.** The PM folded #33 in before the close; I added it as
   a contract slice (the crash→survive-flip proof, the freshness shape, the no-mask bar), routed it as its
   own gated slice, and held both retrospectives until it committed so #33's lesson could fold in — rather
   than closing around it.
