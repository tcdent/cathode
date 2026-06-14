# Cathode vendor-library build — tooling-gap requirement & recipe

**Status:** OPEN TOOLING GAP (surfaced sprint 010 bring-up). This is the requirement + a build
recipe as *input* for the human's tooling teams — it is **not** a hand-run script and must not be
run as a bypass of the existing build tooling.

## The gap, in one line

Cathode's five vendored libraries are present as **source only** (cloned by
`game/Plugins/Cathode/install.sh` into the gitignored `vendor/lib/cathode/<lib>/`). The plugin's
`*.Build.cs` files are now wired to the **conventional UE ThirdParty layout**
(`game/Plugins/Cathode/Source/ThirdParty/<lib>/{include, Win64/<lib>.lib}`), but those `include/`
and `Win64/` directories are **empty** (`.gitkeep` placeholders only). Nothing in the existing
build tooling builds the vendored sources into Win64 static libs. Until that step exists, the
Cathode plugin cannot link.

## What is needed

Four upstream libraries (+ one build-dependency) compiled to **Win64 static libraries** and their
**public headers**, landed at the conventional ThirdParty paths the Build.cs files now reference:

| Vendor source (`vendor/lib/cathode/`) | Static lib output → `Source/ThirdParty/<lib>/Win64/` | Headers → `Source/ThirdParty/<lib>/include/` |
|---|---|---|
| `quickjs-ng` (v0.15.1) | `quickjs.lib` (CMake target `qjs`) | `quickjs.h` (flat root) |
| `litehtml` (v0.9) | `litehtml.lib` **and** `gumbo.lib` | `litehtml.h` (flat root) + `litehtml/` subtree |
| `harfbuzz` (14.2.1) | `harfbuzz.lib` | `hb.h`, `hb-ot.h`, all `hb*.h` (flat root) |
| `blend2d` (master pin) | `blend2d.lib` (AsmJit statically embedded) | `blend2d.h` (flat root) + `blend2d/` subtree |
| `asmjit` (master pin) | *(no separate lib — embedded in blend2d.lib)* | `asmjit/` headers → `Source/ThirdParty/asmjit/include/` |

Pinned commits/URLs are the source of truth in `install.sh` (the `PINS` table). The header include
forms the plugin actually uses (verified in-tree): `quickjs.h`, `hb.h`, `hb-ot.h`, `blend2d.h`,
`litehtml.h` — all **flat-rooted**, so public headers must sit at `include/` root (with `blend2d/`
and `litehtml/` subdirectories present for their internal `#include <blend2d/...>` / `litehtml/...`).

## Cross-cutting constraints (apply to ALL libs)

These are the constraints that make the libs link into UE rather than just compile in isolation:

1. **MSVC runtime = `/MD` (MultiThreaded **DLL**).** UE's Development editor target links the dynamic
   CRT. A lib built `/MT` (static CRT) will produce `LNK2038` runtime-mismatch errors. Every vendor
   lib must be built `/MD` (Release) — i.e. CMake `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL`.
2. **Architecture x64, config Release**, matching `UptimeEditor Win64 Development`.
3. **C++ standard ≥ C++20** (UE 5.7 compiles at C++20); build the vendor libs at C++20 to avoid
   ABI/stdlib surprises.
4. **VS2022 toolset** (the authoritative build is VS2022 over ssh via `setup.ps1`).
5. **No DLLs / no delay-load** — all five are **static**, so there are no runtime `.dll`s to stage
   and the Build.cs files add no `RuntimeDependencies` / `PublicDelayLoadDLLs`.

## Per-library recipe (input — exact flags the tooling team can adapt)

> All commands run from each library's source dir under `vendor/lib/cathode/<lib>/`. The build dir
> is disposable; only the named outputs are copied to `Source/ThirdParty/<lib>/{include,Win64}`.

