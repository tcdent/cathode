# 001 — Cathode scaffold & library staging

## Nature

001 is a **pure-scaffold sprint**: planning, design, and assembly only. It writes **no
buildable code**, links **nothing into the main game**, and runs **no compile**. It establishes
the on-disk skeleton of the Cathode plugin and stages its four upstream libraries so that a later
*integration* sprint can wire them into the Unreal build with a clean, audited starting point.

The design is **not** this sprint's to invent — it is fully settled in
[`docs/browse/architecture.md`](../../browse/architecture.md), which explicitly hands sprint
ordering to the implementing team:

> "This document defines the system's design and the technologies it is built on. These are
> decisions, not options. Sprint ordering and execution are the implementing team's to own."

This sprint's quality bar is the human's, captured verbatim:

> "this is a greenfield opportunity, so the health, quality and future of this application depend
> on architectural choices made today"

> "we are software developers, not game developers, so act accordingly and keep quality high"

> "this will be released as an open source project and end up in my personal portfolio; I want
> recruiters to call me from it for the next ten years"

And the hard sprint-1 constraint, verbatim:

> "this is a shared dev environment. the working tree is always dirty. other developers share the
> build system, so for the first sprint, scope it pure scaffold. no builds, no linking into the
> main game, pure planning, design and assembly"

> "stage libraries in ~/Work/uptime/vendor/lib/"

The line every slice holds: **001 produces files on disk that the shared Unreal build cannot
see, pick up, or be broken by.** Nothing this sprint authors may enter the UnrealBuildTool scan
graph or require a compile to be accepted.

