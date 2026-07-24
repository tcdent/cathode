# Sprint 034 — Linux-First Workflow & UE Plugin Distribution Research

**Team:** cathode-core · **Lead:** cathode.core-lead · **Status:** DRAFT (rev 4, team-reviewed) — pending PM green-light
**Date:** 2026-07-24 · **Backlog:** `CATHODE-1` (rescoped)
**Repo:** `~/Work/cathode` @ `7277616`

> PM/human direction (2026-07-24, revision): *"uptime uses their fork independently — this team does not wire Cathode into uptime."* The sprint researches **(1)** whether *"Cathode development and testing can operate entirely on Linux, keeping Windows reserved only for game compilation and final demo … is a CI-ready test harness running on Linux feasible?"* and **(2)** *"the landscape for distributing an open source UE plugin"* — the human knows package management from **pip** and **cargo** and asks *"whether UE has anything comparable."*

---

## Green-light rulings (2026-07-24) — ratified scope

The PM/human green-lit the sprint with four rulings, recorded here as ratified scope so the contract does not lag them (it did — an earlier revision left these in the PM channel only, and independent review flagged the resulting contradiction):

- **Q-Linux-probe — DEFERRED (option c).** No runnable Linux UE 5.7 engine exists on this host — verified (nothing on `PATH`, no editor binary anywhere, uptime's `vendor/ue-source` is partial source with no `Binaries`; uptime's only usable engine is the Windows install, which cannot validate Linux CI). The probe is deferred, not attempted: proceed **documentation-grade**, every Linux-*behaviour* claim stays DOCUMENTED/ASSUMED, and **do not attempt engine acquisition**. The human is setting up Windows for parallel dev work, which may open future probe opportunities.
- **Test execution — DEFERRED.** Run **no** tests this sprint. The research delivers the landscape and recommendation; execution waits for the Windows setup.
- **Q-Linux-scope-change — APPROVED.** The sprint may recommend rescoping `CATHODE-2` to produce Linux libraries and `CATHODE-3` to a three-axis partition; name both explicitly.
- **Q-Demo-platform — RULED: WINDOWS-ONLY** (final, 2026-07-24). *"Windows is for game compilation and the demo. Linux is to be explored later."* The earlier green-light answer of 'Linux' was **reversed once the consequence surfaced**: a Linux demo would have put the Egress **GPU/Vulkan** path on the critical path, and the human, seeing that cost, confirmed the original framing. **So the GPU-must-work-on-Linux consequence does NOT apply** — the 8 GPU-touching cases remain a CI *exclusion*, not a must-work item. Linux-first is therefore about **development and CI testing**, with Windows reserved for compilation and the demo; the 10-case CPU beachhead is the clean Stage-1 target.
- **Q-Gating-change — a later sprint.** Recommend the source change; do not make it.
- **Human's Q1 question, answered plainly first:** *"How extensive is that? Build flags?"* — Slice 1 verdict: **deeper than flags** (the flags are the last 10%, not the task).

## North Star (inherited, unchanged)

Cathode becomes fully standalone — a complete executable game demonstrating the library, with **no uptime dependency for builds, tests, or content**.

## Closed: the uptime integration question

Recorded so the line is visibly resolved rather than silently dropped. **uptime continues using its in-tree fork independently; this team does not wire Cathode into uptime.** Cathode instead offers clear, ergonomic integration paths and uptime eventually consumes it *as any other consumer would*.

Consequences: the Samba-transport research is no longer the point of this sprint (correct work, retired question). **Divergence control is moot and `Q-Authority` is resolved** — the copies are now *intentionally* forked, so drift is the expected state; `~/Work/cathode` is authoritative for Cathode. The 033 byte-identical baseline is now historical provenance, not a live invariant.

## Established findings (inputs, not open research)

