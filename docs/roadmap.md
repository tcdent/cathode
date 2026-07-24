# Cathode — Roadmap to Full Standalone

**Status:** deferral roadmap authored in sprint 033 (`docs/teams/cathode-core/033-cathode-standalone-relocation.md`).
**Purpose:** state, with evidence, what remains between the current relocated repository and the North Star.

Every problem statement below cites a concrete file and line. Where a question is unresolved this
document **states the question and what evidence would settle it** — it does not answer it. The one
exception is the test arena, whose approach the human ruled must be designed now; that design is
[`docs/design/standalone-test-harness.md`](design/standalone-test-harness.md).

> **Where design-of-record lives.** `docs/browse/` is the *inherited* design-of-record, path-pinned
> by source citations in files held byte-identical, and is therefore **frozen — new design does not
> land there**; `docs/design/` is cathode-core's design-of-record going forward.

---

## North Star

> "This library will eventually be fully standalone — a full executable game that demonstrates its
> functionality. No dependency on uptime for anything: not for builds, not for tests, not for
> content." — 2026-07-23

The dependency on uptime is **temporary scaffolding to be retired**, in three arenas — **build**,
**test**, and **content** — with a fourth, **integration**, governing how uptime consumes Cathode
while that retirement is underway. A fifth workstream, the **pre-public hygiene scrub**, is
cross-cutting rather than an arena.

What this sprint delivered toward it: a clean, self-contained, MIT-licensed repository with full
third-party attribution and no path reaching into the uptime tree. Nothing yet builds or runs.

---

## Arena 1 — Build

### Current situation (cited)

- **The repository cannot build as checked out.** `Source/ThirdParty/<lib>/{include,Win64}` are
  gitignored build outputs (`.gitignore:18-19`; only a `.gitkeep` per lib is tracked), yet every
  consuming module actively links them:
  - `Cathode/Source/CathodeRasterizer/CathodeRasterizer.Build.cs:51` → `blend2d.lib`
  - `Cathode/Source/CathodeText/CathodeText.Build.cs:41` → `harfbuzz.lib`
  - `Cathode/Source/CathodeLayout/CathodeLayout.Build.cs:41-42` → `litehtml.lib`, `gumbo.lib`
  - `Cathode/Source/CathodeRuntime/CathodeRuntime.Build.cs:38` → `quickjs.lib`, and `:45` stages
    `qjs.dll` as a `RuntimeDependencies` entry.
  A fresh checkout therefore links files that do not exist.
- **The only build orchestration lives outside this repository and is host-bound.** The
  vendor→`ThirdParty` build is uptime's `game/setup.ps1` `Cmd-Vendor` (`:316-434`) — not carried
  here, Windows/CMake/MSVC only, and it resolves `vendor\lib\cathode` relative to uptime's
  `SourceRoot`. `Cathode/install.sh` (re-rooted this sprint) stages library *sources* to the
  repository-root `vendor/`, but performs **no build**: it clones, checks out, and verifies pins only.
- **The Blend2D security patch is applied by nothing.** `Cathode/patches/blend2d-assign-data-underalloc.patch`
  has no `git apply` in any tracked script in this repository. Corroborated by
  `docs/teams/cathode-core/029-backlog-convergence.md:66` ("written but not applied").

### The question to resolve (NOT answered here)

**How do the five vendored libraries get built into the `{include,Win64}` layout the `Build.cs`
files expect, from within this repository, on a clean checkout?**

Options to weigh:

| Option | Shape | What would settle it |
|---|---|---|
| **A. Port a plugin-local vendor build** | Re-root `Cmd-Vendor`'s per-lib CMake recipes into this repo as its own entrypoint | Actually build one library plugin-local end-to-end and confirm it lands at the exact path a `Build.cs` links. Harfbuzz is the cheapest probe: pure static, no DLL, no patch, no embedded dependency. Settles feasibility by demonstration rather than estimate. |
| **B. Commit prebuilt Win64 binaries (LFS)** | Ship `{include,Win64}` as tracked artifacts | Measure the repository-size and update-cadence cost, and decide whether a binary drop is acceptable provenance for an OSS project whose current identity model is "the pinned commit IS the provenance" (`THIRD_PARTY_NOTICES`). Note this option may retire `install.sh`'s clone role. |
| **C. Hybrid** | Build from source, cache/publish binaries for consumers | Only meaningful once A is proven; inherits A's evidence requirement. |