**Guardrail (the human's steer, verbatim):**

> "for the record, dont end up where the cable team did. they are two days into a major refactor.
> in their defense they did not have a bunch of open source reference for their work, however"

Cathode's premise is the antidote to that trap: it *"authors no HTML engine, JavaScript runtime,
or CSS parser of its own"* — it **assembles** four mature libraries and writes the thinnest glue.
The discipline this imposes on every slice: lean on the staged libraries, do not grow a bespoke
subsystem that a future sprint must refactor to escape. If a slice begins to look like authoring a
new engine-shaped thing, stop and check whether a staged library already does it.

## Scope Boundary

**This sprint owns:**
- The Starting-Surface audit of the project's native-library convention and the four libraries.
- Staging the four upstream libraries (quickjs-ng, litehtml, HarfBuzz, Blend2D) under
  `vendor/lib/` at human-ratified pinned versions, with licenses and provenance retained.
- Authoring the Cathode plugin **directory skeleton** (descriptor, module layout, `Build.cs`,
  `ThirdParty` layout, authoring-tooling folder) as quarantined files — present, well-formed,
  conformant to the doc and the project convention, **but not enabled or compiled**.
- A scaffold map documenting the layout and every item deferred to integration.

**This sprint does NOT own** (named, with the sprint that owns it):
- **Any compile, link, or build** of the plugin or libraries — *integration sprint (002, not yet
  opened)*.
- **Moving the plugin into `game/Plugins/` and enabling it** in the `.uproject` — *integration
  sprint (002)*.
- **Building libraries from source / producing platform binaries / Git-LFS binary layout** —
  *integration sprint (002)*.
- **The full contents of the capability manifest, `lib.cathode.d.ts`, and CSS allow-list** (only
  the *skeleton/structure* is in scope here) — *authoring-contract sprint (003, not yet opened)*.
- **The `game.*` host API shape** — deferred by the architecture doc itself until the
  simulation's shape is known; out of scope for every near-term Cathode sprint.

## Problem

Cathode does not exist on disk yet. The architecture doc specifies a four-library stack and a
standard Unreal-plugin shape, but:
- The four libraries are not staged anywhere; only *rejected*-path CEF spikes live under
  `vendor/lib/explore/` (cef-spout, UE-Uranium, obs-browser).
- There is no Cathode plugin tree, no module layout, no `Build.cs`, no `ThirdParty` layout.
- No version is pinned for any of the four libraries — a 20-year-horizon project cannot start
  from "latest, whatever that was the day someone cloned it."
- The project *has* an established native-library convention
  (`game/Plugins/JoystickPlugin/Source/ThirdParty`, `game/Source/ThirdParty`) that the Cathode
  skeleton must mirror, but it has not been audited and written down for this purpose.

## Goal

Finishing 001 looks like: a reviewer can stand in a fresh checkout, read
`docs/browse/architecture.md`, and find — staged and documented — exactly the four libraries it
names at known-good pinned versions with their licenses, plus a Cathode plugin skeleton that is
structurally faithful to the doc's "Plugin shape" section and the project's native-library
convention, **all of it invisible to and incapable of breaking the shared Unreal build.** The
next sprint can begin integration from a clean, audited, reproducible base.

This scaffold is meant to be exemplary, not a placeholder — the human's steer, verbatim:

> "just do it proper, set the tone for future work"

## Final State

1. A tracked **`install.sh`, part of the Cathode plugin codebase** (under `game/Plugins/Cathode/`,
   *not* in the gitignored `vendor/`), clones each of the **five** libraries (quickjs-ng, litehtml,
   HarfBuzz, Blend2D, AsmJit) and checks out its ratified pinned commit into
   `vendor/lib/cathode/<lib>/`. The script encodes the pins and *is* the provenance + process
   documentation; it ships with the plugin. (Human: "install.sh is part of the plugin, not the
   vendor lib directory that we use to consolidate libs. it is part of the `browse` codebase";
   "clone but put the vendor dir in gitignore … set them up again (which documents the process).
   package management isn't really a thing here right now".)
2. `vendor/` is gitignored (already true — `.gitignore:21 /vendor/`), so the clones are **not
   committed**. Each clone retains its `.git`, so identity is verified by `rev-parse HEAD == pin`;
   the upstream license file arrives with the clone.
3. A Cathode plugin directory skeleton exists at **`game/Plugins/Cathode/`, present but not
   activated** (human's call — see Review Questions, resolved), containing: the `Cathode.uplugin`
   descriptor, the runtime module layout (`Public/`/`Private/`), `CathodeRuntime.Build.cs`
   declaring the documented dependencies (RHI, RenderCore, Slate) and — following the project's
   real binary-library convention (the **SDL2** pattern: wiring lives in the *consuming module's*
   `Build.cs`, the libs sit in a plain asset folder) — the include / additional-libs /
   runtime-deps / delay-load wiring directly, plus a `ThirdParty/` asset-folder layout and an
   authoring-tooling folder skeleton.
4. The capability-manifest / `lib.cathode.d.ts` / CSS-allow-list **skeletons** exist as
   structure-only placeholders, documented as such.
5. A scaffold map (`docs/browse/` or the plugin's own README per the human's call) documents the
   layout and lists every integration-deferred item.
6. None of the above is referenced by the `.uproject`, enabled by default, or otherwise reachable
   by the shared Unreal build. The plugin is **present but not activated** — the shared build does
   not compile it. (The human accepts that *when a future integration sprint activates and first
   compiles it*, it will throw many build errors; that is expected and out of scope here.)

## In Scope — ordered slices

Each slice follows the Method below. A slice is fed only after the prior slice meets its evidence.

### Slice 1 — Starting Surface audit & inventory (read-only)
**Action:** Produce the Starting-Surface inventory before any edit. **Method:** read-only
inspection of the repo + upstream research.
- Audit the native-library convention: how `game/Plugins/JoystickPlugin/Source/ThirdParty` and
  `game/Source/ThirdParty` declare include paths, additional libraries, runtime dependencies,
  delay-load, and LFS binary layout in their `Build.cs` — the pattern Cathode must mirror.
- Audit how JoystickPlugin's `.uplugin` controls enable/discovery (what keeps a plugin out of the
  build), to ground the quarantine decision.
- Per-library inventory row: upstream repo URL, license + license-file path, latest stable
  release tag **and** commit hash, build-from-source story (CMake?), header layout, approximate
  source size.
- Confirm the four chosen libraries are not already staged.
- **Output shape:** a written inventory in the form
  `library -> upstream / license / version+commit / build-system / headers -> proposed staging target`,
  plus a **proposed version pin per library**. No edits to the tree.

### Slice 2 — Library staging (via `install.sh`, clone model)
**Action:** Author a tracked **`install.sh` inside the Cathode plugin** (`game/Plugins/Cathode/` —
part of the `browse` codebase) that **clones** each of the five libraries (retaining `.git`) into
`vendor/lib/cathode/<lib>/` and checks out its ratified pinned commit. The script encodes the pins
and documents the process; `vendor/` is already gitignored so the clones are not committed.
**Slice boundary:** Slice 2 brings the plugin directory into existence *only* to hold `install.sh`
(and whatever minimal containing structure it needs) — the `.uplugin`, runtime module, and
`Build.cs` remain **Slice 3**. A reviewer seeing `game/Plugins/Cathode/install.sh` in Slice 2 is
seeing the intended boundary, not scope creep. **Method:** per lib — `git clone <url> <dir>` (full, `origin` retained so a future
version bump is `git fetch && git checkout <new-pin>`), `git checkout <pinned-sha>`, confirm
`rev-parse HEAD == pin`, confirm the upstream license file is present. **No build, no configure,
no source modification, no flatten/archive** (retaining `.git` is the point — it is what makes the
repo "preconfigured" for staying current with deps through the UE6 era). The pins:

| lib | pin |
|---|---|
| quickjs-ng | `fd0a0210b7be00957751871e7e01b8291268fc29` |
| litehtml | `6ca1ab0419e770e6d35a1ef690238773a1dafcee` |
| HarfBuzz | `56feae4035bdd48f62ba2b8d8c16232d4d89b3a4` |
| Blend2D | `6dbc2cefbc996379e07104e34519a440b49b15d7` |
| AsmJit | `0bd5787b54b575ed94bf32ac452153b34385c514` |

### Slice 3 — Plugin skeleton (quarantined)
**Action:** Author the Cathode plugin directory skeleton per the doc's "Plugin shape" section,
mirroring the audited convention, in the human-chosen quarantine location. **Method:** files only
— `Cathode.uplugin`, the runtime module (`Public/`/`Private/`), `CathodeRuntime.Build.cs`
declaring RHI/RenderCore/Slate + `ThirdParty` include paths, the `ThirdParty/` layout, the
authoring-tooling folder skeleton. **Not enabled, not referenced by the `.uproject`, not
compiled.**

### Slice 4 — Authoring-contract & capability-manifest skeleton (design-only)
**Action:** Create structure-only skeletons for the single-source capability manifest, the
`lib.cathode.d.ts`, and the CSS allow-list, plus a scaffold map documenting the whole layout and
the integration-deferred list. **Method:** design files only; the *structure* of the
single-source-of-truth, not its full contents. The `game.*` API shape stays out of scope.

## Non-Goals

- **No build, compile, link, or run of any kind.** Acceptance never depends on a successful build.
- **Nothing enters the shared Unreal build graph.** No `.uproject` plugin reference, no
  `EnabledByDefault: true` in a discoverable location.
- **Do not delete or alter `vendor/lib/explore/`** (the rejected-path CEF spikes) — they are the
  research record; trace before touching.
- **Do not build libraries from source or produce binaries** — staging is source + license +
  provenance only.
- **Do not invent the `game.*` API** or the manifest's full contents.
- **No backwards-compatibility scaffolding** unless the human asks.

## Starting Surface

Audit first, in this order, producing the inventory shape above before any edit:
1. The project's **two** real native-library patterns (corrected from an earlier wrong citation;
   confirmed independently by both dev and reviewer):
   - **Pattern A — header-only External module:** `game/Source/ThirdParty/cpp_httplib/` has its
     own `cpp_httplib.Build.cs` (`ModuleType.External`, `PublicSystemIncludePaths` → `include/`),
     consumed by *naming* the module in a dependent's `PublicDependencyModuleNames`.
   - **Pattern B — prebuilt binary, wired from the consuming module:**
     `game/Plugins/JoystickPlugin/Source/ThirdParty/SDL2/` is a plain asset folder (`include/` +
     `Win64/` lib+dll + LICENSE, **no own `Build.cs`**); the include/additional-libs/runtime-deps/
     delay-load wiring + existence guards + platform gating live in `JoystickPlugin.Build.cs`,
     binaries on LFS.
   - **For Cathode:** the compiled four libs follow Pattern B's *wiring* (in
     `CathodeRuntime.Build.cs`); the binary mechanics (additional-libs/runtime-deps/delay-load/LFS)
     only become real at 002 when binaries exist. Slice 3 authors the `.Build.cs` shells.
