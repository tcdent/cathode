# Cathode

Cathode is an Unreal Engine 5.7 C++ plugin that runs HTML/CSS/JS "screens" and renders them as
textures onto objects in the world: monitors, terminals, wall dashboards, single-line device LCDs.

It is assembled from four small, permissively licensed libraries and authors no HTML engine,
JavaScript runtime, or CSS parser of its own:

| Concern | Library | License |
|---|---|---|
| JavaScript (ES2023, native `Promise`) | [quickjs-ng](https://github.com/quickjs-ng/quickjs) | MIT |
| HTML parse, CSS cascade, layout, hit-test | [litehtml](https://github.com/litehtml/litehtml) | BSD-3-Clause |
| Text shaping and measurement | [HarfBuzz](https://github.com/harfbuzz/harfbuzz) | Old MIT |
| CPU rasterization | [Blend2D](https://github.com/blend2d/blend2d) (+ [AsmJit](https://github.com/asmjit/asmjit)) | Zlib |

On top of those sits a thin Cathode-authored layer: a DOM surface bridging quickjs-ng to litehtml's
tree, a dedicated worker thread, the upload of rendered frames to `UTexture2D`, and a typed bridge
between screens and the host game.

**Cathode is a rendering surface for bundled, trusted screens, not a web browser.** There is no
networking, TLS, cookies, storage, history, or multi-tab. Leaving that apparatus out is what keeps
it small, in-process, with no Chromium and no second process.

Cathode was built for the game UPTIME and has no dependency on it. The design is described in
[`docs/browse/architecture.md`](docs/browse/architecture.md).

## Footprint

Embedding a browser engine means shipping one: an embedded Chromium (CEF) is roughly 150 MB at
minimum, because the expensive parts (Blink, V8, Skia, the compositor) are the parts that cannot be
removed. Cathode's entire third-party surface is four libraries, built from source:

| Library | Win64 Release artifact | Size |
|---|---|---|
| quickjs-ng | `quickjs.dll` (linked) | 0.95 MB |
| Blend2D + AsmJit | `blend2d.lib` (static archive) | 5.7 MB |
| litehtml + gumbo | `litehtml.lib`, `gumbo.lib` (static archives) | 14.7 MB |
| HarfBuzz | `harfbuzz.lib` (static archive) | 15.7 MB |

The static-archive sizes are before linking: they are what sits on disk at build time, and an
upper bound on what reaches a binary once the linker drops unreferenced code. The design target is
a linked runtime in the single-digit megabytes.

## Modern web development, lightweight

Screens are written the way web front ends are written now: TypeScript, components, a bundler.

- **The language is complete.** quickjs-ng implements ES2023, including native `Promise`,
  `async`/`await`, classes, and the rest of the modern language. Screens are pre-bundled to a single
  file, so there is no module loading at runtime.
- **The platform is a deliberate subset.** What Cathode narrows is the DOM and web API surface,
  not JavaScript. The supported DOM was scoped by measuring what [Preact](https://preactjs.com)'s
  renderer actually calls, then implementing each of those members to its specification
  ([`docs/browse/components-dom-surface.md`](docs/browse/components-dom-surface.md)).
- **Preact runs on it.** Preact is the React-compatible library with the same component and hooks
  model in about 11 kB (the unmodified 10.29.2 UMD build is 11.4 kB). Cathode's test suite mounts
  a component through that genuine upstream build and checks the resulting DOM. That proof covers
  the mount path with a static component; it does not yet exercise hooks or event handlers.
- **The subset is enforced at author time.** A screen that reaches for an API outside the surface
  fails to compile; see [Authoring screens](#authoring-screens).

## Status

Experimental (`0.1.0`).

- **Win64 only.** Every module links its third-party library under a Win64 gate, and the render
  and texture-upload tests fail by design on other platforms rather than pass vacuously.
- **You build the four libraries yourself.** Their source and binaries are not in this repository;
  see [Build the libraries](#build-the-libraries).
- **The `game.*` host API is a reference shape, not a settled contract.** `CathodeExample` shows
  how a host registers one.
- **CI covers the authoring gate only.** The Unreal automation suite needs a Windows machine with
  the engine installed and is not run in CI.

What is planned next is in [`docs/roadmap.md`](docs/roadmap.md).

## How it works

- **One shared JS runtime, many surfaces.** Each display is a surface: one litehtml document with
  its own output texture. All surfaces read the same global JS state, so a button pressed on one
  device can change what a dashboard across the room shows, with no plumbing between them.
- **Off the game thread.** JS, DOM mutation, layout, shaping, and rasterization run on a single
  dedicated worker thread. The game thread only pumps the texture upload once per frame.
- **Screens are authored against a checked subset.** The supported DOM/JS surface and CSS property
  set are generated from the engine's own bindings, and an author-side build step rejects anything
  outside them before a screen ever reaches the engine.

## Requirements

- Unreal Engine 5.7, Windows x64, Visual Studio 2022
- CMake and Git (to fetch and build the libraries)
- Node.js (only for the authoring gate; not needed on a machine that just builds the plugin)

## Install

The plugin is the `Cathode/` directory of this repository. Clone the repository, put that directory
in your project's `Plugins/` (copy it, or link it), and enable the plugin in your `.uproject` (it is
`EnabledByDefault: false`):

```sh
git clone https://github.com/tcdent/cathode
```

```json
{ "Name": "Cathode", "Enabled": true }
```

### Fetch the libraries

```sh
bash Cathode/install.sh
```

Run from the repository root, this clones each library at a pinned commit into
`vendor/lib/cathode/<lib>/` (gitignored, outside the plugin directory) and verifies `HEAD` against
the pin.

| Library | Pin |
|---|---|
| quickjs-ng | `v0.15.1 @ fd0a0210b7be00957751871e7e01b8291268fc29` |
| litehtml | `v0.9 @ 6ca1ab0419e770e6d35a1ef690238773a1dafcee` |
| HarfBuzz | `14.2.1 @ 56feae4035bdd48f62ba2b8d8c16232d4d89b3a4` |
| Blend2D | `master @ 6dbc2cefbc996379e07104e34519a440b49b15d7` |
| AsmJit | `master @ 0bd5787b54b575ed94bf32ac452153b34385c514` |

### Build the libraries

The plugin's `Build.cs` files expect headers and Win64 libraries at
`Cathode/Source/ThirdParty/<lib>/{include,Win64}/`. Those directories are gitignored build outputs. This
repository does not ship a build script for them; the recipe below is the one the plugin was
developed against, and [`docs/browse/vendor-lib-build.md`](docs/browse/vendor-lib-build.md) covers the
constraints in detail.

First apply the local Blend2D patch. The pinned Blend2D under-allocates in
`bl_array_assign_data`, which is a heap overflow on any copy into a fresh `BLArray`:

```sh
git -C vendor/lib/cathode/blend2d apply "$PWD/Cathode/patches/blend2d-assign-data-underalloc.patch"
```

Then configure each library with the Visual Studio 2022 generator, x64, Release, the dynamic CRT,
and C++20, which is what Unreal links against:

```
-G "Visual Studio 17 2022" -A x64 -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DCMAKE_CXX_STANDARD=20
```

| Library | Extra configure flags | Targets | Copy to `Cathode/Source/ThirdParty/` |
|---|---|---|---|
| quickjs-ng | `-DBUILD_SHARED_LIBS=ON` | `qjs` | `qjs.dll` → `quickjs-ng/Win64/quickjs.dll`, `qjs.lib` → `quickjs-ng/Win64/quickjs.lib`; `quickjs.h` → `quickjs-ng/include/` |
| litehtml | `-DBUILD_SHARED_LIBS=OFF -DLITEHTML_BUILD_TESTING=OFF` | `litehtml`, `gumbo` | `litehtml.lib`, `gumbo.lib` → `litehtml/Win64/`; `include/litehtml.h` and `include/litehtml/` → `litehtml/include/` |
| HarfBuzz | `-DBUILD_SHARED_LIBS=OFF -DHB_HAVE_FREETYPE=OFF -DHB_HAVE_ICU=OFF -DHB_HAVE_GLIB=OFF -DHB_BUILD_SUBSET=OFF -DHB_BUILD_UTILS=OFF` | `harfbuzz` | `harfbuzz.lib` → `harfbuzz/Win64/`; `src/hb*.h` → `harfbuzz/include/` |
| Blend2D | `-DBLEND2D_STATIC=TRUE -DASMJIT_DIR=<path to asmjit>` | `blend2d` | `blend2d.lib` → `blend2d/Win64/`; `blend2d/blend2d.h` → `blend2d/include/blend2d.h`, the `blend2d/` tree → `blend2d/include/blend2d/`; AsmJit's `asmjit/` tree → `asmjit/include/asmjit/` |

quickjs-ng is built as a shared library because several plugin modules share one `JSRuntime`.
AsmJit is embedded in `blend2d.lib` and has no library of its own.

## Using it from a host project

The host-facing entry point is `Cathode::FCompositionSurfaceOwner` in the `CathodeComposition`
module. A host depends on `CathodeComposition` (and `CathodeSurfaces` for the id and input types),
holds one owner, and drives it from the game thread:

```cpp
#include "CathodeCompositionOwner.h"

Cathode::FCompositionSurfaceOwner Owner;

// One spec per display: an author bundle and its pixel size.
TArray<Cathode::FCompositionSurfaceSpec> Specs;
Specs.Add({ TEXT("status-card.iife.js"), FString(), 640, 360 });

Owner.Install(Specs, FontFilePath);            // or pass a TUniquePtr<ICathodeGameBridge> as a third argument

// Every frame, on the game thread:
Owner.Pump();
UTexture2D* Texture = Owner.GetTexture(Owner.GetSurfaceId(0));   // null until the first frame lands
```

- **Bundles** are read from `<Project>/Content/Cathode/` (the `/Game/Cathode` mount), by path
  relative to that root.
- **Fonts** are supplied by the host as a font file path at install.
- **Display** is the host's: sample the texture from a material on whatever mesh you like.
  `ACathodeExampleDeviceActor` binds it to a dynamic material instance through a texture parameter
  named `SurfaceTexture`.
- **Input** is delivered as intents. The host designates one active surface and calls
  `DeliverIntent` with `Up`, `Down`, `Left`, `Right`, `Accept`, or `Back`; Cathode maps the intent
  to a DOM `keydown` on that surface.
- **Game state** reaches screens through `Cathode::ICathodeGameBridge`, which the host implements
  and Cathode exposes to author JS as the `game` global.

Two modules exist as worked references and depend on nothing but Cathode and the engine:

- `CathodeExample`: `FExampleGameBridge`, a host object registered into the `game.*` bridge.
- `CathodeExampleDevice`: `ACathodeExampleDeviceActor`, an actor that shows a surface on a mesh
  and can be targeted for input.

## Authoring screens

A screen is a `screen.ts` plus its own `screen.css`. It mounts into `document.body`:

```ts
const card = document.createElement("div");
card.setAttribute("class", "greeting");
card.textContent = "Hello, Cathode";
document.body!.appendChild(card);
```

The `Cathode/Authoring/` directory is a self-contained Node package that checks and bundles screens:

```sh
cd Cathode/Authoring
npm ci
npm run gate
```

- `lib.cathode.d.ts` is the DOM and JS type surface. Screens compile against it *instead of* the
  standard DOM lib, so an unsupported web API is a TypeScript error.
- `css-allow-list.toml` is the supported CSS property set. A linter rejects any property outside it.
- A screen that passes is bundled to the IIFE that Cathode loads.

Both files are generated from the engine (the binding tables and litehtml's property names) and a
test asserts the committed copies match a fresh generation, so the gate and the engine cannot
drift. `screens/hello/` and `screens/status-card/` are conformant examples; `screens/banned/` shows the
gate rejecting a screen.

## Modules

| Module | Role |
|---|---|
| `CathodeRuntime` | the quickjs-ng runtime, the worker thread, timers, the host message queue |
| `CathodeBinding` | native class registration and value marshaling between C++ and JS |
| `CathodeDom` | the DOM exposed to author JS, the `game.*` bridge, authoring-surface generation |
| `CathodeLayout` | litehtml integration: cascade, layout, paint routing, selectors |
| `CathodeText` | HarfBuzz shaping, font registry, text metrics |
| `CathodeRasterizer` | Blend2D rasterization, buffers, dirty regions |
| `CathodeResolver` | resource loading for bundled content |
| `CathodeSurfaces` | the surface registry, frame lifecycle, input intents |
| `CathodeEgress` | upload of rendered frames to `UTexture2D` through the RHI |
| `CathodeComposition` | the composition root; `FCompositionSurfaceOwner` |
| `CathodeVerify` | runtime self-verification of the JS environment |
| `CathodeExample`, `CathodeExampleDevice` | reference host integrations |

Modules live under `Cathode/Source/`. Each module with a `COVERAGE.md` lists what it implements and which test proves it.

## Tests

The plugin carries 143 Unreal automation tests under the `Cathode.` prefix. Run them from the
editor's Session Frontend, or headless:

```
UnrealEditor-Cmd <Project>.uproject -ExecCmds="Automation RunTests Cathode; Quit" -unattended
```

The texture-upload and composed-render tests read pixels back from the GPU, so they need a real
RHI (do not pass `-nullrhi`).

## Documentation

- [`docs/browse/`](docs/browse/) holds the design documents:
  [`architecture.md`](docs/browse/architecture.md), a `components-*.md` per subsystem, the CSS
  contract, and the testing approach.
- [`docs/teams/cathode-core/`](docs/teams/cathode-core/) is the sprint record the plugin was built
  under. Design documents and commit messages cite sprints and slices by the numbers used there.
- [`docs/roadmap.md`](docs/roadmap.md) lists what is deferred.

Most of this was written while Cathode lived inside the UPTIME repository, and the git history
before the extraction commit is that repository's history for these paths.

## License

MIT; see [`LICENSE`](LICENSE). The four libraries, AsmJit, and the two committed test fixtures
(Liberation Sans and Preact) keep their own licenses; [`THIRD_PARTY_NOTICES`](THIRD_PARTY_NOTICES)
reproduces each notice in full.
