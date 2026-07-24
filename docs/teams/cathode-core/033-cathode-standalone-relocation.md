# Sprint 033 — Cathode Standalone Relocation & Deferral Roadmap

**Team:** cathode-core (adopting and continuing the Cathode lineage built as `browse/001–032`) · **Lead:** cathode.core-lead · **Status:** DRAFT — pending PM green-light
**Date:** 2026-07-23
**Source:** `~/Work/uptime/game/Plugins/Cathode` → **Destination:** `~/Work/cathode` (standalone repo)

> PM/human direction (2026-07-23, revised): this sprint delivers *"a well-structured, clean repo with a grounded roadmap"* — **copy + relocate**, a **clean repository pattern**, and a **documented plan for each deferred item** (vendor/build approach, standalone test-harness design, integration research). *"Working buildability and integration proof belong in later sprints."*

This supersedes the earlier working-library scope (`browse/033`, removed): buildability and uptime-integration are pulled to later sprints; this sprint relocates cleanly and lays down a grounded, honest roadmap for what remains.

---

## North Star (carry into all planning)

> Human, 2026-07-23: *"This library will eventually be fully standalone — a full executable game that demonstrates its functionality. No dependency on uptime for anything: not for builds, not for tests, not for content."*

Every deferred item is framed toward that end state, even when this sprint does not reach it. The dependency on uptime is **temporary scaffolding to be retired**, in three arenas: **build** (its own orchestration, no `setup.ps1`), **test** (its own execution harness, no MCP-into-uptime-editor), and **content** (self-provided screen material/bundles/fonts, culminating in a standalone demo game). This sprint takes the first step — a clean, self-contained tree — and names the rest as grounded backlog.

## Scope Boundary

**This sprint owns:**
1. **Copy + relocate** the existing Cathode codebase into `~/Work/cathode` (with the sprint lineage and design-of-record docs).
2. **A clean repository pattern** — nested layout, clean-slate history, MIT license, correct ignore/keep semantics, every uptime-relative path re-rooted, third-party attribution in place.
3. **A documented deferral roadmap** — each deferred item (vendor/build approach, standalone test-harness **design**, integration research) gets a grounded statement of the problem and what must be resolved, filed as backlog that feeds the next sprints.

**Explicitly NOT owned (later sprints, named):**
- **Working buildability** (the vendor/build implementation) → a build sprint; *this sprint documents the approach question, does not build.*
- **uptime-integration** → the **next research sprint** investigates how uptime integrates against the standalone library.
- **Test-harness implementation** → a later sprint; *this sprint designs the standalone approach, implementation may extend.*
- **Self-provided content / demo game** → later, toward the North Star.

**Numbering & folder:** continues the adopted lineage as **033**, under this team's own folder `docs/teams/cathode-core/` (the legacy `browse` name is not carried — per the human, name it for cathode-core going forward).

## Problem

The Cathode plugin at `~/Work/uptime/game/Plugins/Cathode` is code-decoupled from its host (grep `Uptime` over tracked source = **6 hits, all of them comments**, five being assertions that no coupling exists — "ZERO Uptime*", "names NO host (Uptime*) type", "no UPTIME coupling" — and the sixth a historical note at `CathodeSurfaces.h:66`; **no `#include`, no host module dependency**: every `*.Build.cs` name resolves to a UE engine module, an intra-Cathode module, or a ThirdParty lib), but it is embedded, not standalone:

