# Linux-First Workflow & UE Plugin Distribution — Research

**Status:** research-of-record, sprint 034 (`docs/teams/cathode-core/034-linux-first-and-distribution-research.md`).
**Backlog:** `CATHODE-1`.
**Q1 (Linux-first) — authored Slices 1–3, below.** **Q2 (distribution landscape) — authored Slice 4, below; recommendation is Slice 5.**

> **Evidence grades**, used on every claim: **OBSERVED** (ran/saw it in this repo) · **DOCUMENTED**
> (a primary source — Epic docs, engine source, vendor docs — states it directly) · **ASSUMED**
> (reasoned, or community-sourced, not primary). Blogs and community wikis corroborate; they do not by
> themselves earn DOCUMENTED. External claims carry a source, a date, and a quoted sentence.
>
> **Evidence ceiling for this sprint:** there is no runnable Linux UE engine on the authoring host, so
> **no claim about Linux UE *behaviour* is OBSERVED.** In-repo source structure is OBSERVED; Linux
> behaviour is DOCUMENTED or ASSUMED.

---

## Q1 — Can Cathode develop and test entirely on Linux?

### The plain answer first: how extensive is adding Linux — build flags, or deeper?

**Deeper, substantially. Adding Linux is not primarily a build-flag change; the flags are the last
10%.** It is a small, bounded source change sitting on top of a large body of net-new work that
dominates the effort. Graded, cheapest to costliest:

| Tier | What | Grade |
|---|---|---|
| 0 — pure build flags / config | Essentially none. No config switch turns Linux on; UBT already attempts a Linux build of an enabled plugin — source and missing artifacts stop it, not a disabled flag. | OBSERVED (it is not this) |
| 1 — `Build.cs` **source** | 7 vendored modules add their ThirdParty include path **unconditionally**, above the `Win64` gate (`CathodeRasterizer.Build.cs:43-44`, `CathodeText:37`, `CathodeLayout:37`, `CathodeRuntime:34`, `CathodeBinding:31`, `CathodeDom:44`, `CathodeVerify:48`). Only `PublicAdditionalLibraries`/`RuntimeDependencies` sit inside the gate. | OBSERVED (the three source facts) |
| 2 — descriptor | `Cathode.uplugin` has zero `PlatformAllowList`/`DenyList`; all 13 modules are `LoadingPhase: Default`, so enabling builds all 13. A subset build needs per-module gating. | OBSERVED |
| 3 — **net-new (the majority)** | Linux builds of all 5 vendored libs; a Linux vendor-build orchestration (the only one, uptime `setup.ps1` `Cmd-Vendor:326`, hardcodes `-G "Visual Studio 17 2022" -A x64` + Win64 output dirs, and is not even carried here); `.so` staging replacing the hardcoded `qjs.dll` (`CathodeRuntime.Build.cs:45`); a Linux RHI story; the asmjit-JIT W^X container risk. | DOCUMENTED + ASSUMED |

**Why Tier 1 is a compile failure, not a link failure** (behaviour prediction, DOCUMENTED — no Linux
build ran): the include paths are added unconditionally [OBSERVED], the Linux `include/` dirs are
empty because only the Win64 vendor build populates them [OBSERVED], and the `.cpp` files `#include`
the vendor headers unconditionally [OBSERVED]. From those three OBSERVED facts, a Linux compile of a
vendored module *predicts* failure at the `#include`, before any link step.

All five vendored libraries ship a `CMakeLists.txt` in their vendored source [OBSERVED, local clones],
so they are cross-platform-buildable in principle [DOCUMENTED]; that they build clean for UE-Linux is
[ASSUMED].

### The three walls (licensing is the deciding one)

Running Cathode's tests on Linux hits three independent walls. **Wall 1 is licensing, and it hits
everything — including the smallest possible first step.**

**WALL 1 — engine + license.** The suite is **EditorContext-flagged in all 143 cases** [OBSERVED], so
every UE test needs a Linux *editor* image (an "Engine Tools" / development image), not merely an
engine binary and not the freely-shareable runtime image.

