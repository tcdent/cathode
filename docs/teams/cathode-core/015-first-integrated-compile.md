# Sprint 015 — Integrated Compile (011–014 Drift Verification)

The prerequisite gate for the cross-module wiring epic: **prove the 011–014 incremental drift compiles
and links cleanly across all 9 modules, and all 105 existing automation tests run green.** No new
features, no cross-module wiring, no stub implementation — only the integrated whole, proven coherent
and passing what it already has.

> Scope from the PM (tcdent-approved): the integrated-compile prerequisite before any cross-module
> wiring. **Premise corrected mid-formulation (tcdent):** the from-clean *wipe* framing was a smell —
> a from-clean integrated build already happened at **sprint 010** (commit `ae600fa`, "first clean
> build of all 8 modules"), so 011–014 is **incremental drift on top of an existing baseline**, not a
> never-before integration. The method is **surgical touch+regen of the drifted seam headers**, not a
> wipe.

## Scope Boundary

**This sprint owns** verifying that the cross-module drift accumulated over 011–014 (shared seam-header
changes) compiles and links cleanly across all 9 modules, and that all 105 existing tests pass on the
resulting build — by forcing the drift-affected TUs to recompile against current header content.

**Out of scope (named, owned by later epics):**
- **The cross-module wiring / integration phase** — the production drivers across the joints
  (DOM→Layout→Text→Raster→Surface→Runtime). 015 confirms the modules *link*; the wiring epic makes the
  frame lifecycle *run*.
- **Implementing the engine stubs** — `Evaluate`/`ExecutePendingJob` (CathodeRuntime), `DrawGlyphRun`
  (CathodeRasterizer) stay **stubs** (they already compile — empty bodies, vendor calls in comments).
- **The Texture Egress component**, the **author-gate tooling**, a **production IBundleSource** —
  absent components, their own later sprints.
- **No new features, no behavior changes** — a fix here is the minimal change to make existing code
  compile/link/pass.

## Problem

The 9 modules were last fully clean-built together at **sprint 010** (`ae600fa`, 8 modules — Cathode
Verify was added in 011 and has been incrementally rebuilt since, relinking in 013). 011–014 landed
**incremental** cross-module drift on top: shared seam-header changes whose dependent TUs UBT's
incremental dep-tracking recompiles *if* it sees the change. The risk 015 verifies is a **drift-stale
`.obj`** — a dependent TU whose `.obj` predates an 011–014 seam-header change UBT did not recompile.

**This is bounded, not open-ended (grounded — tcdent's correction):** after `regen` (a cold
re-enumeration of the full include graph), UBT compares every TU's deps to its `.obj` mtime, and the
interim cycle reported **"up to date"** — no dependency, including the seam headers, newer than its
dependent `.obj`, on a *fresh* scan. The only general way that is wrong is the shared-tree mtime-race
([[reference_shared_tree_mtime_race]]), and **no specific 011–014 event grounds it** (no
content-change-without-mtime-bump; `git --diff-filter=DR` over the seam headers is empty — none
deleted or renamed). So the integrated compile is **effectively already coherent per UBT**; 015 proves
the drift surgically rather than trusting that blind.

**The drifted cross-module seam headers (git-enumerated — a change to these can stale *another*
module's `.obj`):** `CathodeDom.h` (→ Verify), `CathodeBinding.h`/`CathodeBoundMember.h` (→ Dom/Verify),
`CathodeLayout.h`/`CathodeSelector.h` (→ Dom), `CathodeRaster.h` (→ Layout),
`CathodeJsRuntime.h`/`CathodeBuildId.h`/`CathodeRuntimeTesting.h`/`CathodeRuntimeModule.h`
(→ Binding/Dom/Verify), `CathodeResolver.h` (→ Layout). (`CathodeSurfaces.h` changed but Dom only
forward-declares it — no `#include`, no stale consumer; `CathodeText.h` changed but has no consumer —
touching it only recompiles CathodeText's own TUs.)

**Dependency graph (the link order):** Runtime/Text/Rasterizer/Surfaces/Resolver = leaves;
Binding→Runtime; Layout→Rasterizer,Resolver; Dom→Binding,Layout,Runtime,Surfaces; Verify→Binding,Dom,
Runtime. **CathodeText is a leaf with NO dependents** — nothing links it transitively (the Layout→Text
joint is unwired), so it must be built **explicitly** and is the module most likely to carry a stale
`.obj` (nothing forces its recompile).

## Goal

A build in which all 011–014 drift-affected TUs demonstrably recompile clean across all 9 modules, the
modules link, and all 105 existing tests run green — establishing the verified baseline the wiring epic
builds on. **No wipe, no destructive box delete** — touch+regen is within env's existing Makefile
surface.

## Final State

- The drifted seam headers are touched; `regen` + build **recompiles every includer** of those headers
  (the build log shows those TUs rebuilt, not "up to date"); the build succeeds (0 errors, 0
  promoted-warnings) and links all 9 modules.
- All 105 existing tests run green in one run against that build (the 4-part gate below), zero
  non-Success.
- The engine stubs remain stubs (bodies unchanged); no cross-module wiring added; no new feature. Each
  fix is a minimal, behavior-preserving correction.

## In Scope (ordered slices)

The work is partly **emergent** — drift surfaces only when the affected TUs recompile. Structure (RQ-1
resolved: discovery-then-fix-in-dep-order):

1. **Touch+regen drift discovery** — touch the git-enumerated drifted seam headers (above) → `regen`
   → build. Confirm the build log shows each touched header's **includers RECOMPILED** (not "up to
   date" — the recompile *is* the drift check). Enumerate any compile/link error by module and class
   (vendor-shape / shared-header drift / log-category / signature). Confirm the **denominator** (105
   across 11 prefixes) and each module's artifact built/linked (CathodeText explicit). No fixes — the
   inventory.
2. **Fix in dependency order (leaves → up)** — any drift surfaced is fixed **minimal +
   behavior-preserving**, lowest module first. A fix touching a shared header is **mechanical-only**
   (rename / backward-compatible widen / moved declaration) and runs every **includer** module's tests
   empirically ([[feedback_empirical_regression_shared_path]]); a non-mechanical change routes to the
   lead (RQ-2). (Likely empty — the drift is expected clean per UBT; this slice fires only if a
   recompile surfaces a real break.)
3. **Full-suite green** — all 105 tests green against the build (the 4-part gate). The **44 never run
   in 013/014** are the at-risk subset where a real (vs stale) failure can first surface — each
   classified, not silently fixed (RQ-3).

## Non-Goals

- No cross-module wiring / production drivers (the integration epic).
- No implementing the engine stubs — they compile as stubs (bodies unchanged).
- No Texture Egress, no gate tooling, no production IBundleSource.
- No refactor or feature under cover of a compile-fix — minimal, behavior-preserving only.
- **No wipe / no `make clean` / no destructive box delete** — the drift is verified surgically by
  touch+regen of the changed seam headers; a full wipe is over-broad and unnecessary (no grounded
  miss-mechanism).
- **Do not delete a stub or a seam to make it compile** — trace callers/tests first; a dead-looking
  seam may be a deliberate integration-deferred island (the 014 pipeline finding).
- No new cross-module Build.cs dep **for wiring** — a dep is allowed only to *link code that already
  existed pre-015* (the calls predate the dep); a dep with new call sites, or one wiring a deferred
  joint, is the wiring epic.

## Starting Surface

Audit first: the dev's 014 state audit + the dependency graph + the git-enumerated seam-header list.
The touch+regen discovery produces:

`touched header → its includer TUs recompiled? → compile/link result → error class (if any) → proposed
minimal fix`

Audit targets: the drifted seam headers (above); the never-recently-touched leaves
(Text/Rasterizer/Surfaces/Resolver/Binding); cross-module log-category references
([[reference_log_category_no_cross_module]]).

**Suite denominator (frozen, source-confirmed): 105 tests across 11 path-prefixes (9 modules).**
Cathode.Dom 46, Cathode.Css.Capability 3 (CathodeDom); Cathode.Layout 12; Cathode.RuntimeCore 12,
Cathode.Runtime 1 (CathodeRuntime); Cathode.Text 8; Cathode.Rasterizer 7; Cathode.Surfaces 6;
Cathode.Binding 6; Cathode.Verify 2; Cathode.Resolver 2. (014 confirmed 61 green = Dom 46 + Layout 12 +
Css.Capability 3; the other **44** are authored but never-run on this checkout — the at-risk subset.)

## Method

**Touch the git-enumerated drifted seam headers → `regen` (cold re-enumeration) → build.** This forces
every includer of a changed header to recompile against current header content (and defeats the
mtime-race — the source is now provably newer than the `.obj`), surfacing any drift surgically — a
recompile **failure** there is the real drift signal; **all-clean** proves the 011–014 drift coherent
without a wipe. Then run the 105 suite. A shared-seam fix is **mechanical-only** and runs every
**includer** module's tests empirically (source-dispositive include graph,
[[reference_firewall_source_dispositive]]). Hold the existing green tests' **outcomes** byte-for-byte.

## Acceptance

- **Drift recompiles clean:** the touched seam headers' **includers demonstrably recompile** (the
  build log shows those TUs rebuilt, not "up to date") and the build succeeds — all 9 modules compile +
  link, 0 errors, 0 promoted-warnings; each module's artifact present/fresh (CathodeText explicit).
- **All 105 green (the 4-part gate against the silently-dropped-suite trap, denominator frozen):**
  (a) `Found == 105`; (b) **each enumerated leaf-path present by NAME** in the run (a rename/swap that
  keeps the count is caught only by name); (c) **each of the 11 per-PREFIX counts == its enumerated
  count** (the native granularity — a single-prefix drop within a multi-prefix module, CathodeDom =
  Dom 46 + Css 3 / CathodeRuntime = RuntimeCore 12 + Runtime 1, is caught directly; a module that
  registered 0 tests is caught here); (d) **zero non-Success** across all 105. CathodeText (8) is the
  concrete orphan instance: explicit + present-by-name, never transitive.
- **Gate-stays-achievable escape (when RQ-3(c) fires):** if a never-run test surfaces a genuine
  out-of-scope design defect (routed to the PM, not fixable in 015), the lead routes it to the PM, who
  either (i) accepts a minimal behavior-preserving stopgap-to-green or (ii) signs off descoping that
  test (105 → 105−N, recorded). The PM owns the per-instance disposition; this only keeps the gate from
  indefinitely blocking on out-of-scope work.
- **Coherence (the touch+regen change-shape — a THREE-LEGGED stool, re-derived from "from-clean" once
  the wipe was conceded, [[reference_fresh_binary_marker_by_change_shape]]).** A wipe recompiles +
  links everything by brute force; the surgical touch recompiles only the touched headers' includers,
  so three legs together cover what the wipe covered by force — no single one suffices:
  - **The 3-part surgical marker (seam-drift + same-session binaries):** the build log shows the
    **touched seam headers' includers RECOMPILED** (not "up to date" — the affected TUs demonstrably
    rebuilt against current content, the surgical equivalent of a fresh build for the seam-drift set)
    **+** the CathodeVerify cross-module **aggregate buildId** coherent + same-session over its 8-module
    closure (Binding/Dom/Runtime/Layout/Surfaces/Rasterizer/Resolver) **+** **CathodeText proven fresh
    separately** (its TUs recompiled on its own header-touch — outside Verify's closure).
  - **Leg A — UBT incremental correctness (covers the vendor-shape category):** a drifted TU can
    include a **vendor** header (litehtml/quickjs/Blend2D), not a Cathode seam header — the seam-touch
    does NOT recompile it; UBT does, *if* it sees the dep change. The surgical method therefore *trusts
    UBT's dep scan for every non-touched TU*, and that trust is grounded, not assumed: (1) the interim
    build reports **"up to date"** (UBT recompiled every TU whose ANY dep — seam or vendor — changed; a
    stale `.obj` would show "not up to date"), (2) **no mtime-race event** ([[reference_shared_tree_mtime_race]]),
    (3) `git --diff-filter=DR` over the seam headers is **empty** (no deleted/renamed header — the case
    UBT's scan most often misses). These three are the explicit premise the vendor-category coverage
    rests on.
  - **Leg B — the integrated LINK succeeds (covers the log-category category):** a cross-module
    log-category reference ([[reference_log_category_no_cross_module]]) is an unresolved extern at
    **link**, not a compile error — no recompile-of-includers surfaces it (the referencing TU can be
    "up to date" yet the extern unresolved). The integrated build's LINK step catches it by
    construction: **all 9 modules link, 0 unresolved externals.**
  - **Named residual (the honest cost of conceding the wipe):** the surgical method's coherence is
    "UBT-correct + no-miss-grounded," NOT "everything was rebuilt." The residual a wipe would still
    catch = a UBT-scan miss on a mechanism *outside* the ruled-out set (a generated/untracked include,
    a clock skew beyond the mtime-race check). Small, and the known mechanisms are grounded above — but
    named so a future reader knows what the coherence rests on. mtime-order corroborates, not
    dispositive alone.
- **No scope leak (reject conditions):** the 3 stub bodies unchanged from baseline (markers intact); no
  new Build.cs dep for wiring (link-existing-only, calls predate the dep); no behavior change (the
  suites' outcomes unchanged); no delete-to-compile.

## Review Questions (open forks for the team + PM)

1. **Slice structure** — RESOLVED: discovery-then-fix-in-dep-order; the discovery is **touch+regen of
   the drifted seam headers** (not a wipe — tcdent's correction, the wipe was conceded as a smell).
2. **A shared-header fix** — must be **mechanical-only**; the reviewer gates it via the
   source-dispositive includer graph + empirical includer suites + same-outcome. A **non-mechanical**
   change → the lead routes it to the human.
3. **A pre-existing test surfaced RED by the recompile** (most likely in the 44 never-run subset) —
   **classified, not silently fixed**: (a) real defect (fix minimal + flag), (b) stale test (update
   minimal + flag), (c) genuine design defect (route to the PM via the lead — the gate-escape governs
   the stopgap-vs-descope outcome). The fix-vs-route disposition is the lead's/PM's.
4. **CathodeText (the orphan leaf)** — RESOLVED: built explicitly, its 8 tests in the 105, name-checked
   present, proven fresh separately (outside Verify's closure). The Layout→Text dep stays unwired.

## Outcome

015 **verified the 011–014 incremental drift compiles and links clean across all 9 modules, with all
105 existing tests green** — surgically, via touch+regen of the drifted seam headers, **not** a wipe.
The sprint's premise was corrected mid-formulation: a from-clean integrated build already existed at
**sprint 010** (`ae600fa`, 8 modules), so 015 was really *verify the drift*, not a first-ever
integration.

**Method (no commit of source — the close record is this doc):** touched the 12 git-enumerated drifted
cross-module seam headers → `regen` → build. The build recompiled the **82 of 98** TUs that include
those headers (0 errors), linked all 8 touched modules (CathodeSurfaces correctly not relinked — Dom
forward-declares it, no `#include`), and ran all 105 tests green. No source content changed (touch is
mtime-only); the engine stubs stayed stubs.

**The three-legged coherence held (the planned stool, not a strengthened one):**
- **Leg 1 (seam-drift):** the 82 touched-includers recompiled clean; CathodeText fresh separately;
  the CathodeVerify aggregate buildId coherent over its 8-module closure (Cathode.Verify 2/2).
- **Leg A (vendor category — TRUSTED):** the **16 skipped** non-touched TUs (vendor-only +
  CathodeSurfaces) trusted-correct on the grounding — interim post-regen up-to-date + no mtime-race +
  `git --diff-filter=DR` = 0 over `ae600fa..HEAD` (re-derived in-tree by review) — with the **residual
  named** (a UBT-miss outside those mechanisms), not claimed empirical.
- **Leg B (log-category):** the integrated link, 0 unresolved externals.

**Acceptance met (4-part green, frozen denominator):** Found 105; each leaf-path present by name; each
of the 11 per-prefix counts == enumerated (CathodeText 8 included); zero non-Success. Zero scope leak
(git status `game/Plugins/Cathode/` empty — mtime-only touch).

**Dissolved:** the from-clean tooling blocker — touch+regen is within env's existing Makefile surface,
so the `make clean`-vs-box-delete decision (which had routed to tcdent) became moot and was withdrawn.

## Postmortem

**Lead (uptime.browse-lead).** The load-bearing move was conceding a frame the human punctured rather
than defending it: I had assembled the from-clean *wipe* into the contract; tcdent called it a smell;
the right response was to route the question to ground the *premise* (not defend the wipe), and the dev
found the 010 baseline that dissolved it. A build-method justification is past-shaped — checkable in
git — so it gets verified, not held as a position. The harder discipline was at the close: I overclaimed
Leg A as *empirical* ("everything recompiled") from the dev's first imprecise "zero up-to-date lines,"
and passed that misframe to the reviewer — who initially repeated it. All three of us converged back to
the honest "16 trusted, residual named" once the dev's 82/16 count and the silent-skip mechanism
surfaced. The lesson for the seat: a lead's overclaim propagates (I seeded the reviewer's), so own and
retract it the instant the counts say so — an inflated coherence is the exact opposite of what conceding
the wipe was meant to buy. Process: form the sprint as a team (the dev/reviewer formulated slices and
acceptance), route the tooling/destructive decision to its owner (tcdent), and keep the doc current when
the *method* changes (from-clean → touch+regen), since that is a contract change, not devlog churn.

**Dev (uptime.browse-dev, Mundo).**
1. **Ground a build-method premise in git before accepting a blunt method.** The wipe rested on "the
   integrated compile never happened / a stale `.obj` UBT missed"; git punctured both (`ae600fa` = the
   010 baseline; `--diff-filter=DR` empty; no mtime-race event). A method's justification is a
   past-shaped, checkable claim — verify the premise (baseline, real drift window, deleted/renamed set)
   before executing.
2. **A surgical drift check beats a wipe** — touch the git-enumerated drifted *seam* headers + regen
   recompiles precisely their includers (a recompile fail = real drift), strictly more targeted *and*
   within env's existing Makefile surface (it dissolved the destructive/gated tooling blocker). The
   precise method is often also the one that sidesteps the gated operation.
3. **UBT silently skips up-to-date TUs — read the log by what it AFFIRMS.** "Zero 'up to date' lines"
   is not "everything recompiled"; UBT prints nothing for a skip. The freshness signal is the
   recompile-count vs total-TU-count (82/98 → 16 skipped), never a grep for an absence.
4. **A surgical substitute needs a multi-leg marker** — one grounded leg per drift category the method
   does not directly exercise (seam = recompiled / vendor = UBT-trusted-with-residual / log-category =
   integrated-link). The honest coherence is the multi-leg stool, not the one leg the method proves.

**Review (uptime.browse-review).** 015 was a negative/baseline proof with no behavior change, so the
durable lesson is proving a fresh *integrated* build's coherence when neither a new-test nor a
behavior-flip marker exists. Conceding the wipe for a surgical touch+regen buys speed at a real cost,
and the discipline is to pay it *honestly*: the surgical marker proves only the seam-drift category;
the vendor category rides UBT's incremental correctness grounded by no-miss, and log-category drift
surfaces only at the integrated link — so the coherence is a three-legged stool with a **named
residual**, not "everything rebuilt." The sharpest method catch — which the lead and I both initially
overclaimed and both corrected — is that UBT logs no per-TU "up to date," so a grep-empty is not a
freshness signal; the real signal is recompile-count vs total-TU-count (82/98 → 16 skipped-and-trusted),
and the skipped leg is trusted on its grounding, never empirical. I caught my own draft repeating the
misframe and re-derived the honest picture in-tree. Next reviewer inherits: for an integrated-whole
gate, name all three legs + the residual rather than claim a strengthened/empirical close; verify
freshness by recompile-count-vs-total, never by grepping for skips a build tool never logs; and retract
an overclaim toward the honest "trusted + residual" the moment the counts say so.