**Platform gating — the shorthand "all 13" is wrong and the correction is load-bearing:**
- **Only 7 of 13 modules carry a Win64 gate** (`CathodeBinding`, `Dom`, `Layout`, `Rasterizer`, `Runtime`, `Text`, `Verify`). Six carry none. It is true that *every ThirdParty block* is Win64-gated — but "all 13 modules are gated" would erase the beachhead below.
- **A vendor-free transitive closure exists: 3 modules** — `CathodeResolver` (no deps), `CathodeSurfaces` (no deps), `CathodeEgress` (→ Surfaces only) — carrying **5 test files / 15 of the 143 cases**.
- **It splits again on RHI.** `CathodeEgressTests.cpp` does real GPU readback (`ENQUEUE_RENDER_COMMAND` + `RHICmdList.ReadSurfaceData` + `FlushRenderingCommands`), so of those 15, **Egress 5 are GPU-bound** and **Resolver 4 + Surfaces 6 = 10 cases have zero GPU/RHI references**. The headless-CI-safe, vendor-free set is **10 of 143 (7%)**.
- **The beachhead is not free today, for two removable reasons:** (i) vendor include paths are added **unconditionally** — the `PublicSystemIncludePaths.Add` calls **precede** the Win64 gate, so on Linux those directories exist but are empty while the `.cpp` files include the headers anyway. This is a **compile** failure, not a link failure. (ii) `Cathode.uplugin` declares all 13 modules with `Name`/`Type`/`LoadingPhase` only — **no `PlatformAllowList`/`PlatformDenyList` anywhere** — so enabling the plugin builds all 13 and the 3 clean modules are not independently loadable.

**Test suite:**
- **66 test files / 143 automation cases**, all `IMPLEMENT_SIMPLE_AUTOMATION_TEST` (universe: git-tracked `*Tests.cpp` under `Cathode/Source`).
- **`EditorContext` is uniform — 143 of 143** carry `EditorContext|EngineFilter`. There is **no non-editor subset to escape to**; Linux CI must provide an *editor*, not merely an engine.

**Other Q1 constraints:** quickjs staging is Windows-shaped (`CathodeRuntime.Build.cs:45` hardcodes `qjs.dll` inside the Win64 gate — Linux needs a `.so` **and** its own staging, which "build five Linux libraries" does not cover). AsmJit is a JIT, so W^X restrictions in hardened containers are a containerised-CI risk *(ASSUMED)*. Engine acquisition is likely the item that actually decides "CI-ready."

**Existing pattern and starting position:**
- `game/setup.ps1:6-7` documents uptime as authored on the Linux/Mac side with *"the Windows box only COMPILES"* — the human's model **extends an existing pattern**.
- **Half the verification story is already Linux-native:** the Node author gate ran green on this Linux host in 033 Slice 5, and the committed workflow is `runs-on: ubuntu-latest`. The Node half is done; the UE half is the open question.

**Evidence ceiling — state this before anyone reads a grade:** there is **no UE engine on this Linux host** (nothing at `/opt/UnrealEngine`, nothing on `PATH`, nothing engine-shaped on disk). Therefore **every Linux-UE *behaviour* claim in this sprint is capped at DOCUMENTED or ASSUMED and cannot reach OBSERVED**, unless engine acquisition enters scope (see `Q-Linux-probe`). The grades are not improvable by effort alone.

## Scope Boundary

**This sprint owns:** two research questions, each ending in a **graded recommendation** —
1. **Linux-first development and testing** — can development and testing run entirely on Linux, with Windows reserved for game compilation and the final demo? Is a CI-ready Linux test harness feasible, and what would it take?
2. **UE plugin distribution** — is there anything comparable to pip/cargo, what are the conventional patterns, what is most ergonomic for a consuming game developer, and how are versioning and upgrades handled?

Plus: **file the API-surface / developer-ergonomics backlog signal** the human flagged as a priority for the next couple of sprints.

**This sprint does NOT own:** uptime integration (closed) · implementing anything · the vendored-library build (`CATHODE-2`) · the harness implementation (`CATHODE-3`) · content/demo game (`CATHODE-4`) · the pre-public scrub (`CATHODE-5`) · the API-surface review itself (filed, not performed) · **the platform-gating source change** (recommended, not made).

## Problem

- **Q1.** Cathode's suite has never run anywhere; the only designed harness targets a headless *editor*; the vendored libraries build for Win64 only; and no UE engine exists on this host. Whether Linux-first development plus Linux CI is feasible — and at what cost, in what order — is unknown.
- **Q2.** Cathode is intended for open-source release with *"a straightforward, well-understood integration path,"* but no distribution decision exists. UE's conventions differ substantially from the pip/cargo model the human reasons from, and the landscape moved recently. This choice shapes every consumer's first experience of the library.