- Epic, *Overview of Containers in Unreal Engine* (UE 5.7 documentation, ?application_version=5.7; accessed 2026-07-24), states
  directly: **"Distribution of development images is governed by the terms of the Unreal Engine
  EULA,"** and **"Since runtime images do not contain the editor or build tools, their distribution is
  less restricted than development images."** → **primary-DOCUMENTED** for the category/direction: the
  images Cathode's tests require are the more-restricted class.
- The **sharper** claim — that Engine-Tools images are barred from *public* container registries — is
  quoted by a community reference (Unreal Containers, *EULA Restrictions*, ©2019-2023, accessed
  2026-07-24: *"any container images containing Engine Tools (e.g. images used for continuous
  integration purposes) can only be stored and shared via private container registries"*, citing EULA
  §1A/§25). Epic's own page defers to "the EULA terms," and the primary EULA
  (`unrealengine.com/eula/unreal`) is acceptance-gated (403), so this exact wording is
  **primary-EULA-unverified**, not flat DOCUMENTED.
- **The recommendation holds under the conservative reading.** Even on the primary alone — dev images
  EULA-governed and more-restricted-than-runtime — a hosted *public* image path is foreclosed, and the
  realistic field collapses to **{self-hosted runner, private-registry container}**. Confirming the
  sharp public-bar wording only tightens an already-closed door; it does not change the answer.

Consequence, and it reorders the naive expectation: **"start small" does not dodge the licensing
wall.** Because even 10 tiny tests need the editor, the beachhead's blocker is engine acquisition +
EULA — *not* the vendored libraries.

**WALL 2 — the vendored-library Linux gap** (`CATHODE-2`). 128 of 143 cases live in vendor-tainted
modules that will not compile on Linux until Linux builds of the five libraries exist [OBSERVED that
they are tainted; DOCUMENTED that the compile fails]. The 10-case beachhead is behind Wall 1 *only*,
not Wall 2 — which is exactly what makes it the right first target.

**WALL 3 — RHI/GPU.** `-nullrhi` lets the 135 GPU-free cases run without a GPU
[DOCUMENTED-by-community]. The 8 GPU cases do real `ReadSurfaceData` readback [OBSERVED] and cannot
pass under `-nullrhi`; on Linux that path is Vulkan. **Ruled 2026-07-24: the demo is Windows-only**
(the human reversed the Linux-demo answer once the Vulkan-on-critical-path cost surfaced), so
GPU-must-work does **not** apply — the 8 GPU cases stay a **CI exclusion**, and the Vulkan path is not
on the Linux critical path.

### The three-axis partition of all 143 cases

Axes computed from source [OBSERVED]: **vendored** = transitive closure over intra-Cathode deps from
all 13 `Build.cs`; **RHI/GPU** = presence of `ENQUEUE_RENDER_COMMAND`/`ReadSurfaceData`/`RHICmdList`/
`FlushRenderingCommands`; **host-content** = reference to `/Game/Cathode` or `GetDefaultResourceRoot`.
**Universe named on every figure** (the 5-vs-8 GPU distinction below is the denominator discipline made
literal).

| Axis | Count | Universe |
|---|---|---|
| Vendor-free modules | 3 of 13 — `CathodeSurfaces`, `CathodeResolver`, `CathodeEgress` | 13 modules |
| Vendor-free cases | 15 of 143 | whole suite |
| GPU cases, whole suite | **8 of 143** (`CathodeEgress` 5, `CathodeExample` 2, `CathodeExampleDevice` 1) | whole 66-file suite |
| GPU cases, within vendor-free set | **5 of 15** (all `CathodeEgress`) | vendor-free set only |
| Host-content (names `/Game/Cathode`) | 4 of 143 (`CathodeCompositionTickTests`) | whole suite |

**The beachhead — vendor-free ∩ GPU-free = 4 files / 10 cases:** `CathodeResolverTests` (2),
`CathodeResolverFileSourceTests` (2), `CathodeSurfaceRegistryTests` (4), `CathodeSurfaceRelayTests`
(2). Which number answers which question:

- **Strict "reads no asset at all" = 8 cases.** `CathodeResolverTests` + both `CathodeSurfaces` files
  create their own inputs in-test.
- **Linux-CI-runnable beachhead = 10 cases.** The extra 2 (`CathodeResolverFileSourceTests`) **read a
  plugin-local committed fixture** (`probe.txt` via `IPluginManager` + `FFileHelper`,
  `CathodeResolverFileSourceTests.cpp:29-35`) — *not* content-free, but Linux-CI-safe because the
  fixture ships in the repo and is therefore on disk on a fresh checkout.

All 10 remain `EditorContext`-flagged [OBSERVED] — so the beachhead needs a Linux *editor*, not merely
an engine or a bare binary. That is Wall 1 again.

**This partition reaches back into `CATHODE-3`,** whose designed partition
(`docs/design/standalone-test-harness.md`) has **one axis** (content-dependency). Linux-first makes it
**three** (vendored × RHI × content). Better learned now than during implementation.

### The CI landscape (what can actually run UE automation in CI)

Separating **what UE supports** from **what people do**, per method.

**What UE supports** [DOCUMENTED]:
- Headless editor-cmd automation. Epic, *Run Automation Tests in Unreal Engine* (UE 5.7 docs, ?application_version=5.7; accessed
  2026-07-24) gives the command form `-ExecCmds="Automation RunTest Test1+Test2;Quit"` and
  `-ReportExportPath="<output path>"`. Epic's page does **not** list the CI flags `-nullrhi`,
  `-unattended`, `-nopause`; those are community-DOCUMENTED (Bugnet, *How to Set Up Smoke Tests for
  Game Builds*, accessed 2026-07-24: `UnrealEditor-Cmd.exe MyProject.uproject -nullrhi
  -ExecCmds="Automation RunTests SmokeTest" -unattended -nopause`). Grade the canonical CI incantation
  ASSUMED-leaning-DOCUMENTED; verify against engine source.
- Official Linux dev images on GitHub Container Registry. Epic, *Building the Linux Container Images
  from Source* (UE 5.7 docs, ?application_version=5.7; accessed 2026-07-24): *"All container images that are included with
  Unreal Engine have prebuilt versions available for download from GitHub Container Registry."*
  Registry `ghcr.io/epicgames/unreal-engine`; `dev`/`dev-slim` are the editor images. Acquisition is
  gated behind Epic-account link + EpicGames GitHub-org membership + EULA acceptance
  [DOCUMENTED-by-community, Unreal Containers *Official Images*, accessed 2026-07-24].