2. `.uplugin` enable/discovery: UBT **scans `game/Plugins/**` and parses every `.uplugin` at
   project-generation** even when disabled; modules only *compile* if enabled (`EnabledByDefault`
   in the descriptor, or an `Enabled` entry in `uptime.uproject` `Plugins[]`). Cathode is absent
   from `uptime.uproject` today. **Implication:** an inert descriptor is still *parsed* for every
   dev at project-generation, so it must be well-formed and must not reference modules that fail
   to load — that is the residual-risk surface of the in-place (Option A) choice.
3. `vendor/lib/` (confirmed: only `explore/` populated; the four chosen libs absent).
4. The four upstream libraries — repo, license, latest stable tag+commit, build system, headers.

Inventory shape:
`library -> upstream / license / version+commit / build-system / headers -> proposed staging target`

## Method

The standard build/log-coherence Method **does not apply this sprint** — nothing compiles, so
there is no build artifact or log signature to bind. Acceptance is **structural conformance**,
proven instead by:
- **Provenance coherence (clone model):** for each cloned lib under `vendor/lib/cathode/<lib>/`,
  `git rev-parse HEAD` equals the pin encoded in `install.sh`, the clone retains `origin` pointing
  at the recorded upstream, and the upstream license file is present and matches upstream for that
  commit. No archive/tarball hash is involved — `.git` is retained, so the commit *is* the proof.