- It has **no repository of its own** — no clean-slate history, no top-level layout, no license.
- **Uptime-relative paths** reach out of the plugin: `install.sh:39-41` stages to the uptime repo root; the author-gate CI workflow sits at the uptime root hardcoding `game/Plugins/Cathode/Authoring/**`; `README.md:11` and several `COVERAGE.md`/`PROVENANCE.md` link `../../../docs/browse/*`.
- **No license apparatus**: no `LICENSE` tracked despite `README.md:10` asserting MIT; no aggregated `THIRD_PARTY_NOTICES` (only the Liberation font fixture's license is tracked). The 5 vendored libs are permissive & compatible (pins verified byte-exact vs source: quickjs-ng MIT, litehtml BSD-3, harfbuzz Old-MIT, blend2d + asmjit Zlib).
- **Three hard questions are unresolved and cannot be answered by relocation alone** — how the vendored libs build in-repo (orchestration is external + host-bound + Win64-only; the blend2d security patch is applied by nothing, `browse/029:66`), how the library runs its own tests without uptime's MCP harness, and how uptime integrates against the extracted copy. These are the deferral roadmap.
- The **Cathode sprint lineage** (`browse/001–032`, 30 tracked + 027, 032 untracked) and **design-of-record** (`docs/browse/*`, 27) would be lost without an explicit carry.

## Goal

`~/Work/cathode` is a clean, self-contained standalone repository holding the relocated Cathode codebase under a sensible layout, MIT-licensed, with third-party attribution, no path reaching into uptime, the sprint lineage and design docs carried in — **and** a grounded, honest backlog that states exactly what each deferred arena (build, test, integration, content) must resolve on the path to the North Star. A stranger can read the tree, understand the design, and see the roadmap; nothing is claimed to build or test yet.

## Final State

1. `~/Work/cathode` — standalone git repo, **clean-slate history**, **nested** layout (top level reserved for future project files: a host/demo project, top-level docs, CI), top-level **MIT `LICENSE`**.
2. The plugin tree relocated with tracked-file parity (incl. the 2 untracked-but-wanted carries); ignore/keep semantics preserved for `ThirdParty` and `Authoring/node_modules`+`dist`.
3. Every uptime-relative path re-rooted; `grep` shows zero surviving uptime-tree references.
4. `THIRD_PARTY_NOTICES` documents attribution for the intended vendored set (5 libs) + the 2 tracked third-party test assets (Preact — upstream 10.29.2 LICENSE fetched in, none is tracked today; Liberation Sans — already tracked), blend2d's local patch disclosed with altered-source marking. (Attribution, not gated on a build.)
5. Sprint lineage `cathode-core/001–032` (the carried `browse` history) and `docs/browse/*` design-of-record carried in; this doc is **033**; new sprints continue here.
6. Author-gate CI (Node-only: tsc + css-tree + esbuild — no UE) runs green in the new repo.
7. **Deferral roadmap filed**: three grounded backlog items — vendor/build approach, standalone test-harness design, integration research — each with a problem statement, resolution criteria, and its tie to the North Star. The test-harness item carries an actual **design**, not just a placeholder.

## In Scope (ordered slices)

- **Slice 1 — Repo init & clean pattern.** Initialize `~/Work/cathode` (clean-slate, nested layout); establish the repository skeleton (top-level `LICENSE` MIT stub, `README` placeholder, `.gitignore`/`.gitkeep` semantics for `ThirdParty` + `Authoring` build outputs, directory layout leaving room for a future host/demo project and top-level CI). *Method:* structure only; no source edits.
- **Slice 2 — Multi-tree copy & re-root.** Copy/relocate from **four trees — enumerate each so none drops silently**: (i) **plugin subtree** — `git ls-files` = 265 **plus** 2 untracked-but-wanted (the whole `patches/` dir and `CathodeRasterizerVendorPatchTests.cpp`), explicit carry/drop per untracked item; (ii) **`docs/browse/*`** — 27 design-of-record, of which `architecture.md`, `vendor-lib-build.md` and the ~18 `components*.md` carry by default; of the 7 non-component docs the ruling is **constrained by the link graph, not free** — **4 must carry** because carried files reference them: `spec-surface` (load-bearing "three-axis contract", referenced by carried component docs; 6 inbound), `observation` (6 inbound, incl. the byte-identical source comment `CathodeVerifyModule.h:14`), `testing` (3), `surface-load-document-wiring` (2, incl. the byte-identical source comment `CathodeDomMountProofTests.cpp:24`) — and **3 may stay** (zero inbound: `css-epic-contract`, `feedback`, `scale-demo`); (iii) **lineage `browse/001–032`** (30 tracked + 027, 032 untracked) → carried into `docs/teams/cathode-core/` as the adopted lineage (content unchanged; provenance preserved in each doc's own header); (iv) root **`.github/workflows/cathode-author-gate.yml`** → top-level CI in the new repo. Re-root every uptime-relative path: the CI workflow path + working-directory; the `../../../docs/browse/*` links (**must land inside the carried `docs/browse` or they dangle**); `install.sh`'s staging path — kept, and re-rooted to the **repo-root `vendor/`** (`SCRIPT_DIR/..` from `Cathode/install.sh`), *not* plugin-local. Two reasons: the existing root-anchored `/vendor/` ignore rule already covers it, and staging inside `Cathode/` would put 5 full upstream clones (each retaining its own `.git`) in the directory consumers drop into their `Plugins/`. Its build step is deferred, but the script travels as the pin-of-record. *Method:* diff `git ls-files` **and** the on-disk tree per carry; grep-prove zero residual uptime-tree paths; a host-agnosticism read on carried `docs/browse/*`; module/`*.Build.cs` files byte-identical (all use `ModuleDirectory`-relative ThirdParty paths — none needs editing).
- **Slice 3 — License apparatus.** Top-level MIT `LICENSE`; author `THIRD_PARTY_NOTICES` (5 vendored notices read from local source + Preact + Liberation Sans; sweep harfbuzz subdir COPYING; disclose the blend2d patch with altered-source marking; fetch the upstream Preact 10.29.2 LICENSE in). *Method:* read each notice from source, cite each; do not assert a license from memory. (Documentation/attribution — no build.)
- **Slice 4 — Deferral roadmap (the documented plan).** Produce three grounded backlog items (filed via agent-tracker, each with problem + resolution criteria + North-Star tie), and hold the hard-won analysis so it is not re-walked:
  - **4a Vendor/build approach.** Problem: `ThirdParty/{include,Win64}` are gitignored build outputs the `Build.cs` files actively link; the only orchestration (`setup.ps1 Cmd-Vendor`) is external, host-bound, Win64-only; the blend2d security patch is applied by nothing. Options to weigh (port a plugin-local re-rooted build vs LFS-prebuilt binaries vs hybrid). Preserve the design notes — **grounding for the build sprint, not settled facts to trust blindly**: **patch idempotency** (`install.sh`'s `git checkout` reverts the patch each run → re-apply post-checkout, idempotent via a `git apply --check` guard, `-p1` from `vendor/lib/cathode/` — *to validate*, inferred from the patch's `a/blend2d/core/array.cpp` diff header); the **`qjs.dll` soname** requirement (not `quickjs.dll`, or runtime error 126 — *to validate in the build sprint; currently untraced*); the **deterministic patch proof** (the `CathodeRasterizerVendorPatchTests` discriminator false-greens on an unpatched lib because it leans on undefined behavior — a ~512KiB overflow into mapped-adjacent heap can avoid crashing *and* still round-trip byte-exact; **asan/PageHeap is the deterministic separator** — a `BLArray` capacity/`impl_size` assertion is tempting but has a wrinkle: `size()` is set to N regardless and capacity is not readable before `assign_data`'s internal overflow has already run), pinned to the real `CathodeAggregateBuildId` signature (`CathodeBuildId.cpp:94`); and a **doc-vs-code gap to record** so the implementer does not trust it: the test's own comment claims `setup.ps1` has a git-apply hook — it does **not** (`Cmd-Vendor:316-434` has none), so "applied by nothing" is the accurate statement. North-Star tie: build with **no uptime dependency** (no `setup.ps1`).
  - **4b Standalone test-harness DESIGN.** Per the human, test execution is part of being standalone and must **not** rely on uptime's tooling. This sprint delivers an actual *design* (not a stub): the repo runs its own UE automation suite via a **headless-editor automation runner** (`UnrealEditor-Cmd <minimal-host>.uproject -ExecCmds="Automation RunTests Cathode." -unattended -nosplash`) invoked from the repo — needing only UE + a minimal host + the built plugin; no MCP, no live-editor socket, no `uptime.testing` (uptime's `make test` is MCP-into-editor, `Makefile:269-272`). Three constraints the design must state: **(1) hard dependency on 4a** — the suite cannot *execute* until the plugin compiles (the roadmap order respects this); **(2) north-star content guard** — some tests (composition/example proof) need `/Game/Cathode` **host content**, and populating the minimal host by copying uptime's `Content/Cathode` would silently re-import the uptime *content* dependency (a north-star violation), so the design **must partition the suite** into plugin-self-contained tests (e.g. the font fixture via `IPluginManager` — runnable early) vs content-dependent tests (blocked on the self-provided-content arena), or "runs its own suite" overclaims; **(3) scope precision** — this is headless-*editor* automation (the tests are EditorContext-flagged), **distinct** from the North Star's demo game (which is the content-arena deliverable) — do not conflate the two. Plus an explicit statement of what implementation remains. North-Star tie: **no MCP-into-uptime-editor, and no borrowed uptime content**.
  - **4c Integration research framing.** Hand-off to the next research sprint: what it must resolve — the interim mechanism for uptime to integrate against the standalone copy (submodule/symlink/path), **and** the eventual end state where uptime is not the consumer at all (the standalone demo game). North-Star tie: **temporary integration, retiring toward full standalone**.
- **Slice 5 — Clean-repo verification & hygiene.** Non-distributable-content sweep on the relocated tree (independently re-derived by the reviewer); structure/parity sanity; author-gate CI green in the new repo. *Method:* the one executable signal available this sprint is the Node author gate (no UE) — prove it green.

## Non-Goals

- **No working build.** This sprint does not produce `ThirdParty/{include,Win64}`, does not apply the blend2d patch, does not compile the plugin. Those are the vendor/build sprint. The tree links libs that a later sprint populates; that is expected, not a defect.
- **No uptime-integration and no test-harness implementation.** Integration is the next research sprint; the test harness is designed here, built later.
- **No behavior change / no refactor / no README rewrite.** Relocation is byte-identical for module code; the 4 stale README sections are flagged for a later polish sprint. Two cosmetic host strings stay (later scrub): the `FUptimeSurfaceHandshakeAdapter` *comment* at `CathodeSurfaces.h:66` and `Cathode.uplugin:8` `"CreatedBy": "Uptime"`.
- **Do not delete a fallback because production does not hit it — trace callers and tests first** (the blend2d patch, the Example modules, the `CathodeVerify` runtime faculty all carry).

## Starting Surface

Audit first, before editing:
1. **Cross-tree carry inventory** — the 4 trees above, each enumerated (plugin 265+2; `docs/browse` 27; lineage 001–032; root CI workflow 1).
2. **Path audit** — every uptime-relative path: `install.sh:39-41`, `.github/workflows/cathode-author-gate.yml:14-27`, `README.md:11`, `COVERAGE.md` + `Tests/JS/PROVENANCE.md` doc links. (`setup.ps1` is uptime-side and is *not* carried; it informs the 4a build dossier.)
3. **License-notice inventory** — 5 vendored roots (read directly), Preact `PROVENANCE.md` (no license text present), Liberation `LICENSE-LiberationSans.txt`, harfbuzz subdir COPYING sweep, the blend2d patch.
4. **Deferral-source audit** — the current `setup.ps1 Cmd-Vendor` recipes, the `Build.cs` link expectations, the automation-test runner (`make test` → MCP), and the integration touchpoints — as grounding for the Slice 4 dossier (analysis, not implementation).

## Method

Behavior-preserving relocation: **move, don't edit source.** The only authored content is the repository skeleton, the license apparatus, and the deferral dossier. Prove coherence by (a) `git ls-files` **and** on-disk parity per carry with deltas named, (b) grep-proof of zero surviving uptime-tree paths, (c) the Node author-gate CI green in the new repo (the one executable signal this sprint, requiring no UE). Module/`Build.cs` files stay byte-identical. The deferral items are **grounded** — every problem statement cites source; no hand-waving — so the backlog is a real anchor for the next sprints, not a wish list.

## Acceptance

1. `~/Work/cathode` is a standalone git repo (clean-slate history, nested layout, MIT `LICENSE`); tracked-file parity vs source (265 + 2 carried untracked; parity list attached, deltas named).
2. All four carries present and enumerated: plugin subtree, `docs/browse/*` (27, with the **4-must-carry / 3-may-stay** ruling applied), lineage `cathode-core/001–032`, top-level author-gate CI. **grep-proof of zero dangling `docs/browse` references from ANY carried file — doc links AND source comments** (not merely the architecture doc; two referrers are byte-identical plugin source comments, so the pointer cannot be edited out — the target must carry).
3. `grep` shows zero surviving uptime-tree paths across the tree.
4. `THIRD_PARTY_NOTICES` present, covering all 7 components, blend2d altered-source-marked + patch disclosed, Preact LICENSE fetched in; every notice traceable to a cited source.
5. Author-gate CI green in the new repo (`npm ci && npm run gate`).
6. Non-distributable-content sweep clean on the relocated tree (independently re-derived by the reviewer); module/`Build.cs` files byte-identical to source (diff proof).
7. Three deferral backlog items filed (vendor/build, test-harness design, integration research), each grounded in cited source with resolution criteria and a North-Star tie. "Concrete design, not a placeholder" is made **checkable** by holding 4b to its named elements: a headless-editor automation runner, a minimal host project, repo-invoked (not uptime's MCP), the suite **partitioned** into plugin-self-contained vs content-dependent, and an explicit "what implementation remains."

## Review Questions (for PM / human)

- **Q-License-scope — confirm license apparatus belongs in this pulled-back sprint.** I kept MIT `LICENSE` + `THIRD_PARTY_NOTICES` as part of the "clean repository pattern" (pure attribution, no build). Flag if you'd rather defer the notices with the vendor/build item. *(scope confirm; lean: keep — it's hygiene, not build)*
- **Q-Backlog-mechanism — where deferred items live.** I plan to file them as agent-tracker backlog tickets (grounded, resolution criteria) with the dossier in-repo. Confirm the tracker is the intended backlog surface. *(process confirm)*
- **Q-Roadmap-order — sequencing of the deferred sprints.** The natural order toward the North Star is integration research (next, per your ruling) → vendor/build → test-harness implementation → self-provided content/demo game. You own sequencing; I state it only so the backlog ties are coherent. *(future-shaped — human sequences)*

---

*Revised by cathode.core-lead per the human's revised rulings (Q-Integration → research sprint; Q-TestHarness → design this sprint, not uptime tooling; Q-Lineage-folder → cathode-core's own; Q-Scope → copy + pattern + deferral roadmap) and the North Star (full standalone: no uptime for build, test, or content). Grounded in the developer's extraction inventory and the reviewer's independent OSS-blocker audit, and preserving the hardened patch-proof/build-signature analysis in the 4a dossier so it is not re-walked. Hardened through a third team review pass (all 2026-07-23): the `docs/browse` ruling corrected from "free per-doc" to a link-graph-constrained **4-must-carry / 3-may-stay** with a widened anti-dangle gate, asan named as the deterministic patch separator, and the 4b **suite partition** added as a north-star content guard. Pending PM green-light.*

---

# Postmortem

**Sprint closed 2026-07-24.** Acceptance #1–#5 and #7 **MET**; #6 **MET, qualified** (the pre-public scrub inventory is extended and accurate but not complete — the lineage stratum is deliberately unswept pending the ruled publication policy, tracked as `CATHODE-5`).

Final state: `Cathode/` 268 tracked-equivalent · `docs/browse/` 26 · `docs/teams/cathode-core/` 33 (001–033 continuous) · `docs/roadmap.md` + `docs/design/standalone-test-harness.md` · CI workflow 1 · MIT `LICENSE` + `THIRD_PARTY_NOTICES` (7 components) · author gate **green** · 212 module files + 13 `*.Build.cs` byte-identical to source · uptime source unmodified.

Backlog filed in the confirmed order: `CATHODE-1` integration research · `CATHODE-2` vendor/build · `CATHODE-3` test-harness implementation (correction comment `b747fa35`) · `CATHODE-4` self-provided content + demo game · `CATHODE-5` pre-public hygiene scrub (correction comment `7b62010b`).

**Human rulings recorded:** MIT licence, holder Travis Dent; clean-slate history; nested layout; scope pulled back from working-library to copy + repo pattern + deferral roadmap; integration → its own research sprint; test harness designed here, implemented later, and never inherited from uptime; lineage kept as-is, scrub gated before any public remote, driver being **reader clarity, not confidentiality**.

## Lead Retrospective — cathode.core-lead

**What I tried.** Turn moving direction into an anchored written contract, route one narrow slice to one owner, hold the builder/checker separation absolutely, and arbitrate every report against the stated gates rather than against whether the work looked good.

**What evidence changed the plan.** The plugin was already host-agnostic by deliberate design, which reframed the whole effort from *decoupling* to *packaging and provenance*. The human then reshaped scope three times — minimal tree → working buildable library → copy + pattern + roadmap. Each time I dropped the prior frame rather than defending it, including my own five-sprint roadmap and my own no-compile boundary.

**What failed — mine.**

1. **I set the wrong carry criterion.** I framed `docs/browse` as "4-must-carry / 3-may-stay" on inbound counts. Both the criterion and the numbers were wrong. The dev's control case — `components-plugin-shell-build`, zero-inbound in every universe yet core design-of-record — showed inbound-count can never justify a carry *or* a drop; it is only a **one-way veto on drops**. I adopted a checker's figure without asking what universe it was measured over.
2. **I shipped a filtered count as a raw measurement.** `033:36` asserted "grep `Uptime` over tracked source = 1 comment"; the raw count is six. I was enforcing exactly this discipline on both teammates while carrying the error in my own contract. Corrected at close-out.
3. **I wrote a defect into the contract.** "`install.sh` re-rooted plugin-local" would have staged five full upstream clones, each retaining its own `.git`, inside the directory consumers copy into their `Plugins/` — unignored. The reviewer caught it at the Slice 1 boundary, before the copy ran.
4. **I identified a pattern and then failed to apply it one file over.** I caught the "do not distribute" caution as expired scaffolding and explicitly ordered its removal — and missed the README licence section asserting the same three expired facts. The reviewer found it.
5. **I routed review at summaries before evidence.** The reviewer read the contract three times but the test sources only after the partition criterion was drafted. That is why the mixed-file discovery came from the builder rather than the checker. Mine to fix.

**What worked, and why.**

- **No lateral dev↔review contact.** Both of the reviewer's retractions and the dev's correction of me landed on the record, independently verifiable, instead of being settled in a side conversation. The latency cost was real and worth paying.
- **ACCEPT-with-required-fixes**, but only under the reviewer's own test: *artifact correct and description wrong* takes a fix; *artifact wrong* takes a reject. Five slices, zero rejections, every substantive defect caught in flight. Without that test the shape is just a way to avoid the discomfort of rejecting.
- **Refusing "test green" as proof.** Two independent lines converged on the blend2d patch: the test cannot run standalone, and even when run its discriminator rests on undefined behaviour. A passing marker would have certified an unpatched, vulnerable library.
- **Parameterising around blockers** rather than stalling — the copyright holder and the absent tracker project each landed in a gap instead of on the critical path.
- **Escalating the genuine forks** (licence identity, git history, root layout, vendor/build story, lineage publication) instead of deciding them. Every one returned a ruling I would not have predicted.

**What the next sprint should inherit.**

- **Adopt both denominator conventions; they are complementary.** Enumerate the strata once up front, then compute every figure against that fixed universe (dev — fixes derivation); ship every count with its universe inline and name the single command whose output *is* the denominator (reviewer — fixes reporting).
- **Route review at the evidence before the document that summarises it.**
- **Agree the enumerating command before independent sweeps**, so later disagreements are real discrepancies rather than scope mismatches.
- **Get figures right before they cross into an immutable surface.** Ticket bodies cannot be edited; comments are the only correction channel.
- **Deferred items are documented, not decided.** 4b was the single sanctioned exception, on an explicit human ruling.
- **Mid-sprint contract edits are for factual corrections that would otherwise cause a defect** — two were made (the vendor staging path, the filtered grep count). Not for status churn. The plan is not a devlog.
- **Nothing in this sprint was ever built.** Every build-coherence discipline written into the Method is untested in anger.

## Dev Retrospective — cathode.core-dev

**What I tried.** Executed the five slices as scoped: repo skeleton, four-tree relocation with per-carry parity proofs, the licence apparatus, the deferral dossier, and the executable verification. The through-line was **prove it from the artifact, not from the plan** — set-diff the carried tree rather than count it, `git check-ignore` the real generated files rather than assume the rules bind, `cmp` the reproduced licence against its source rather than eyeball it, run the gate rather than assert the toolchain survived.

**What evidence changed the plan.**

- **Two untracked-but-wanted files (Slice 2).** The parity baseline was `git ls-files`, but `patches/blend2d-assign-data-underalloc.patch` and `CathodeRasterizerVendorPatchTests.cpp` were untracked on disk. A git-list-driven copy would have silently dropped both — including the patch that Slice 3's altered-source disclosure then had to cite.
- **The `css-epic-contract.md` link graph.** Framed as one of three "zero-inbound" candidates; measured over the universe we actually carry it had six inbound references from lineage docs ruled content-unchanged, so holding it back would have left a permanent dangle unfixable by editing the referrer.
- **`CathodeCompositionTickTests.cpp` (Slice 4).** A real file mixing both fixture mechanisms moved per-case partitioning from a design note to a design decision. Following the lead's correction further showed grep-by-path fails in *both* directions — under-classification being the dangerous one, because a case misclassified self-contained just fails and creates exactly the pressure to copy uptime's content in.
- **The harfbuzz `ms-use` chain (Slice 3).** The sweep only justified a notice once the causal chain was traced into the compiled `harfbuzz.lib`. Without the chain it is a formality; with it, a shipped obligation.

**What failed.**

1. **The narrow-denominator pattern — six instances.** Every time the enumeration was correct and the denominator was measured over a narrower universe than the one that shipped. **This is the dangerous shape** — a correct enumeration makes a wrong denominator look verified, so the error survives review of the substance. **Two fixes failed before one worked.** "Check the denominator" was attentional and did not survive contact. "Derive the denominator first" still ran per-row, on whichever row I was editing — which is exactly why instance five happened *after* I wrote the scope directive: **a scope directive does not self-apply.** What worked: **enumerate the strata once, up front, then compute every figure against that one fixed universe.** That version caught the `panels` row and the filtered `033:36` figure without being told to look.
2. **Instance six landed somewhere immutable.** `CATHODE-5`'s body shipped stale `of 24` denominators; corrected via comment `7b62010b`, the sanctioned channel. **Immutable surfaces amplify the pattern** — get the figure right *before* it crosses into one. Root cause: appending a correction to the body rather than fixing its original bullet — the same failure one layer down.
3. **Three self-status falsehoods.** The README licence section, the "do not distribute until the holder is set" caution, and the roadmap's "each **is filed**". Each was true when written and expired without the text moving. The caution was worst: I authored it *as* a guard for a placeholder, then retired the placeholder and left the guard. **A deliverable that asserts a status about itself acquires a maintenance obligation the moment it is written.**
4. **Two overstatements corrected by review.** Claiming the OFL was "reproduced verbatim … none from memory" when it had been rewrapped and one character substituted; and writing the `capacity()/impl_size` caveat as a disposal when it was only a narrowing.

**What the next sprint should inherit.**

- **The ignore-rule caveat, exactly as stated.** Only the **two `Authoring` lines** in the root `.gitignore` are redundant (the deeper plugin-local file wins). `/Cathode/Source/ThirdParty/*/{Win64,include}/` and `/vendor/` are **root-only and load-bearing**. A naive "delete the redundant root rules" cleanup would silently un-ignore the vendored payload and the build outputs.
- **`agent-tracker auth init --force`.** Any agent that attempted tracker work before the project existed holds a PAT, and plain `init` short-circuits on it *before* granting membership.
- **`docs/roadmap.md` is authoritative on all scrub figures.** Tickets are deliberately thin pointers; where a body and the roadmap disagree, the roadmap is correct.
- **Four figures in the 4a grounding are explicitly unverified** (patch idempotency; `qjs.dll` soname UNTRACED; the deterministic patch proof; the doc-vs-code `git apply` hook gap). Treat them as leads, not results.
- **The gate proves less than it looks like it proves** — nothing about the C++ build, the 143 automation cases, or any vendored library, and it ran on node 24 while CI pins node 20.

> **Lead note (2026-07-24):** the dev's inheritance list flagged `033:36`'s filtered grep count as "not my document to fix." It was corrected during close-out — that line now states six hits, all comments, five of them assertions that no coupling exists. Item closed, not inherited.

## Review Retrospective — cathode.core-review

**What I tried.** Re-derive every claim from source, never from the report, and prefer exhaustive checks over samples wherever cost allowed: set-equality over the carry manifests rather than comparing counts; running all 267/268 files through `check-ignore` and `cmp` rather than spot-checking; running `npm ci && npm run gate` myself rather than accepting a reported exit code; reading `CathodeRasterizerVendorPatchTests.cpp` rather than trusting that a patch proof exists; and tracing the harfbuzz `ms-use` chain through the **CMake** path that actually builds `harfbuzz.lib` rather than the `meson.build` entry cited to me.

**What evidence changed the plan.** Reading the patch test turned "green ⇒ patched" into a **UB false-green**. Grepping the `docs/browse` link graph turned a free per-doc carry ruling into a link-graph-constrained one, because two referrers are byte-identical source comments that cannot be de-pointed. The dev's mixed-file finding turned the partition unit **per-case**, and the two counterexamples turned the criterion **behavioural** — both verified from source and corroborated against the content tree (`slice3.css` present, `bg.png` absent).

**What failed — mine. Two retractions, one root cause.**

1. **The `css-epic-contract` inbound count.** I swept plugin + `docs/browse` and **omitted the lineage**, already a designated carry. Two of my three "zero inbound" figures were wrong (0→6, 0→1). Only `feedback.md` was genuinely zero — and by luck that was the one dropped. Applied strictly, my numbers would have stranded six references.
2. **The MCP class.** I matched `WIN_MCP_HOST` but not bare `MCP`, saw two hits, dismissed them as "env-var names", and never enumerated. I presented an incomplete inventory as the finding.

Both are the same error: **I scoped a sweep to a subset of the publishing set and reported the result as though it were the total.**

**The denominator pattern.** Not arithmetic. In almost every instance the *numerator* was right and the **denominator — the universe actually swept — was silently narrower than the claim.** It recurs because a grep's scope is invisible in its output: the scope lives in the command, the claim lives in prose, and nothing binds them. It is worst coming from review, because a wrong number from the person whose job is checking *looks* authoritative and propagates — which is literally what happened when my `24` reached a ticket body. Two conventions fix it, both already in-tree: **every count ships with its universe inline**, and **any inventory names the single command whose output *is* the denominator** (`git add -An .` = the publishing set).

**ACCEPT-with-required-fixes vs a binary verdict.** It served better, for a precise reason: nearly every late defect was in the **documentation of correct work**, not the work. Rejecting Slice 3 over a stale three-line README paragraph would have discarded a mechanically-verified 438-line NOTICES. The test I applied was *is the artifact correct and its description wrong, or is the artifact wrong?* I never used the shape to wave through a method defect; without that test it is just a way to avoid the discomfort of rejecting.

**Routing of independent re-derivation — what I would change.** (1) I was routed to the **summaries before the evidence**; route review at test sources, link graphs and build paths *before* the document that summarises them. (2) **Agree the universe up front** so later disagreements are real discrepancies, not scope mismatches. (3) **Keep the strict no-lateral-contact rule** — every correction, including both of mine, landed on the record and was independently verifiable. (4) **Folding fix-deltas into the next slice's pass worked**, avoiding a review-of-a-review spiral without letting any delta go unchecked.

**What the next sprint should inherit.** The 4a grounding notes **with their Status lines intact** — they are leads, not results. The behavioural partition criterion and its two counterexamples — do not re-derive them, do not replace them with a grep. The build-coherence bar: **no green is trusted without a signature pinning the run to the binary that produced it** — nothing in 033 was ever built, so this is unexercised. The count-with-universe convention.

> **Lead note (2026-07-24):** the reviewer's open residual — `CATHODE-3`'s body instructing "PARTITION all 66 tests" — was closed before sprint end via correction comment `b747fa35`, carrying the per-case unit, the 66-files/143-cases figures, the behavioural criterion, both wrong grep methods with counterexamples, and the authority hierarchy. Item closed, not inherited.