## Goal

Two grounded, evidence-graded answers the human can act on — with a **staged** verdict for Q1 where a staged answer is the accurate one, and a plain answer to Q2's actual question before any menu of options. Every claim traceable, graded, and impossible to mistake for verified when it is reasoned.

## Method — the evidence is mostly EXTERNAL, which changes the discipline

033 was verifiable entirely from the tree; this sprint is not. The 033 lesson generalises as **"make the invisible property visible and gate it"** — not "reuse the same convention." The invisible properties here are *where a claim came from* and *what population it describes*.

1. **Never assert UE ecosystem facts from model memory.** This domain moved recently and is exactly where stale-but-confident answers appear.
2. **Two dates, not one:** every external citation carries **publication date AND date-accessed**. A 2019 forum post and a 2026 Epic doc are both "cited with a date" and are not equal.
3. **`DOCUMENTED` is defined mechanically:** a **primary source** (Epic documentation, engine source, the vendor's own docs) stating the claim **directly**. Blogs and forum posts *corroborate*; they do not promote a grade. Grades are self-assigned, and the drift to guard is ASSUMED → DOCUMENTED.
4. **Quote the supporting sentence, do not merely link it.** The one drift with no in-repo analogue is **model memory laundered into a citation** — recall a fact, find a plausible URL, grade it DOCUMENTED. A quote is checkable; a bare URL is an assertion about a URL.
5. **Every prevalence claim names its population.** "What the community does," "the conventional pattern," "most plugins ship X" are population claims with no stated population — the denominator failure in external clothing. Name which plugins, how found, how many examined, or grade it **ASSUMED**. *(Gated, not advised — this team has already demonstrated it is prone to this.)*
6. **Separate "what UE supports" from "what people actually do."** Both matter; they are different claims.
7. **An uncitable claim becomes a QUESTION, not a graded assertion** — the shape the 033 deferral roadmap already proved.
8. **Phrase Q2 for a GENERIC consumer.** Worked examples naming uptime would quietly re-open a closed question.
9. **Count-with-universe** still applies to any in-repo figure.

## In Scope (ordered slices)

- **Slice 1 — Q1 constraints and the three-axis partition.** Establish what Linux UE automation testing requires, and partition the 143 cases on **three axes: vendored-library dependency × RHI/GPU dependency × content dependency**. "N cases can run in Linux CI" is meaningless without all three. Cost the beachhead explicitly: *15 cases across 3 modules could run on Linux if the descriptor gains platform gating and the include paths are gated* — present it as a real option to be **judged, not hidden**. *Note this reaches backwards:* `CATHODE-3`'s designed partition has only **one** axis (content); better learned now than at implementation.
- **Slice 2 — Q1 CI landscape.** What can actually run UE tests in CI: hosted runners (which carry no engine), self-hosted runners, containerised engines, engine acquisition and licensing — and what the UE community does in practice *(prevalence claims per Method 5)*. Then what Cathode specifically needs.
- **Slice 3 — Q1 recommendation, STAGED.** Do not force a binary verdict where a staged one is accurate. Assess at minimum: **Stage 1** — prove the harness end-to-end on the ~10 vendor-free, GPU-free cases (needs the gating change and a Linux editor, **not** `CATHODE-2`), de-risking `CATHODE-3` early; **Stage 2** — the full suite (needs `CATHODE-2`, the `.so` staging port, and excludes GPU-readback cases absent a real RHI). State what necessarily stays on Windows, and flag the `CATHODE-2`/`CATHODE-3` rescopes explicitly.
- **Slice 4 — Q2 distribution landscape.** Answer the human's direct question plainly first — **is there a pip/cargo equivalent for UE?** — then the real options: source-in-`Plugins/`, precompiled binaries, git submodule, engine plugin, Fab/marketplace, third-party tooling. For each: how a consumer installs, how versioning and upgrades work, maintainer cost, and suitability for a source-available library with five vendored native dependencies. Note explicitly where the landscape changed recently.
- **Slice 5 — Q2 recommendation + backlog signal.** The most ergonomic consumer path with its versioning/upgrade story. Then **file `CATHODE-6`** for the API-surface / developer-ergonomics review the human flagged, and note it in `docs/roadmap.md`.

## Non-Goals

- **No uptime work of any kind.** Closed — do not research, wire, or modify it.
- **No implementation.** No build, harness, packaging, CI workflow, and **no platform-gating source change** — that is recommended for a later sprint and must be reconciled with `CATHODE-2`'s scope.
- **Do not make the standalone build to test an option** (`CATHODE-2`).
- **Run NO tests this sprint, and do not acquire a UE engine** (both deferred by the human, 2026-07-24). Every Linux-behaviour claim stays DOCUMENTED/ASSUMED.
- **No API-surface review** — file the signal; do not perform the review.
- **Do not present an ASSUMED claim as DOCUMENTED.** An uncited confident answer is worse than an admitted gap; the human is making a durable choice on this.

## Starting Surface

1. **Q1 in-repo** — the Win64 gates in **7 of 13** `*.Build.cs`; the unconditional include paths; `Cathode.uplugin`'s absent platform lists; `docs/design/standalone-test-harness.md`; the 66-file / 143-case suite with uniform `EditorContext`; `CathodeRuntime.Build.cs:45` `qjs.dll` staging.
2. **Q1 external** — UE 5.7 Linux support, headless/commandlet automation, engine acquisition and licensing for CI.
3. **Q2 external** — UE plugin distribution channels and conventions, versioning practice, how comparable plugins handle native dependencies.
4. **Q2 in-repo shape** — 13 modules, five vendored native libraries from pinned sources, `CanContainContent:false`, MIT + `THIRD_PARTY_NOTICES`.

## Acceptance

1. **Q1 answered, staged where staged is accurate** — with the three-axis partition, the beachhead costed, what stays on Windows, and the `CATHODE-2` / `CATHODE-3` rescopes stated explicitly as changes to filed tickets.
2. **Q2 answered**, beginning with a plain answer to the pip/cargo question, then options with install / versioning / upgrade / maintainer-cost each, and a single recommendation.
3. **Citations — exhaustive on the load-bearing set, sampled on the rest.** Every citation that a *recommendation* rests on resolves **and its quoted sentence supports the claim** (this set will be small). Plus a **reviewer-selected** random 3 of the remainder. "Current" means it resolves **and** its publication date post-dates any transition the claim depends on — a pre-Fab source can resolve perfectly and be wrong. *(Sampling alone is the shape that failed in 033; the load-bearing set is checked exhaustively.)*
4. **Every recommendation states its evidence mix** — literally "rests on N OBSERVED / M DOCUMENTED / K ASSUMED," reconciling with the sprint's own grade table. No "mainly" threshold to litigate; the gate is arithmetic and fires on weak recommendations automatically.
5. **Every prevalence claim names its population** (which artifacts, how found, how many examined) or is graded ASSUMED.
6. `CATHODE-6` filed for the API-surface / ergonomics review, noted in `docs/roadmap.md`.
7. Zero modifications to uptime; zero to `Cathode/`. Any in-repo count names its universe.

## Review Questions (for PM / human)

- **Q-Linux-probe — DEFERRED** (option c; see Green-light rulings). No runnable Linux UE engine exists on this host; the human deferred the probe rather than acquire one. Q1 lands documentation-grade; the Windows setup underway may open a future probe.
- **Q-Linux-scope-change — if Linux-first is adopted, `CATHODE-2` must produce Linux libraries and `CATHODE-3` must target Linux** (and its partition needs three axes, not one). That rescopes two filed tickets, and Win64-only was an explicit 033 non-goal. Confirm the sprint may recommend it. *(sequencing — human)*
- **Q-Gating-change — the beachhead needs a small source change** (platform-gate the include paths; add `PlatformAllowList` to the descriptor). Recommended here, not made. Confirm it belongs to a later sprint rather than this one. *(boundary — human)*
- **Q-Demo-platform — RULED: Windows-only** (final; see Green-light rulings). Windows for game compilation and the demo; Linux explored later. The GPU axis stays a CI *exclusion*, not must-work.
- **Q-Probe (Windows UBT) — demoted per your direction.** *Lean: skip.* With uptime closed and the direction Linux-first, it validates a path we may not take. Raise again only if Q1 concludes Windows remains load-bearing. *(noted, not blocking)*

---

*Reframed by cathode.core-lead per the PM/human revision (2026-07-24), then corrected through team review. The developer established the compile-not-link failure, the absent per-module gating, RHI as a second partition axis, and the staged Stage-1/Stage-2 shape; the reviewer corrected "all 13" to "7 of 13" — a shorthand that would have erased the beachhead — confirmed the 143-case and EditorContext figures exactly, and identified prevalence-without-population as the denominator failure in external clothing, which is gated rather than advised. Green-lit and completed 2026-07-24.*

---

# Postmortem

**Sprint closed 2026-07-24.** All acceptance gates met; both research questions delivered documentation-grade with honest grading throughout. **Q1:** Linux-first development + CI testing is feasible but *staged* — the ~10-case CPU beachhead is the minimal proof, and its blocker is **WALL 1 (engine acquisition + Epic EULA)**, not the vendored libraries, because the suite is `EditorContext`-uniform (143/143) so even the smallest step needs the editor. **Q2:** UE has **no pip/cargo equivalent** for a native plugin (plugins are directories discovered by filesystem scan, not resolved from a registry); recommendation is to ship a **versioned precompiled binary plugin release**, from-source path kept. Deliverable: `docs/research/linux-first-and-distribution.md`. Backlog: `CATHODE-6` filed (API-surface/ergonomics review); `CATHODE-2` and `CATHODE-3` carry rescope comments (Linux libraries + orchestration + `.so` staging; three-axis partition + Linux headless target).

**Evidence ceiling (load-bearing):** no runnable Linux UE engine on the authoring host, so **every Linux-behaviour claim in 034 is DOCUMENTED or ASSUMED — zero OBSERVED.** The probe was authorized then deferred; the Stage-1 "it will run" is the single ASSUMED a future sprint converts by acquiring an engine and running the beachhead.

## Lead Retrospective — cathode.core-lead

**What I tried.** Hold a moving research target still enough to route: turn each scope revision into a written contract before work resumed, keep one narrow slice with one owner, hold the builder/checker separation, and arbitrate every report against the stated grades rather than whether the answer sounded good. 034 was mostly future-shaped — a research question with no in-repo anchor — so the discipline shifted from "is this right against the tree" to "is this claim graded honestly against a source."

**What FAILED — mine.**
1. **I wrote the count slip.** "the Win64 gates in all 13 `Build.cs`" — it is **7 of 13**, and the shorthand would have erased the entire beachhead, because the three clean modules are exactly the ungated ones. My third count overstatement across two sprints; the reviewer caught it.
2. **I let the contract lag the rulings.** The PM green-lit with four rulings (demo=Linux among them); I routed them to the dev but did not fold them into the contract. The reviewer, re-deriving from the on-disk doc, correctly flagged a demo-platform contradiction that existed *only* because the ruling lived in the message channel. The lesson is sharp: **rulings that fix scope must land in the contract immediately, or independent re-derivation flags phantoms** — and phantom-chasing costs the checker real time.
3. **I scoped the integration frame on an unchecked assumption.** 034 opened as "which git/UE mechanism wires Cathode into uptime." The dev found the binding constraint was the **Samba transport** — a git-shaped assumption whose precondition I never checked. The frame's substrate was cheaper to verify than the frame was to build, and I built the frame first.

**What worked, and why it mattered.**
- **Surfacing the demo=Linux consequence rather than executing it.** The green-light ruled demo=Linux; I traced that it put Linux Vulkan on the critical path and surfaced the cost. The human reversed to Windows-only once the cost was visible. That is the whole point of consequence-surfacing — the ruling was made without the implication in view, and showing it let the human decide with full information rather than discover the lift later.
- **Parameterizing around blockers** — the copyright holder and absent tracker project (033), the missing Linux engine (034) each landed in a gap while documentation-grade work proceeded, rather than stalling on a human-only decision.
- **Refusing a green that wasn't proof** — the blend2d false-green (033) and the evidence-mix arithmetic (034) were the cases where a passing-looking signal was held to the actual causal chain.

**What the next sprint inherits.**
- **Two recurring failure shapes, not one** (the dev's distinction, the most important carry): (1) counting over the wrong universe — *enumerate strata once, compute every figure against that fixed universe*; (2) a summary line drifting from what it summarizes — *re-derive every roll-up from its parts at write-time, never restate a remembered total*. Six-plus instances across two sprints were treated as one; they need different checks.
- **The grade ladder needs a rung for structurally-unreachable primaries** (the reviewer's carry): a claim whose only primary is acceptance-gated but is convergently corroborated by independent secondaries gets a distinct honest grade — not laundered to DOCUMENTED, not buried at ASSUMED — and the acceptance-gated sources are named once, up front.
- **A reviewer recommending a "better source" fetches it first.** The discipline the checker enforces applies to the checker.
- **Check a frame's precondition before evaluating options inside it.** When a finding one level down *dissolves* options rather than ranking them, the frame was the thing to test.

## Dev Retrospective — cathode.core-dev

**What I tried.** A research sprint, not a build one. The through-line was **ground the frame before reasoning inside it, and grade every external claim by how it was actually obtained** — never let a confident sentence carry a grade it didn't earn. Check the mechanical preconditions of each proposed option before evaluating it; separate what a primary source states from what a blog repeats; and on every count, name the universe it's counted over.

**What evidence changed the plan — and the transferable lesson about frames.** Twice the plan was reframed, and the first I caused by grounding one level below the question. The **Samba transport retired the integration frame**: I checked the precondition every candidate mechanism shared — *can the Windows build box even see the standalone copy* — and found the box is an SMB mirror whose share root **is** the uptime repo, with `~/Work/cathode` a sibling outside it. That fact didn't rank the mechanisms; it *dissolved* half of them (symlink refused by `wide links=no`; submodule gated on a remote Cathode doesn't have) and reframed the real first question. The transferable lesson: **a research frame inherits the assumptions of the layer beneath it, and those are cheaper to check than the frame is to build.** One `cat setup.ps1` + one `smb.conf` read invalidated the assumption the entire slice rested on. Before evaluating options within a frame, spend the cheap check on the frame's own precondition; when a finding one level down *dissolves* options rather than ranking them, the frame itself was the thing to test.

**Which external-evidence gates caught something vs were ceremony.** *Primary-vs-secondary* CAUGHT the load-bearing thing — the licensing grade boundary is the whole Q1 answer, and marking primary-DOCUMENTED-for-direction vs primary-EULA-unverified-for-the-sharp-claim is what let the recommendation stand on the conservative reading. *Prevalence-names-population* CAUGHT a real overreach (self-hosted "common pattern" → ASSUMED). *Quote-the-sentence* was mostly ceremony but earned its keep once — the verbatim *"only binary packages (wheels)"* is what made pip-is-Python-only airtight. *Cite+date* caught nothing but is cheap staleness insurance — a dateless "Marketplace" claim would already be a year stale post-Fab.

**What made the staged reorder findable.** Decomposing "the tests" into three independent axes (vendored × RHI × content) instead of one block — the beachhead fell out mechanically, then a constraint from a *different* slice (`EditorContext`-uniform 143/143) crossed it and forced WALL 1 under even the smallest step. The naive "start small to dodge the big dependency" survives only while the suite is one undifferentiated block.

**What FAILED — count slips, same denominator DNA as 033.** Win64 "all 13" → actually **7 of 13** (the lead wrote it, I didn't catch it first pass); the **5-vs-8 GPU universe** stated without its universe; the **Q2 tally 7→5+2**, rolling 2 non-primary grades into "DOCUMENTED" against my own key. **Is the fix landing? Partially.** The 033 fix (enumerate strata once) works and held where applied deliberately. But the Q2 tally wasn't a wrong denominator — each tag was graded right; the **roll-up stopped matching the tags beneath it**. That is 033's *self-status* DNA, not its *denominator* DNA. So there are **two** recurring shapes, and I'd been treating them as one: (1) counting over the wrong universe [033 fix applies]; (2) a summary drifting from what it summarizes [re-derive the roll-up from the tags at the moment of writing, never restate a remembered total]. Naming them as two is the actual 034 lesson — one fix landing hid that a second was still open.

**What the next sprint inherits.** `docs/research/linux-first-and-distribution.md` is the record; the in-doc Q2 tally supersedes any message tally. `CATHODE-2`/`CATHODE-3` carry rescope comments (bodies unchanged by design — read the comments); `CATHODE-6` is the API-surface review, distinct from the Q2 distribution recommendation. The evidence ceiling is load-bearing: zero OBSERVED Linux-behaviour; the Stage-1 "it will run" is the single ASSUMED a future sprint must convert. Both Fab primaries are 403-gated; Marketplace→Fab stands on press. **CATHODE has no git remote today** — worth resolving before the prebuilt-release recommendation is implemented.

## Review Retrospective — cathode.core-review

**What I tried.** My job inverted. In 033 I re-derived from the tree; here most evidence was external, so I shifted to three moves: (1) **fetch the cited primary and confirm the quoted sentence is actually there** — the highest-value check, run on every load-bearing citation; (2) **grade the source against the contract's mechanical bar** — primary, or a secondary quoting one; (3) **re-derive the in-repo anchors the external claims pivot on** — because the load-bearing conclusions kept resting on a tree fact after all (WALL 1 on `EditorContext` 143/143; the beachhead on the vendor dependency graph; the tally on tag arithmetic). Even an "external" sprint had a verifiable spine.

**What evidence changed the plan.** Fetching, not assuming, moved three things: an Epic **primary** (*Overview of Containers*) that upgraded the category/direction claim; the **primary EULA returning 403**, capping the sharp public-registry-bar wording below primary; and re-deriving the Q2 tag counts, which caught the tally rolling **2 non-primary claims into "7 DOCUMENTED"** — mattering because Slice 5's recommendation was evidence-mix-gated and a "7 DOCUMENTED" headline would have made a 2-ASSUMED recommendation read firmer than it was.

**What FAILED — mine.** In Slice 4 I flagged an "accessible Epic primary the dev missed" (the `epicgames.com/site/news` Fab page) and asserted it *would upgrade* the grade — **without fetching it first.** In Slice 5 it was 403 too. I surfaced a source from a search-result title and assumed it was reachable — a small instance of the exact "plausible URL laundered into a citation" trap the sprint's own Method warns against, committed by the reviewer whose job is to catch it. **A reviewer recommending a "better source" must fetch it before recommending it, or the review inherits the defect it's policing.**

**The 403 wall — the concrete carry-forward.** Three load-bearing primaries (the EULA, both Fab announcements) are acceptance-gated and **structurally unreachable from this host**. The contract's binary primary/not-primary bar has no cell for this, so both fell into the gap between *overclaiming* (DOCUMENTED) and *understating* (ASSUMED — for a fact corroborated by 5 independent secondaries). **The grade ladder needs a rung:** `SECONDARY-CORROBORATED` / `primary-unreachable` — a distinct honest grade — and, mirroring the denominator convention, **name the acceptance-gated sources once, up front** so every claim resting on one is visibly capped. Make the invisible property (source-reachability) explicit and gate it.

**Is the primary-vs-secondary bar earning its cost?** This sprint it changed **no conclusion** — all robust-by-corroboration under the conservative reading. As an active error-catcher it earned nothing here; but that's survivorship bias — its value is **insurance** against the one case where a confidently-cited secondary is *wrong* (the "landscape moved" trap: a pre-2024 "Marketplace" source is confidently, plausibly, wrongly primary-shaped). Where it *did* do real work was the **tally** — keeping 2 non-primary out of DOCUMENTED, a recommendation-firmness signal. Verdict: keep the bar; its job is calibrating recommendation confidence, not vetoing individual facts.

**ACCEPT-with-fixes, external vs in-repo.** In 033 the fixes were stale *status* (binary-checkable). In 034 they were grade *calibration* (5.8-vs-5.7; the tally; the public-bar held below DOCUMENTED). The 033 test extended cleanly to *"is the finding sound and its grade slightly hot, or is the finding wrong?"* — grade-hot took accept-with-fix, finding-wrong would take reject, I never had a finding-wrong. So accept-with-fix is **more** native to external work: the defects there are almost always over-stated *confidence*, not a wrong claim, and calibration is a fix, not a rejection.