- **Convention conformance:** the plugin skeleton's structure mirrors the audited JoystickPlugin
  convention and the doc's "Plugin shape" section, item for item.
- **Quarantine proof (Option A — resolved):** the `Cathode.uplugin` **explicitly carries
  `"EnabledByDefault": false`** (the decisive element — *not* mere absence, which auto-enables a
  project plugin) **and is absent from `uptime.uproject` `Plugins[]`** (or listed `Enabled:false`),
  so its modules are not compiled by the shared build; and the descriptor is well-formed (valid
  JSON, no reference to a module that fails to load) so it is safe to *parse* at every dev's
  project-generation.
- **Baselines:** since this is greenfield, there are no prior baselines to hold byte-identical;
  the baseline this sprint *establishes* is the staged trees and their provenance records.

## Acceptance — the gates that close the sprint

1. **Libraries staged (×5) via `install.sh` — three-way coherence:** because `vendor/` is
   gitignored, the **committed proof-of-record is `install.sh`**, not the ephemeral clones. ACCEPT
   requires, per lib, that all three agree:
   **(a)** the pin `install.sh` *encodes* == **(b)** the live clone's `git rev-parse HEAD` (detached)
   == **(c)** the ratified contract pin below; **and** `install.sh`'s clone URL == the live clone's
   `git remote get-url origin` == the recorded upstream. (Cleanest way to guarantee a==b: produce
   the clones **by running `install.sh`**, never hand-clone-then-write-different-pins.) Upstream
   `LICENSE` present in each clone (trivially coherent at a clone-at-commit). `vendor/` stays
   gitignored. Named values — the five pins: quickjs-ng `fd0a021…`, litehtml `6ca1ab0…`,
   HarfBuzz `56feae4…`, Blend2D `6dbc2ce…`, AsmJit `0bd5787…`.

   **Non-circular verification:** when the clones are *generated by* `install.sh`, `a==b` holds by
   construction, so the load-bearing leg is **`a==c`** (the script must encode the *ratified* pins,
   not a typo the clone faithfully reproduces). The reviewer reads `a` from the script text, `b`
   from `git rev-parse`/`remote get-url` on the live clone, and `c` from this contract — each from
   its own authoritative source — and confirms the encoded upstream is the **real upstream** (not a
   fork/mirror that would satisfy `origin==encoded` while being wrong).