A decision here also determines whether `install.sh` survives in its current form and where
`vendor/` lives — so the option choice is upstream of any further path work.

### Grounding to validate (hard-won, NOT settled fact)

These were derived during sprint 033 analysis. They are **leads for the implementer to verify**,
not established results:

- **Patch idempotency.** `install.sh` runs `git checkout <pin>` against a full clone, which
  **reverts** any applied patch on every run. Application must therefore happen *after* checkout
  and be idempotent (a `git apply --check` guard, or apply-to-known-clean). The patch's diff
  prefixes are `a/blend2d/core/array.cpp`, so it applies with `-p1` from `vendor/lib/cathode/`,
  not from inside `blend2d/`. *Status: reasoned from the script and the patch header; not executed.*
- **The `qjs.dll` soname requirement.** `CathodeRuntime.Build.cs:45` stages the runtime DLL under
  the name `qjs.dll`, not `quickjs.dll`, with an in-file note that the import library carries the
  `qjs.dll` soname and that a mismatch yields loader error 126. If a ported build renames the
  output, the plugin will **link successfully and fail at runtime**. *Status: UNTRACED — read from
  the Build.cs comment, never reproduced or confirmed against a built artifact. Flagged as such.*
- **The deterministic patch proof.** `Cathode/Source/CathodeRasterizer/Private/CathodeRasterizerVendorPatchTests.cpp`
  is intended as the patch discriminator, but its separator is **undefined behavior**: it relies on
  an unpatched library *crashing* on a heap overflow. Overflow-to-crash is not guaranteed — a
  512 KiB overflow into mapped adjacent heap may neither crash nor fail the byte-exact round-trip,
  so the test can **false-green on an unpatched library**. A deterministic separator is needed.
  **asan or Windows PageHeap is the known-deterministic path** — either makes the overflow reliably
  fatal, converting an undefined-behaviour lottery into a controlled failure.
  Asserting `BLArray` **`capacity()` / `impl_size` remains an OPEN candidate**, not a ruled-out one.
  Be precise about why: `size()` is set to `N` on both the patched and unpatched paths, so `size()`
  cannot discriminate — but `capacity()` is a *different field*, and that argument does not transfer
  to it; the patched path sizes the allocation from `n` while the unpatched path sizes it from the
  old `u.size`, so `capacity()` is exactly where the two differ. The real caveat is narrower: the
  overflow happens *inside* `assign_data`, before any post-call read — but that only matters in the
  case where the process actually dies, which is the case that needs no separator at all. So
  `capacity()` is worth evaluating (likely at a size small enough not to be fatal), and should not
  be discarded on the `size()` rationale. Whatever separator is chosen should be pinned to
  the build it ran against via `Cathode::CathodeAggregateBuildId()`
  (`Cathode/Source/CathodeRuntime/Private/CathodeBuildId.cpp:94`), which reads every loaded Cathode
  module's image live. *Status: analysis of the test source; not executed.*
