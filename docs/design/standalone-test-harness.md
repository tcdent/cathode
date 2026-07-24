# Standalone Test Harness — Design

**Status:** design-of-record, authored sprint 033. **Implementation deferred** (see *What
implementation remains*). This is the one deferral item the human ruled must be **designed** now
rather than merely framed: test execution is part of being standalone.

**Roadmap context:** [`docs/roadmap.md`](../roadmap.md) Arena 2.

---

## Problem

Cathode carries **66 test files holding 143 automation test cases** (`*Tests.cpp` under
`Cathode/Source/`; every case is an `IMPLEMENT_SIMPLE_AUTOMATION_TEST`) and no way to run
them. The only runner that has ever executed them is uptime's, and it is structurally unusable
here:

- `game/Makefile:269-272` — `make test` invokes `uv run python -m uptime.testing` with
  `WIN_MCP_HOST` / `WIN_MCP_PORT`, driving an **already-running** UnrealEditor over MCP. It does not
  build, launch, or manage the editor; it talks to one that a human started.
- Neither the Python driver nor the editor it targets is part of this repository.
- Corroborated by `docs/teams/cathode-core/029-backlog-convergence.md:60`: "a stock runner has no
  UE/MCP".

Adopting that runner would re-import a dependency on uptime's tooling and on a live, human-tended
editor session — both disqualified by the North Star.

## Design

**Headless UE editor automation, invoked from this repository against a minimal host project.**

The canonical UE mechanism needs no MCP, no live editor, and no uptime code:

```
UnrealEditor-Cmd.exe <MinimalHost>.uproject \
    -ExecCmds="Automation RunTests Cathode." \
    -unattended -nosplash -nop4 \
    -TestExit="Automation Test Queue Empty" \
    -log -ReportExportPath=<dir>
```

Its inputs are exactly three: an installed UE 5.7, a minimal host `.uproject` that enables the
Cathode plugin, and the compiled plugin binaries. None of the three is uptime.

**Components to build:**

1. **A minimal host project, committed to this repository** — the thinnest `.uproject` that enables
   Cathode, plus the config a test run requires. It exists to be loaded, not played.
2. **A repo-local runner entrypoint** — a script that locates the engine, invokes the command above
   with a `Cathode.` test filter, and translates the automation report into a process exit code so
   the run is usable by a human and by CI identically.
3. **Result reporting** — consume the emitted report (`-ReportExportPath`) rather than scraping the
   log, so pass/fail is structured.

**Why the editor binary and not a packaged game:** the tests are `EditorContext`-flagged — e.g.
`Cathode/Source/CathodeRasterizer/Private/CathodeRasterizerVendorPatchTests.cpp:26` declares
`EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`. `EditorContext` tests
require the editor, so the harness must drive `UnrealEditor-Cmd` in unattended mode. A packaged-game
test runner cannot host them. This is a constraint, not a preference.

## The three binding constraints

### 1. Hard dependency on the build arena

The harness **cannot execute** until the plugin compiles, because automation runs against built
binaries — and the plugin does not build today (`docs/roadmap.md` Arena 1: `ThirdParty/{include,Win64}`
are gitignored outputs the `Build.cs` files actively link). Designing now is fine; **scheduling
implementation before the vendor/build arena lands is not.** The confirmed sequence already places
vendor/build ahead of test-harness implementation; this constraint is why.

### 2. North-Star content guard — partition the suite

Not all 143 cases can run against a bare minimal host, and the difference matters more than it looks:

- **Plugin-self-contained tests** resolve their fixtures relative to the plugin via `IPluginManager`
  (the committed OFL font, the Preact fixture). These run against a bare minimal host immediately.
- **Content-dependent tests** need `/Game/Cathode` host content — the author bundles and the
  `M_CathodeScreen` material that the composition and example proof tests exercise. The plugin
  cannot supply these: `Cathode/Cathode.uplugin:9` sets `"CanContainContent": false`.

**The guard:** populating the minimal host by copying uptime's `game/Content/Cathode/` would make
the tests pass while silently re-importing the exact uptime **content** dependency the North Star
exists to retire. That is the failure mode to design against.

**Therefore:** the harness must partition the suite explicitly — run the self-contained set now, and
mark the content-dependent set as blocked on the content arena (`docs/roadmap.md` Arena 3) with
self-provided fixtures. A harness that claims to "run the suite" without stating this partition
overclaims, and invites someone to close the gap by copying from uptime.

**The partition unit is PER-CASE, not per-file — and this is load-bearing, not pedantry.** The suite
is 66 files but **143 cases**, and the automation framework runs, filters, and reports at the *case*
level (`Cathode.<Area>.<Case>`), so a per-file partition cannot even be expressed to the runner
without collapsing cases together.

*The failure mode:* if any single `.cpp` mixes plugin-self-contained and content-dependent cases, a
per-file partition **splits wrong in both directions** — classify the file as content-dependent and
its self-contained cases are needlessly blocked; classify it as self-contained and its
content-dependent cases fail, creating exactly the pressure to "fix" them by copying uptime's
`Content/Cathode` into the minimal host. That is the content guard leaking, and this guard is the
whole North-Star protection the constraint exists to provide.