2. **Plugin skeleton conformant:** `Cathode.uplugin` is valid JSON and declares the runtime
   module; `CathodeRuntime.Build.cs` declares RHI + RenderCore + Slate and the intended
   ThirdParty include/lib wiring per the **SDL2 pattern** (wiring in the consuming module); the
   tree mirrors the audited convention. ThirdParty directories may be empty layout placeholders
   this sprint — headers/binaries are populated at 002 integration.
3. **Quarantine proven:** the reviewer independently confirms `Cathode.uplugin` **explicitly
   carries `"EnabledByDefault": false`**, is absent from `uptime.uproject` `Plugins[]`, and is
   well-formed — so the shared build neither compiles it nor breaks parsing it. (Explicit `false`
   is mandatory: an unspecified field auto-enables a project plugin.)
4. **Skeletons present:** capability-manifest / `d.ts` / CSS-allow-list structure placeholders and
   the scaffold map exist and are documented as structure-only.
5. **No build occurred and none is required** to verify 1–4.

## Review Questions (open forks)

### Resolved
1. **Quarantine location — RESOLVED by the human: Option A, in place but not activated.** Verbatim:
   > "yeah we dont have much of a plugin surface yet. just do it proper, set the tone for future
   > work. put it in the plugins dir, but don't activate it yet. yeah, you will get a ton of build
   > errors first compile, but that's fine"

   So: the skeleton lives at `game/Plugins/Cathode/`, the `.uplugin` is **present but not
   activated** — **explicitly `"EnabledByDefault": false`** and absent from `uptime.uproject`
   `Plugins[]`. The shared build does not compile it. The first-compile errors the human accepts
   are a *future integration-sprint* event, not this sprint. **Residual-risk discipline (from the
   audit):** the `.uplugin` is still *parsed at project-generation* for every dev, so it must be
   valid and must not reference a module that fails to load — keep the descriptor clean.

   **Correctness note (reviewer, asymmetric-risk):** for a *project* plugin under `game/Plugins/`,
   an **unspecified** `EnabledByDefault` resolves to **enabled** (UE returns enabled-by-default for
   project-loaded plugins when the field is Unspecified). So mere *absence* would auto-enable
   Cathode and pull its module into every dev's compile — the opposite of quarantine. The gate
   therefore requires an **explicit `false`**, which is correct whether the default resolves to
   enabled (then it is mandatory) or disabled (then it is harmless). Flagged by the reviewer as
   documented-UE inference, not a source line re-read on this box; adopted anyway because the
   conservative gate is correct under both branches.

### Resolved — pin set (human-ratified)
2. **Pin-set — RATIFIED.** Five libraries (four from the architecture doc's stack table + AsmJit
   as Blend2D's build dependency, see F2). Commit-hash pins explicitly blessed by the human:
   > "commit hash as pin is a non issue to me"

   | lib | pin | commit | note |
   |---|---|---|---|
   | quickjs-ng | `v0.15.1` | `fd0a0210b7be00957751871e7e01b8291268fc29` | released 2026-06-04 |
   | litehtml | `v0.9` | `6ca1ab0419e770e6d35a1ef690238773a1dafcee` | latest tag, 2024-01-31, slow project |
   | HarfBuzz | `14.2.1` | `56feae4035bdd48f62ba2b8d8c16232d4d89b3a4` | released 2026-06-02 |
   | Blend2D | *(no tag)* | `6dbc2cefbc996379e07104e34519a440b49b15d7` | master, 2025-11-29 |
   | AsmJit | *(no tag)* | `0bd5787b54b575ed94bf32ac452153b34385c514` | Blend2D build dep; master, 2026-03-26, Zlib |
3. **F1 — Blend2D no released tags — RESOLVED:** commit pin accepted (quote above).
4. **F2 — Blend2D requires AsmJit — RESOLVED: option (i), stage AsmJit as a 5th vendored lib.**
   Human, verbatim:
   > "add a 5th vendored lib that seems liek an obvious choice though I do appreciate the scope
   > change notification"

   AsmJit is staged as Blend2D's **build dependency** (it lives at `[blend2d]/3rdparty/asmjit` at
   build time; that relocation/symlink is a 002 concern — staging puts it at
   `vendor/lib/cathode/asmjit/` as a sibling with its own provenance). AsmJit is also tagless, so
   the dev resolves a `master` commit pin and records it. **Architecture-doc note:** the stack
   table in `docs/browse/architecture.md` lists four libraries; AsmJit is a transitive build dep,
   not a peer renderer — a one-line mention there is the human's call (lead does not edit the
   human's design doc unasked).