- **A doc-vs-code gap the implementer will hit.** Two carried files assert a patch-application hook
  that **does not exist**: `CathodeRasterizerVendorPatchTests.cpp:15` ("applied by the setup.ps1
  `git apply` hook BEFORE the blend2d build") and `Cathode/Source/CathodeRasterizer/COVERAGE.md:33`
  (same claim, in more detail). Both are byte-identical carried files and were deliberately not
  edited. Do not trust them; trust `029:66` and the absence of any `git apply` in tracked scripts.

### North-Star tie

Build with **no uptime dependency** — its own orchestration, no `setup.ps1`.

---

## Arena 2 — Test

### Current situation (cited)

- **66 test files (143 automation test cases) exist** across the plugin (`*Tests.cpp` under
  `Cathode/Source/`; every case is an `IMPLEMENT_SIMPLE_AUTOMATION_TEST`), but this repository has
  no way to run them.
- **The only runner is uptime's, and it is MCP-bound.** uptime's `make test`
  (`game/Makefile:269-272`) invokes `uv run python -m uptime.testing` with `WIN_MCP_HOST`/`WIN_MCP_PORT`,
  driving a **already-running** UnrealEditor over MCP. Neither the driver nor that editor is part of
  this repository. Corroborated by `docs/teams/cathode-core/029-backlog-convergence.md:60` — "a
  stock runner has no UE/MCP".

### Resolution

**This arena's approach is DESIGNED, not deferred** — per the human's ruling that test execution is
part of being standalone. See **[`docs/design/standalone-test-harness.md`](design/standalone-test-harness.md)**
for the recommended design, its three binding constraints, and the explicit list of what
implementation remains.

### North-Star tie

Run its own tests with **no MCP and no uptime tooling**.

---

## Arena 3 — Content / demo game

### Current situation (cited)

- **The plugin ships no content, by descriptor.** `Cathode/Cathode.uplugin:9` sets
  `"CanContainContent": false`.
- **Production content is host-provided.** The production resource root is `/Game/Cathode`
  (`Cathode/Source/CathodeComposition/Private/CathodeComposition.cpp:266-269`,
  `GetDefaultResourceRoot()` = `ProjectContentDir()/Cathode`). Consuming that requires the host
  project to carry a staging entry — in uptime, `game/Config/DefaultGame.ini:19`
  (`+DirectoriesToAlwaysStageAsUFS=(Path="/Game/Cathode")`) — a file that lives in the host, not the
  plugin. uptime supplies the actual assets today (`game/Content/Cathode/`: `M_CathodeScreen.uasset`,
  screen bundles, CSS), including the **screen material** the reference device samples.
- Consequence: the reference example modules compile against Cathode alone, but nothing renders
  without host-supplied content.

### The question to resolve (NOT answered here)

**Where does Cathode's own renderable content come from, given the plugin cannot contain content?**

The tension is structural: `CanContainContent:false` means the assets cannot live in the plugin, so
a standalone demo needs a *host project inside this repository* to hold them. Options include a
committed minimal demo project that owns `Content/Cathode` and the staging config; a content-only
companion; or revisiting the `CanContainContent` descriptor setting itself. What would settle it:
determining whether a repo-local host project is needed anyway for the build and test arenas — if
Arena 1 or Arena 2 lands one, the content question largely collapses into it, and the arenas should
be sequenced to exploit that. This document does not pick.

### North-Star tie

The end state is the **full executable game that demonstrates the library** — self-provided
material, bundles, and fonts, with **no uptime content**.

---

## Arena 4 — Integration

### Current situation (cited)

- uptime enables Cathode explicitly (`game/uptime.uproject` lists `Cathode` in `Plugins[]` with
  `"Enabled": true`) and builds it as part of the full editor build; there is no standalone
  plugin build anywhere (`grep` for `BuildPlugin`/`RunUAT` across uptime's `setup.ps1` and
  `Makefile` returns nothing).
- The extracted copy at `~/Work/cathode` is not yet referenced by uptime in any way.

### The question to resolve (NOT answered here)

Two questions, deliberately distinct:

1. **Interim:** by what mechanism does uptime integrate against the standalone copy while the
   arenas above are being retired — git submodule, symlink, or a path reference? Each trades
   differently on: whether uptime's build box can resolve it, whether Cathode's history stays
   independent, and how a developer edits both trees in one change. What would settle it: the
   uptime build box's actual constraints, which are outside this repository.
2. **End state:** the North Star implies uptime eventually is **not the consumer at all** — Cathode
   ships its own demo game and uptime becomes one downstream user among others, or none. The
   research sprint should state what "done" means for uptime's involvement, not just how to wire it
   up today.

### North-Star tie

**Temporary integration, retiring toward full standalone.**

---

## Cross-cutting — Pre-public hygiene scrub

Not an arena; a **dependency of public release**, gated before any public remote is added. It is not
a blocker for the working repository, which is usable exactly as it stands.

> **THE DRIVER IS READER CLARITY, NOT EXPOSURE.** Ruled by the human via PM, 2026-07-23: **nothing in
> this repository is sensitive.** No credentials, no secrets, nothing confidential — the swept-and-clean
> record below confirms that independently. The problem these rows describe is that an outside reader
> encountering them in a public repository would be **confused**: references to a host project they
> cannot see, to internal tooling and hostnames that mean nothing to them, and to agent identities and
> personas with no external referent. Every item below is therefore a **clarity item**, not a leak.
>
> Whoever implements this must not perform a security-flavoured redaction. The question for each row
> is *"would this confuse or mislead someone reading this repository cold?"* — not *"is this safe to
> expose?"*, which is already answered: yes.

> **SCOPE — derive this inventory from everything that will publish.** That means all four strata:
> the plugin source under `Cathode/`, the carried `docs/browse/` design-of-record, the **32 carried
> lineage docs** in `docs/teams/cathode-core/`, and the **newly authored** `docs/roadmap.md` +
> `docs/design/`. The list below originally grew out of a host-agnosticism read of plugin source and
> `docs/browse/` only, and consequently missed an entire class of clarity item that lives in the lineage and
> in the new docs (the "internal-infrastructure references" section). Re-derive against the full
> publishing set — `git add -An .` enumerates it — not against whichever stratum a previous sweep
> happened to cover.

### Host references — `docs/browse/` and plugin source ONLY (NOT re-derived over the lineage)

**Read the denominator on every figure in this subsection.** These counts come from the original
host-agnosticism read, whose universe was `docs/browse/` plus plugin source. They are **not totals for
the repository**. The lineage stratum is counted separately below, and it dwarfs these.

- **Carried design docs name the host.** Of the **26** files carried into `docs/browse/`, **5 name
  Uptime explicitly** — `components-controller.md:15` ("UPTIME is controller-first, and the panels run
  in-game"), `components-texture-egress.md:82-83` (`UptimeCathodeDevice`, `Source/Uptime`),
  `components-plugin-shell-build.md:24,41` (`game/uptime.uproject`, `Source/Uptime`),
  `vendor-lib-build.md:42` (`UptimeEditor Win64 Development`), and `architecture.md:26` (a *positive*
  decoupling claim — harmless, listed for completeness). **9 of 26 reference "panels"** (11 of 26
  contain "panel" in any form), uptime's UI application layer — the most widespread of these.
- **Four stale README content sections** (`Cathode/README.md`): the module count says 11 (the
  descriptor declares 13), the ThirdParty block is described as stubbed (it is active), the AsmJit
  step is described as a relocation (the realized mechanism is `ASMJIT_DIR`), and it states no CI
  workflow exists (one does). *Path* re-roots were fixed in sprint 033; these are content.
- **`Cathode/Source/CathodeSurfaces/Public/CathodeSurfaces.h:66`** — a comment naming
  `FUptimeSurfaceHandshakeAdapter` (historical note; the code moved off it).
- **`Cathode/Cathode.uplugin:8`** — `"CreatedBy": "Uptime"`.
- **The stale `git apply` hook claims** — `CathodeRasterizerVendorPatchTests.cpp:15` and
  `CathodeRasterizer/COVERAGE.md:33` (see Arena 1); these are *incorrect*, not merely host-flavored.
- **`docs/browse/scale-demo.md`** — one "panel side" phrase.

Note that several of these live in files held byte-identical during relocation, so the scrub is the
first sanctioned opportunity to change them.

### Internal-infrastructure references (from the carried lineage and the newly authored docs)

This class was missed by the original sweep because that sweep did not cover the lineage or the new
docs. All of the following would reach a public reader, and none of them would mean anything to one:

- **Internal hostname `a10k.internal`** — `docs/teams/cathode-core/006-cathode-surfaces-scaffold.md:241`.
  Names internal infrastructure that has no external referent — the clearest example of a
  reader-confusion item (it is not sensitive; it is simply meaningless outside).
- **Agent identities — ~109 occurrences across 32 of the 33 lineage files.** This row was originally
  written as "`cathode.core-lead`, twice in `033`" — an under-count of roughly 97%, because it was
  derived over the newly authored docs only and never over the lineage. Re-derived over the full
  publishing set:
  | Identity | Occurrences | Files |
  |---|---|---|
  | `uptime.browse-review` | 34 | 30 |
  | `uptime.browse-lead` | 30 | 30 |
  | `uptime.browse-dev` | 25 | 25 |
  | `Mundo` (dev persona) | 17 | 17 |
  | `cathode.core-lead` | 3 | 2 (`033` ×2, `docs/roadmap.md` ×1) |
  They appear in unambiguous, structural form — e.g. `### Lead (uptime.browse-lead)` and
  `## Dev Retrospective — Mundo (uptime.browse-dev)` — so they are section headings, not incidental
  mentions. (`cathode.d` ×54 is a **false positive**: it is `lib.cathode.d.ts`, not an identity.)
  Note the third row of that table is self-referential: documenting this class put another
  instance of it into this very file.
- **Internal agent tooling `agent-message`** — 5 lineage files (`006`, `022`, `023`, `024`, `026`).
- **Internal agent tooling `agent-tracker`** — `033` (`:63`, `:101`).
- **Internal test-harness tooling `MCP` / `WIN_MCP_HOST` / `WIN_MCP_PORT`** — 6 files, and note two of
  them are the **docs authored in this sprint** (`docs/roadmap.md:125`,
  `docs/design/standalone-test-harness.md:19`), alongside `.github/workflows/cathode-author-gate.yml`
  and lineage `022`, `029`, `033`. Describing uptime's MCP-bound runner is legitimate content; the
  question at scrub time is whether the env-var names and the tooling name should survive publication.
- **Local filesystem paths `~/Work/...`** — 3 files (`docs/roadmap.md:186`, `033` several,
  `001:33`). Lowest priority: they use `~`, so nothing about the machine is disclosed. Still a
  local-environment detail that means nothing to an outside reader.

### The carried lineage — RULED: stays as-is

**RULING (human via PM, 2026-07-23): the lineage stays AS-IS. Do not redact it. Do not treat it as a
leak.** The four options previously open here are **CLOSED** by that ruling — recorded below so no
future reader reopens a settled question.

The 33 carried lineage docs are **uptime's own sprint history**. They are saturated with host
references *by nature* — that is what they are, not a defect in them:

| Stratum | `uptime` occurrences | Files affected |
|---|---|---|
| `docs/teams/cathode-core/` (lineage) | **263** | **33 of 33** |
| `docs/browse/` | 8 | 5 of 26 |
| `Cathode/` (plugin) | 13 | 9 of 268 |
| `docs/roadmap.md` + `docs/design/` (new) | 47 | 2 of 2 |

Why redaction was the wrong frame, and why the ruling went this way: at 263 references across every
single file, plus ~106 agent-identity occurrences in structural headings, redacting the lineage would
not be a cleanup pass — it would be a rewrite destroying the provenance the lineage was carried *for*.
And since nothing here is sensitive, there is no exposure argument to weigh against that cost.

**Options considered, now CLOSED:**

| Option | Outcome |
|---|---|
| Redact host and identity references throughout | **Rejected** — destroys provenance at no security benefit |
| Keep as an explicitly-historical record | **CHOSEN** — see below |
| Publish as-is with no framing | Rejected — leaves the reader-confusion problem unaddressed |
| Do not publish the lineage at all | Rejected — provenance is the reason it was carried |

**What the scrub ticket therefore does for the lineage:** not redaction, but *framing*. Add a clear
statement that `docs/teams/` is inherited internal sprint history from the project Cathode was
extracted from — retained deliberately for provenance, written for an internal audience, and not
maintained as public-facing documentation. That single orientation note resolves the reader-confusion
problem the 263 references actually cause. The lineage content itself is not touched.

### Swept and clean

**Swept and clean** (recorded so the next pass need not re-derive): no credentials, API keys, or
secrets — the only `password|secret|api_key` match in the whole publishing set is the English word
"secretly" (`011:328`), and every `token` match is lexer/parser vocabulary (CSS selector tokens, test
token arrays). No IP addresses, no UNC/SMB paths, no `localhost`, no absolute `/home/<user>` paths.
`root@a10k.co` appears once, in `LICENSE:3` — that is the ruled copyright holder and is **intended**,
not a leak.

---

## Sequence

Order confirmed by the PM/human, 2026-07-23 (recorded here so the backlog ties are coherent):

1. **Integration research** — next.
2. **Vendor/build** (Arena 1).
3. **Test-harness implementation** (Arena 2; hard-depends on 2).
4. **Self-provided content + demo game** (Arena 3).
5. **Pre-public hygiene scrub** (cross-cutting; before any publication).

Each is **to be filed** as a backlog ticket linking back to its section here. Filing is currently
blocked: the `CATHODE` project does not exist in the tracker workspace and the CLI has no
project-creation verb, so provisioning is required first. The five ticket bodies are written and
ready to file in the order above.