*This is not hypothetical.* `Cathode/Source/CathodeComposition/Private/CathodeCompositionTickTests.cpp`
holds 4 cases and references **both** mechanisms — 5 references to the `/Game/Cathode` content root
and 6 to `IPluginManager` (the plugin-relative fixture path). Its 4 cases do *not* share one
classification, which is precisely what a per-file unit would paper over. Classify per case.

#### The classification criterion: does the case LOAD content it does not itself provide?

**Not** "does the case mention `/Game/Cathode`". Grepping for the path is the obvious method and it is
**wrong in both directions** — the implementer should not reach for it:

- **It over-classifies.** Of the 5 `/Game/Cathode` references in that file, **4 are in comments**. The
  single code-level one (`:484`) is
  `TestEqual(DefaultRoot, FPaths::ProjectContentDir()/TEXT("Cathode"))` — a *path-string equality
  assertion* that never touches an asset. Mentioning the path is not depending on content; classifying
  by mention needlessly gates self-contained cases behind the content arena.
- **It under-classifies, which is the dangerous direction.** A case can genuinely require host content
  while never containing the literal string, because the path is *derived*.
  `Cathode.Composition.Tick.ComposedFrameRendersLinkedResource` computes
  `GetDefaultResourceRoot() / TEXT("slice3.css")` and hard-fails via `FPaths::FileExists` + `AddError`
  if that file is absent — a real dependency on pre-existing host content (`slice3.css` ships in
  uptime's `game/Content/Cathode/` today), invisible to a path grep. A case misclassified as
  self-contained will simply fail in the harness, creating exactly the pressure to "fix" it by copying
  uptime's content in. That is the content guard leaking.

Grepping for `GetDefaultResourceRoot` instead does not rescue the method either:
`Cathode.Composition.Tick.ComposedFrameRendersBackgroundImage` calls it, but **stages its own fixture**
(`SaveArrayToFile` of an inline PNG, with a cleanup guard) — so it writes to the production root rather
than consuming host-provided content, and is self-contained despite the symbol match.

**Therefore the criterion is behavioural:** a case is *content-dependent* if it **reads an asset it does
not itself create**. Read the case and determine that; do not infer it from a string match. Expect a
three-way outcome — plugin-relative fixtures only (self-contained); self-staged fixtures at the
production root (self-contained, but needs write access there); and genuine consumers of pre-existing
host content (blocked on the content arena).

### 3. This is not the demo game

The North Star's "full executable game that demonstrates its functionality" and this harness are
**different artifacts** with different requirements:

| | Test harness | Demo game |
|---|---|---|
| Binary | `UnrealEditor-Cmd`, headless, unattended | packaged game, interactive |
| Purpose | prove correctness | demonstrate the library |
| Host project | minimal, exists to be loaded | real content, exists to be played |

They may eventually share a host project, but the harness must not be designed *as* the demo game —
`EditorContext` tests cannot run in a packaged build, so conflating them would break the harness.

## What implementation remains

Deferred to the test-harness implementation sprint (sequenced after vendor/build):

1. Author the minimal host `.uproject` + its config, committed here.
2. Author the runner entrypoint (engine discovery, invocation, exit-code translation).
3. Wire structured report consumption via `-ReportExportPath`.
4. **Perform the partition** — classify all **143 cases** (not the 66 files) as self-contained vs
   content-dependent, from the source, and record the classification per case. Apply the behavioural
   criterion above (does the case read an asset it does not itself create); do **not** grep for
   `/Game/Cathode` or `GetDefaultResourceRoot`, both of which misclassify. This has not been done;
   only the categories, the per-case unit, the criterion, and representative examples are
   established. `CathodeCompositionTickTests.cpp` is the known split file — its
   `...RendersLinkedResource` case is content-dependent while `...RendersBackgroundImage`
   self-stages.
5. Determine engine-discovery strategy (fixed install path, env var, or config) — untested here.
6. Decide whether the harness runs in CI. The existing workflow
   (`.github/workflows/cathode-author-gate.yml`) is Node-only on `ubuntu-latest` and deliberately
   excludes the UE suite; a UE run needs a self-hosted Windows runner with a licensed engine. Out of
   scope for the design, named so it is not assumed.

## Open questions for the implementer

- **Engine version pinning.** UE 5.7 is the current target (uptime's `EngineAssociation`), but this
  repository has no engine pin of its own. Where should it be declared?
- **Does the minimal host need to be a C++ project?** A content-only host may suffice to load a
  compiled plugin; if the harness also needs to *build* the plugin, that likely forces a C++ host.
  Interacts with the vendor/build arena's outcome.
- **Exit-code semantics for the blocked partition** — should content-dependent tests be skipped
  (green) or reported as blocked (distinct code)? Reporting them as blocked keeps the gap visible;
  skipping them silently is how the guard in constraint 2 gets defeated over time.