### quickjs-ng → `quickjs.lib`
```
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL ^
  -DBUILD_SHARED_LIBS=OFF
cmake --build build --config Release --target qjs
# copy: build/**/qjs.lib            -> Source/ThirdParty/quickjs-ng/Win64/quickjs.lib  (rename)
# copy: quickjs.h                   -> Source/ThirdParty/quickjs-ng/include/quickjs.h
```
Note: the CMake static-lib target is `qjs` (the build instruction); the required OUTPUT name is
`quickjs.lib` (browse's spec, matching the Build.cs wiring) — the tooling renames the `qjs` output to
`quickjs.lib`.

### litehtml → `litehtml.lib` + `gumbo.lib`
```
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DLITEHTML_BUILD_TESTING=OFF
cmake --build build --config Release
# copy: build/**/litehtml.lib       -> Source/ThirdParty/litehtml/Win64/litehtml.lib
# copy: build/**/gumbo.lib          -> Source/ThirdParty/litehtml/Win64/gumbo.lib
# copy: include/litehtml.h          -> Source/ThirdParty/litehtml/include/litehtml.h
# copy: include/litehtml/           -> Source/ThirdParty/litehtml/include/litehtml/
```
litehtml bundles the gumbo HTML parser as a sibling static target — both libs are required.

### harfbuzz → `harfbuzz.lib`
```
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DHB_HAVE_FREETYPE=OFF -DHB_HAVE_ICU=OFF -DHB_HAVE_GLIB=OFF ^
  -DHB_BUILD_SUBSET=OFF -DHB_BUILD_UTILS=OFF
cmake --build build --config Release --target harfbuzz
# copy: build/**/harfbuzz.lib       -> Source/ThirdParty/harfbuzz/Win64/harfbuzz.lib
# copy: src/hb.h, src/hb-*.h        -> Source/ThirdParty/harfbuzz/include/   (flat)
```
Built standalone — no FreeType/ICU/GLib (HarfBuzz ships its own UCD tables; Cathode only needs the
core shaper + `hb-ot`).

### blend2d (+ asmjit embedded) → `blend2d.lib`
```
# AsmJit must be visible to Blend2D's build at 3rdparty/asmjit (symlink or copy the sibling clone):
#   vendor/lib/cathode/blend2d/3rdparty/asmjit  ->  vendor/lib/cathode/asmjit
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL ^
  -DBLEND2D_STATIC=TRUE
cmake --build build --config Release --target blend2d
# copy: build/**/blend2d.lib        -> Source/ThirdParty/blend2d/Win64/blend2d.lib
# copy: src/blend2d.h               -> Source/ThirdParty/blend2d/include/blend2d.h
# copy: src/blend2d/                -> Source/ThirdParty/blend2d/include/blend2d/
# copy: <asmjit>/src/asmjit/        -> Source/ThirdParty/asmjit/include/asmjit/
```
`BLEND2D_STATIC=TRUE` embeds AsmJit into `blend2d.lib`; there is no separate `asmjit.lib`. The
plugin defines `BL_STATIC=1` (set in `CathodeRasterizer.Build.cs`) so Blend2D's headers don't emit
`dllimport`. AsmJit's headers are still placed because Blend2D's public headers may reference them.

## How the plugin consumes the outputs (already wired — for reference)

- `CathodeRuntime.Build.cs` — links `quickjs-ng/Win64/quickjs.lib`, public include
  `quickjs-ng/include` (confines quickjs; `CathodeBinding`/`CathodeDom` inherit it transitively).
- `CathodeRasterizer.Build.cs` — links `blend2d/Win64/blend2d.lib`, includes `blend2d/include` +
  `asmjit/include`, defines `BL_STATIC=1`.
- `CathodeText.Build.cs` — links `harfbuzz/Win64/harfbuzz.lib`, include `harfbuzz/include`.
- `CathodeLayout.Build.cs` — links `litehtml/Win64/{litehtml,gumbo}.lib`, include `litehtml/include`.
- `CathodeBinding.Build.cs` — re-asserts the public `quickjs-ng/include`; the lib comes transitively
  from `CathodeRuntime`.

## Required output names (definitive — browse specifies, tooling accommodates)

These are the **required** static-lib output filenames. The plugin's `*.Build.cs` files already link
exactly these; the tooling **produces each named exactly as listed** (renaming the upstream CMake
target output where it differs). There is no name negotiation — browse specifies, the tooling names
the output to match.

| Required output name (what to call it) | CMake build target (what to build) | Lands at |
|---|---|---|
| `quickjs.lib` | target `qjs` | `Source/ThirdParty/quickjs-ng/Win64/` |
| `litehtml.lib` | target `litehtml` | `Source/ThirdParty/litehtml/Win64/` |
| `gumbo.lib` | litehtml's bundled gumbo target | `Source/ThirdParty/litehtml/Win64/` |
| `harfbuzz.lib` | target `harfbuzz` | `Source/ThirdParty/harfbuzz/Win64/` |
| `blend2d.lib` (AsmJit embedded) | target `blend2d` (`BLEND2D_STATIC=TRUE`) | `Source/ThirdParty/blend2d/Win64/` |

The **CMake target name** (e.g. `qjs`, the gumbo target) is the build *instruction* — what to build.
The **output name** above is browse's spec — what to call the resulting `.lib` so it matches the
Build.cs wiring. Where they differ (notably `qjs` → `quickjs.lib`), the tooling renames the output.
No separate `asmjit.lib` is produced — AsmJit is statically embedded into `blend2d.lib`.

The per-lib header set listed above is the minimal public surface the plugin includes today; copying
each lib's fuller public `include/` is harmless and future-proof.

---

## ADDENDUM (supersedes the quickjs-ng row above): quickjs-ng must be a SHARED DLL, not static

**Human decision (sprint 010 bring-up).** quickjs-ng is the **one exception** to the static-lib recipe:
it must be built as a **shared library (DLL)**, not a static `.lib`. The four other libraries
(`litehtml` + `gumbo`, `harfbuzz`, `blend2d`) **stay static** exactly as specified above — this
addendum changes **only** quickjs-ng.

### Why quickjs is the exception — one shared copy of a stateful runtime

quickjs-ng is a **stateful JS runtime** linked by **three** plugin modules that call its C API directly
(`CathodeRuntime`, `CathodeBinding`, `CathodeDom`), all operating on **one shared `JSRuntime`/`JSContext`**
(the runtime is created in CathodeRuntime and handed to the others). The single-module libraries
(litehtml/harfbuzz/blend2d are each used by exactly one module) have no multi-link question, so they stay
static; quickjs is the only multi-module case.

**Accuracy note (don't overclaim):** quickjs-ng keeps almost all mutable state **per-`JSRuntime`** — class
ids are allocated by `JS_NewClassID(JSRuntime*, JSClassID*)` (per-runtime, *not* a process-global counter),
and the allocator and atom table live in the `JSRuntime`. So three static *code* copies operating on one
shared `JSRuntime` would **most likely work today** — this is **not** established corruption of the shared
context. Static-per-module is a matter of **fragility and principle**, not a definite runtime fault.

One shared DLL is nonetheless the correct architecture, for four reasons:
1. **Single authority / single source of truth** — one copy of the runtime's code and whatever state it
   does hold, not N copies that merely *happen* to agree.
2. **Idiom** — duplicating a stateful library's compiled code across DLLs is non-idiomatic; a stateful
   runtime shared by multiple modules is exactly the shared-DLL case (the human's "should never have been
   linked twice — one shared copy").
3. **Fragility — the concrete failure-IF** — the duplication breaks *silently* IF quickjs-ng's narrow
   process-global state ever grows, OR if the screens ever use **`SharedArrayBuffer` / `Atomics`** (whose
   cross-agent waiter list *is* process-global, not per-`JSRuntime`). Cathode's single-worker model almost
   certainly never exercises that today — but the static-per-module layout would break the moment it did,
   with no compile-time signal.
4. **Bloat** — 3× the quickjs code linked across three module DLLs for no benefit.

The fix is one shared instance: a **single quickjs DLL** all three modules link against, so there is
exactly one copy of the runtime's code (and state) shared across the modules.

### Build instruction (the only per-lib change vs the static recipe)

```
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL ^
  -DCMAKE_CXX_STANDARD=20 ^
  -DBUILD_SHARED_LIBS=ON                         ^  # <-- the ONLY change: ON (shared), not OFF (static)
cmake --build build --config Release --target qjs
```

Same cross-cutting constraints as every other lib: **`/MD` (MultiThreadedDLL)**, **x64**, **Release**,
**C++20**, **VS2022**. With `BUILD_SHARED_LIBS=ON`, MSVC emits the export-decorated DLL plus an **import
library** (the `JS_EXTERN` API in `quickjs.h` resolves to `dllexport` when building the DLL; consumers
get `dllimport`). Verify the DLL actually **exports the `JS_*` symbols** (e.g. `dumpbin /exports
quickjs.dll`) — if quickjs-ng's headers gate the export macro behind a define, set it for the DLL build
so `JS_*` are exported.

### Required outputs (DEFINITIVE names — the Build.cs will reference these)

| Required output (what to call it) | CMake target | Lands at |
|---|---|---|
| `quickjs.dll` (runtime DLL) | `qjs` (shared) | `Source/ThirdParty/quickjs-ng/Win64/quickjs.dll` |
| `quickjs.lib` (**import** lib, not static) | `qjs` (shared) | `Source/ThirdParty/quickjs-ng/Win64/quickjs.lib` |
| `quickjs.h` (unchanged) | — | `Source/ThirdParty/quickjs-ng/include/quickjs.h` |

The import-lib name stays **`quickjs.lib`** (same as the superseded static row), so the existing
`PublicAdditionalLibraries` reference is unchanged in name — but it now points at an **import** lib
backed by `quickjs.dll`, not a static archive. The new artifact is **`quickjs.dll`** alongside it.

### Staging + linkage (for the Build.cs, after the DLL exists)

Once the tooling produces the DLL, the plugin's `*.Build.cs` change (held until then):

- **All three** quickjs-calling modules (`CathodeRuntime`, `CathodeBinding`, `CathodeDom`) link the
  **one** `quickjs.lib` import lib — **dropping the two existing static `quickjs.lib` links** (the
  Runtime+Binding static copies that are the current bug). One import lib, one shared `quickjs.dll`.
- **Stage the DLL:** `RuntimeDependencies.Add(".../Win64/quickjs.dll", StagedFileType.NonUFS)` so
  `quickjs.dll` ships next to the module binaries and is found at load.
- **Load choice (note for the Build.cs author):** either (a) **standard import-lib linkage** — the DLL
  auto-loads if it sits in the module `Binaries/Win64` dir next to the loader's search path (simplest;
  RuntimeDependencies + import lib, no delay-load); or (b) **delay-load** —
  `PublicDelayLoadDLLs.Add("quickjs.dll")` plus a manual `FPlatformProcess::GetDllHandle` at module
  startup pointed at the staged path, for explicit control of when/where the DLL loads. Pick (a) unless
  load-path control is needed.

### CRITICAL — the DLL **soname** must match the staged filename (sprint 010 load-time finding)

Renaming the DLL *file* is **not enough**: the import library carries the DLL's **embedded soname**
(the name the linked binaries put in their import table), and the loader looks for *that* name, not the
file's name on disk. In the first DLL build, the shared `qjs` target produced a DLL whose soname is
**`qjs.dll`**; the tooling renamed the *file* to `quickjs.dll` but the import lib still references
`qjs.dll`, so every Cathode module imported `qjs.dll` and failed to load (`GetLastError=126`, "module
could not be loaded") even though `quickjs.dll` was staged next to the binaries.

Two consistent fixes — **pick one and the tooling + Build.cs agree on it**:
- **(preferred — fully consistent naming)** build the shared target with CMake
  `set_target_properties(qjs PROPERTIES OUTPUT_NAME quickjs)` (or `RUNTIME_OUTPUT_NAME`/`OUTPUT_NAME`),
  so the **soname IS `quickjs.dll`**. Then file, soname, and the `quickjs.lib` reference are all
  `quickjs.dll`, and the Build.cs stages `quickjs.dll` (the originally-specified name). Everything reads
  "quickjs".
- **(interim, no rebuild — what the Build.cs does today)** leave the native soname `qjs.dll` and **stage
  the runtime DLL under that name** (`RuntimeDependencies.Add("$(BinaryOutputDir)/qjs.dll", <src
  quickjs.dll>)`) so the imported name resolves. Correct and immediate, but the deployed runtime DLL is
  `qjs.dll` while everything else reads "quickjs" — a cosmetic inconsistency.

The preferred fix is a one-line CMake property in the tooling's quickjs recipe; until/unless it's
applied, the Build.cs stages as `qjs.dll`.