**What people do** [PREVALENCE — population named, graded ASSUMED]: every public UE-CI how-to found
(mevlyshkin.com, filfreire.com, developer.pureweb.io, Sentry's UE-SDK blog; all accessed 2026-07-24)
reaches for **self-hosted runners with UE preinstalled**. Population = "authors of public UE-CI guides
indexed on the open web" — a biased sample (people who blog about CI), **not** UE teams in general. No
primary source quantifies this; graded ASSUMED.

**The four options, graded:**

| Option | UE support | Verdict for Cathode |
|---|---|---|
| A — hosted GitHub Actions as-is | none (runners carry no engine; can't cache it in a public image) | Not viable for the UE suite. **Viable today for the Node author gate** — see the contrast below. |
| B — self-hosted runner, engine preinstalled | full; no public distribution so EULA not triggered | Viable. DOCUMENTED-by-practice. |
| C — containerised engine, **private** registry | full, Epic-blessed (`ghcr.io/epicgames`) | Viable. The derived CI image is itself an Engine-Tools image → also must stay private. |
| D — the mechanism (applies to B & C) | `UnrealEditor-Cmd <proj> -ExecCmds="…;Quit" -nullrhi -unattended -nopause -ReportExportPath=…` | The invocation, per above. |

**The clean contrast — the one OBSERVED thing in the CI picture:** the Node author gate already runs
green on hosted `ubuntu-latest` with **zero engine** (033 Slice 5; workflow `runs-on: ubuntu-latest`).
It clears all three walls by not being a UE test. **Cathode's Linux CI is already half-done for free;
the UE half is gated on Wall 1 first.**

### Recommendation — staged, not binary

**Q1 verdict: Linux-first development and testing is feasible, in two stages, and the first stage is
available much sooner than an all-or-nothing reading suggests — but every UE stage is gated first on
engine acquisition + the EULA (Wall 1), not on the vendored-library build.**

**Stage 1 — the 10-case CPU beachhead as the minimal Linux-CI proof.**
- Needs: the Tier-1 + Tier-2 source gating change (exclude the vendor-tainted modules from the Linux
  build); a Linux *editor* image obtained via the license-gated path and run **self-hosted or from a
  private registry**; `UnrealEditor-Cmd … -nullrhi -unattended`.
- **Does NOT need `CATHODE-2`.** Its blocker is Wall 1 (engine + EULA), not the vendored libraries.
- Proves the Linux headless-editor harness end-to-end on 10 real cases before any Linux vendored
  library exists — the cheap early decision point.
- Evidence mix: **rests on 5 OBSERVED** (the beachhead exists; EditorContext-uniform; the gating is a
  bounded source change; the Node gate is already green on hosted Linux; the partition counts) **/ 3
  DOCUMENTED** (headless editor-cmd automation; Linux dev images exist; dev-image distribution is
  EULA-governed) **/ 3 ASSUMED** (that the gated engine actually runs the 10 cases headless on Linux —
  unprovable here with no engine; the exact `-nullrhi`/`-unattended` flag set; private-registry /
  self-hosted setup effort). **The load-bearing "it will run" is ASSUMED — no Linux build or test ran
  this sprint.**

**Stage 2 — the full non-GPU suite (135 GPU-free cases).**
- Needs: `CATHODE-2` producing Linux builds of the five libraries + the `.so` staging port, on top of
  Stage 1's gating and engine.
- GPU cases (8, whole suite) and the demo stay **Windows** (demo ruled Windows-only, 2026-07-24).
- Evidence mix: **rests on 3 OBSERVED** (the 135/143 GPU-free universe; the vendored taint; the qjs
  staging line) **/ 2 DOCUMENTED** (headless automation; Linux libs cross-platform via CMake) **/ 4
  ASSUMED** (that the five libs build clean for UE-Linux; the Linux vendor-build orchestration; `.so`
  staging behaviour; that the full non-GPU suite passes on Linux). **This stage is mostly ASSUMED and
  should be read as a direction, not a proven plan.**

**What necessarily stays on Windows:** the demo game, and the 8 GPU/`ReadSurfaceData` cases as a
standing CI exclusion on Linux.

### Rescopes this recommendation implies (Q-Linux-scope-change approved; named, not yet ticketed)

Ticket comments are deferred to Slice 5 (with `CATHODE-6`); named here in the record:

- **`CATHODE-2` (vendor/build)** — rescope from Win64-only to **also** produce Linux builds of the
  five vendored libraries, a **Linux vendor-build orchestration** (the existing one is
  VS2022/Win64 PowerShell), and **`.so` staging** for quickjs. This reverses 033's explicit Win64-only
  non-goal, for the Linux-first direction.
- **`CATHODE-3` (test-harness)** — rescope the one-axis (content) partition to **three axes**
  (vendored × RHI × content), and target **Linux headless** `UnrealEditor-Cmd` in addition to the
  Windows editor, handling which cases run under `-nullrhi` vs need a real RHI.

---

## Q2 — UE plugin distribution landscape

*Slice 4 authors the landscape below. The **recommendation** is Slice 5 — this section surveys and
grades the options; it does not pick one.*

### The plain answer first: is there a pip/cargo equivalent for UE?

**No — not for a C++/native plugin like Cathode.** UE has no central package registry with a
dependency resolver and a lockfile the way pip (PyPI) and cargo (crates.io) do. The model is
different in kind, not just in maturity:

- **A UE plugin is a directory of files, discovered by a filesystem scan — not resolved from a
  registry.** A plugin is found by scanning for its `.uplugin` descriptor on disk. [DOCUMENTED] Epic,
  *Plugins in Unreal Engine* (accessed 2026-07-24): *"Plugin Descriptors are discovered and loaded
  automatically by the Engine, Editor, and UnrealBuildTool whenever those programs are run,"* and a
  plugin must sit at `<Project>/Plugins/<Name>/<Name>.uplugin` (or the Engine's `Plugins/` tree) to be
  seen. There is no `install <name>` that fetches-and-resolves; you place the tree and the engine
  finds it.
- **The one pip-like feature UE has is Python-only, and does not apply to Cathode.** [DOCUMENTED] Epic,
  *Python Plugin Dependency Management for Unreal Engine* (UE 5.7, `?application_version=5.7`; accessed
  2026-07-24): *"Unreal Editor supports using pip to download and install all Python dependencies of
  enabled plugins upon launch,"* via a `PythonRequirements` field in the `.uplugin` — but *"You can
  only install binary packages (wheels) with pip."* This resolves **Python** dependencies of a plugin;
  Cathode's dependencies are five **native C++** libraries, which this mechanism does not cover.
- **Third-party package managers exist but are niche and out-of-ecosystem.** Plugget (a cross-DCC
  installer) and Rez are community tools, not UE-native. [ASSUMED — community projects, no primary
  Epic endorsement; population: "tools surfaced by a web search for UE package managers," not a usage
  measure.]

So the honest framing for someone reasoning from pip/cargo: **UE distribution is closer to "vendor a
directory into your project" than to "declare a dependency and let a resolver fetch it."** The pieces
pip/cargo bundle — a registry, a resolver, versioned upgrades, a lockfile — are either absent or
partial, and where present (Fab) they are a *storefront*, not a dependency resolver.

### The options, assessed identically

Each row: how a consumer **installs**, how **versioning + upgrades** work, **maintainer cost**, and
**suitability** given Cathode's decisive constraint — **five vendored native libraries built from
pinned sources, which are NOT committed** (`vendor/` is gitignored; `install.sh` clones them and a
separate build produces `ThirdParty/{include,Win64}`). That constraint is what rules options in or
out, because it determines whether the *consumer* has to reproduce a native build.

| Option | Consumer install | Versioning / upgrade | Maintainer cost | Suitability given 5 vendored native deps |
|---|---|---|---|---|
| **Source in `Plugins/`** | Copy the plugin tree into `<Project>/Plugins/`. [DOCUMENTED] | Consumer re-copies a newer tree; no lockfile, no resolver. Manual. | Low to publish (it's just the tree). | **Weak as-is.** The vendored libs are not in the tree, so the consumer must also run `install.sh` + the vendor build to get `ThirdParty/`. That pushes a native toolchain (CMake/MSVC) onto every consumer — the current friction. |
| **Precompiled binary plugin (UAT `BuildPlugin`)** | Copy a prebuilt tree containing compiled binaries; no build needed. [DOCUMENTED-by-community; Epic community tutorial "Building and Packaging Unreal Engine Plugins", accessed 2026-07-24 — verify against UAT source] | Per-**engine-version** and per-**platform** artifacts; consumer swaps the matching build. | **High.** Maintainer produces and hosts a binary matrix (engine version × platform), and `BuildPlugin` requires the plugin to build first — so Linux binaries are blocked by `CATHODE-2`. | **Strong for the consumer, heavy for the maintainer.** This is the option that makes the five native deps a non-problem for consumers: the built libs ship inside the package. The cost is the maintainer owning the build matrix. |
| **Git submodule** | Add the Cathode repo as a submodule under `Plugins/`. | Consumer bumps the submodule commit; git provides the pin. | Low; needs a public git remote (Cathode has none today [OBSERVED]). | **Same native-dep gap as source-in-`Plugins/`.** A submodule delivers *source*, not the vendored libs (still `install.sh`-cloned), so it does not solve the build burden — it only versions the source cleanly. |
| **Engine plugin** (install into the engine's `Plugins/`) vs project plugin | Place under `<Engine>/Plugins/` so all projects on that engine see it. [DOCUMENTED] | Tied to an engine install; upgrade = replace in the engine tree. | Similar to source/binary, plus it touches the user's engine install. | **Orthogonal to the dep problem** — it changes *where* the tree lives, not whether the native libs are prebuilt. Combine with precompiled to be useful. |
| **Fab** (the former UE Marketplace) | Acquire via Fab, added through the launcher/engine. | Fab manages entitlement + per-engine-version listings; upgrades via the storefront. | Storefront submission + the same per-version binary obligation; content/plugin review. | **A storefront over the precompiled-binary model, not a new mechanism.** Inherits precompiled's "ship built libs per version/platform" cost; adds review + account gating. Suits reach, not a from-source library that wants a `git clone` install. |
| **Third-party (Plugget / Rez)** | Tool-specific fetch + place. | Tool-managed. | Adopt an out-of-ecosystem tool; niche audience. | **Poor fit** — adds a dependency on a non-UE tool the target audience is unlikely to run. [ASSUMED] |

**The recent change to flag explicitly:** the **Unreal Engine Marketplace became Fab**, launched
**October 2024** — the storefront a consumer looks for has a new name and scope (it also absorbed
Quixel, Sketchfab, ArtStation Marketplace). [DOCUMENTED-by-press; Epic's own announcement blog
(`unrealengine.com/blog/fab-content-marketplace-launches-in-october...`) is **403/acceptance-gated**
so unquotable here — corroborated by Creative Bloq and CG Channel (both accessed 2026-07-24), the
latter dated the October 2024 launch.] Anyone reasoning from pre-2024 "UE Marketplace" material is
now one rename behind; a distribution recommendation must target Fab, not the Marketplace.

### What UE supports vs what people do

**What UE supports** [DOCUMENTED]: source trees in `Plugins/` discovered by `.uplugin` scan;
precompiled plugins via UAT `BuildPlugin`; engine-level or project-level placement; Python-dep
resolution via `PythonRequirements`; Fab as the first-party storefront.

**What people do** [PREVALENCE — population named, graded ASSUMED]: I did **not** find a primary
source quantifying how open-source UE plugins ship. Community guides and the shape of the ecosystem
suggest source-available plugins are typically distributed as **a git repo the consumer clones into
`Plugins/`**, with paid/binary plugins going through the Marketplace→Fab channel. POPULATION for that
impression: "UE plugin-packaging how-tos and the visible open-source-plugin repos surfaced by web
search," which is not a measured sample of the plugin population. Graded **ASSUMED**; a real
prevalence claim would require enumerating a defined set of plugins and counting.

### The Cathode-specific pivot (landscape observation, not a recommendation)

Every option divides on one question: **does the consumer have to reproduce the native build of the
five vendored libraries, or does the maintainer ship them prebuilt?** Source-in-`Plugins/`, submodule,
and engine-plugin all leave the native build to the consumer; precompiled-binary and Fab move it to
the maintainer (a per-version/per-platform matrix, and Linux binaries gated on `CATHODE-2`). That
single axis — not the delivery channel — is what a Q2 recommendation will turn on. Naming it here;
Slice 5 chooses.

### Q2 grade tally (reconciled — counting bracketed grade tags in this Q2 section)

Method: count the explicit bracketed grade tags, per the grade key at the top of this document,
treating `DOCUMENTED-by-community` and `DOCUMENTED-by-press` as **non-primary**, not `DOCUMENTED`.

- **5 primary-`DOCUMENTED`** — the `.uplugin`-scan/`Plugins/` placement (×2 tags: plain-answer +
  table), pip-is-Python-only, engine-plugin placement, and the what-UE-supports summary.
- **2 non-primary** — `BuildPlugin` precompiled (community tutorial) and Fab succession (press).
- **3 `ASSUMED`** — third-party tools niche (tagged at both the plain-answer and the table; one claim),
  and the prevalence/what-people-do claim (one tag + a prose restatement).
- **1 `OBSERVED`** — Cathode has no git remote today.

(This corrects a Slice-4 report tally that rolled the 2 non-primary into `DOCUMENTED` and miscounted
`ASSUMED` as 4; tags must match the key beneath them.)

### Q2 recommendation

**Recommendation: ship Cathode primarily as a versioned, precompiled binary plugin release — the
maintainer-ships-prebuilt side of the axis — while keeping a from-source path for contributors.**

Why this side, for a *consuming game developer* specifically: Cathode's decisive constraint is that
its five native libraries are **not committed** (`vendor/` gitignored; `install.sh` clones pinned
sources; a separate build produces `ThirdParty/{include,Win64}`) [OBSERVED]. On the consumer-reproduces
side (source-in-`Plugins/`, submodule, engine-plugin), the consumer inherits that entire native build —
`install.sh` plus a CMake/MSVC toolchain — before the plugin will compile. For a game developer who
just wants a rendering plugin, that is the ergonomic cliff. Moving the native build to the maintainer
(`BuildPlugin` output with the built libs bundled) reduces the consumer's install to **place the
prebuilt tree under `<Project>/Plugins/` and enable it** — the standard UE plugin experience the engine
already supports [primary-DOCUMENTED: plugins are discovered by `.uplugin` scan under `Plugins/`]. There
is no pip/cargo-style resolver to lean on [primary-DOCUMENTED: pip covers only Python deps], so
"prebuilt tree in `Plugins/`" *is* the ergonomic ceiling UE offers.

**Versioning + upgrade story:** cut a **git release tag per Cathode version, and under each tag publish
one prebuilt package per (engine-version × platform)** — the pin already lives in `install.sh`, so a
Cathode version maps to an exact vendored-library provenance. The consumer upgrades by **replacing the
plugin tree with the newer engine-matched release** (UE plugin binaries are engine-version-specific).
There is no lockfile and no automatic resolution; the git tag is the version of record and the release
asset is the artifact. Contributors and packagers still build from source via `install.sh` + the vendor
build; only the consumer path is prebuilt.

**Retirement / lock-in cost (stated, per method):** this commits the maintainer to a **binary matrix**
— one build per engine version and per platform — and Linux binaries are **gated on `CATHODE-2`**
producing Linux libraries (until then, prebuilt is Win64-only and Linux consumers must build from
source). The lock-in is asymmetric: consumers who rely on prebuilt binaries would, if the project later
dropped them, need the native toolchain they were spared — so dropping prebuilt is a breaking change for
that audience. `Fab` is available as a *storefront over this same prebuilt model* for reach, but adds
review + account gating and no new mechanism; it is a distribution *channel* decision, separable from
the prebuilt-vs-source *packaging* decision recommended here.

**Evidence mix (literal, primary-only `DOCUMENTED`; non-primary listed separately, per the reconciled
tally):** this recommendation rests on **2 OBSERVED** (the five vendored deps are uncommitted →
source-install pushes a native build onto the consumer; Cathode has no git remote, constraining the
submodule variant) / **2 primary-`DOCUMENTED`** (plugins install by placement under `Plugins/` +
`.uplugin` scan; pip resolves only Python deps, so no native resolver exists to lean on) / **2
non-primary** (`BuildPlugin` produces the precompiled bundle — community; `Fab` is the storefront —
press) / **2 `ASSUMED`** (that prebuilt is the most ergonomic path for the median consumer — a judgment
from an ASSUMED prevalence impression, not a measured one; that a per-version × per-platform matrix is a
manageable maintainer cost at Cathode's scale). **The load-bearing "prebuilt is most ergonomic" is
ASSUMED — grounded in the mechanics UE documents, but not in a measured survey of consumer preference.**

**What this recommendation does NOT decide:** the `Fab`-vs-self-hosted-release *channel* (separable);
whether to publish Linux prebuilt binaries at all (follows `CATHODE-2`); and the API-surface/ergonomics
work that shapes how pleasant the *enabled* plugin is to use, which is filed as `CATHODE-6`.