### Resolved — staging mechanism (clone + gitignore + `install.sh`)
5. **Vendoring convention — RESOLVED by the human: clone, gitignore `vendor/`, reproduce via a
   tracked `install.sh`.** Verbatim:
   > "clone but put the vendor dir in gitignore. then we have an install.sh script or similar to
   > set them up again (which documents the process). package management isn't really a thing here
   > right now"

   And the rationale that *requires* retaining `.git` (rules out flattening/archiving):
   > "and then when we want to bump the version we have the repo preconfigured. this project will
   > continue to be developed well into the ue6 release; we are planning on staying current with
   > all deps"

   So the clones keep `.git`/`origin`; a version bump is `git fetch && git checkout <new-pin>` +
   updating the pin in `install.sh`. This **dissolves** the earlier flattened-export/tarball-hash
   method-fork (reviewer's gzip false-reject concern, dev's `git archive --format=tar` refinement):
   there is no archive — `git rev-parse HEAD == pin` on the retained `.git` is the proof.
   Staging-path grouping `vendor/lib/cathode/<lib>/` stands (separates the chosen five from
   `explore/`).

### Open — minor (lead defaults if the human is silent)
6. **Scaffold-map home:** `docs/browse/` or the plugin's own `README.md`. (And: should `install.sh`
   live at repo root, or under an existing scripts location? Lead will have dev place it in the
   conventional tracked spot — outside the gitignored `vendor/` — and report the path.)

---

# Postmortem (sprint close)

Sprint 001 **closed ACCEPTED** — all four slices independently accepted by `uptime.browse-review`,
zero builds run, plugin not activated, `uproject` byte-unchanged, `vendor/` gitignored. The
tracked deliverable is the `game/Plugins/Cathode/` tree (`.uplugin`, `install.sh`, `CathodeRuntime`
module + dependency-complete `Build.cs`, `ThirdParty` placeholders, `Authoring/` skeletons,
`README.md` scaffold map); the five libraries are cloned + pinned under the gitignored
`vendor/lib/cathode/`, reproduced by `install.sh`.

## Lead retrospective — uptime.browse-lead

What worked: every design fork (quarantine location, version pins, the Blend2D AsmJit/F2 scope
change, the vendoring mechanism, the S1 base-module override) was surfaced to the human and
routed, never resolved downstream. Slicing held: read-only audit first, then staging, skeleton,
authoring-contract — each fed only after the prior was accepted.

What I got wrong (caught, not shipped): (1) my contract's quarantine proof was **inverted** —
"carries no `EnabledByDefault`" would have auto-enabled the plugin; the reviewer caught it and the
gate became explicit `false`. (2) The contract's Starting-Surface cited a non-existent
`JoystickPlugin/Source/ThirdParty/*/Build.cs`; corrected to the real SDL2 module-wired pattern. (3)
My flattened-export vendoring lean was the wrong default — the human's bump-and-stay-current-through-UE6
requirement made clone-with-`.git` correct; I should have elicited that workflow constraint before
proposing a mechanism. Process lesson banked: **the sprint plan is a contract, not a devlog** — I
over-edited it mid-sprint; corrections belong in coordination, lessons here at close.

## Developer retrospective — uptime.browse-dev

Four slices, all evidence-proven, zero build. Decisive lessons:
- **Staging mechanism turned over three times** before landing; the deciding input was a *workflow*
  constraint (bump versions / stay current through UE6), not a technical property — a historyless
  export defeats it, so clone-with-`.git` + `install.sh` + gitignored `vendor/` won. Elicit the
  workflow requirement earlier.
- **F1/F2 came from reading Blend2D's CMakeLists**, not its reputation: no release tags (rolling
  release) + a hard AsmJit build dep at `blend2d/3rdparty/asmjit`.
- **Quarantine correction:** "what the exemplar does" (JoystickPlugin omits `EnabledByDefault`) is
  not "what the rule is" — an unspecified field resolves to *enabled* for a project plugin. Verify
  the resolution, not the sample.
- **S1 flipped on human override:** the doc's `RHI/RenderCore/Slate` was the characterizing subset,
  not the complete compile set; the sprint goal was a dependency-complete scaffold.
- Friction: messages crossed in flight twice; re-reading the files and re-verifying from disk was
  the fast path, not trusting the stale report.

## Reviewer retrospective — uptime.browse-review

Re-derived every slice from source/script/contract, never the dev report. Catches (none were
zero-mismatch rubber-stamps): (1) the **`EnabledByDefault` quarantine inversion** (highest value —
an omitted field auto-enables, the opposite of quarantine); argued from asymmetric risk so the gate
held regardless of the exact default. (2) Corrected the contract's non-existent per-`ThirdParty`
`Build.cs` citation to the SDL2 module-wired pattern (dev's independent audit agreed). (3) Flagged
that hashing a `git archive` tarball couples the proof to git/gzip determinism (false-reject risk);
mooted by the clone model. (4) Established the **`a==c` non-circular gate** (install.sh encodes the
*ratified* pins) with a real-upstream check. (5) README doc-accuracy flag (inference stated as
fact) — softened before commit. Stayed inference-until-confirmed: the exact UE5.7
`IsEnabledByDefault` semantics (engine source on the Mac; a read-only SSH was correctly denied to
a peer's suggestion). The gate never depended on it.

## Sprint 002 (integration) inherits

- **First owned build event:** activate the plugin (uproject ref or flip `EnabledByDefault`) — the
  moment quarantine ends; expect the first real compile to surface gaps (base modules are pre-paid).
- **Un-stub `Build.cs` third-party block** per the SDL2 pattern (include paths + platform-gated
  `PublicAdditionalLibraries`/`RuntimeDependencies` NonUFS/`PublicDelayLoadDLLs`, existence-guard
  `BuildException`s); binaries via Git LFS.
- **Build the 5 libs from source** into `Source/ThirdParty/<lib>/` (headers + platform binaries).
  Build systems audited: quickjs-ng CMake/Makefile/meson; litehtml CMake; harfbuzz meson(pref)/CMake;
  blend2d CMake+presets; asmjit CMake. Build *from* the gitignored clones — do not re-vendor.
- **Relocate AsmJit** to `blend2d/3rdparty/asmjit` (or `ASMJIT_DIR`) before Blend2D's JIT build
  (F2's chosen path was stage-AsmJit, not `BLEND2D_NO_JIT`).
- **litehtml is parse-then-render, not live-mutation** — plan the retained-tree-as-source-of-truth
  (+ likely a small litehtml fork) up front, don't discover it mid-integration.
- **The one open confirmation:** the exact UE5.7 `IsEnabledByDefault` semantics for an omitted field
  on a project-loaded plugin — optionally close via an `Engine/Source/Runtime/Projects` grep on the
  Mac; not asserted as fact anywhere.
- `install.sh` is the standing dependency-bump workflow. Manifest/allow-list are TOML
  (human-vetoable, revisit in sprint 003 when the generator is built). `PlatformAllowList` omitted
  (all platforms, design-faithful) — revisit only if a real restriction appears.
