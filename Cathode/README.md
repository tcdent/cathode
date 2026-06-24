# Cathode

Cathode is an Unreal Engine 5.7 C++ plugin that runs first-party HTML/CSS/JS "screens" and
renders them as textures onto in-world devices — rack KVMs, crash-cart laptops, wall dashboards,
single-character device LCDs. It is **assembled from four small, permissively-licensed
libraries** and authors no HTML engine, JavaScript runtime, or CSS parser of its own. It is a
rendering surface for bundled, trusted screens — **not a web browser** (no networking, TLS,
cookies, storage, history, or multi-tab).

Cathode is MIT and has no dependency on `Source/Uptime`, so it remains a reusable Unreal plugin.
The full design is in [`docs/browse/architecture.md`](../../../docs/browse/architecture.md).

> **Status: scaffold (sprint 001).** This is the on-disk skeleton only. Nothing here is built,
> linked, or activated — the plugin is **not** enabled and is invisible to the shared Unreal
> build (see [Quarantine](#quarantine)). Integration is sprint 002; the authoring contract is
> sprint 003.

## Layout

```
game/Plugins/Cathode/
  Cathode.uplugin                       # descriptor — EnabledByDefault:false, declares CathodeRuntime
  install.sh                            # reproduces the vendored library clones (see below)
  README.md                             # this scaffold map
  Authoring/                            # the build-time authoring gate (GENERATED from the engine — Model A, 025)
    lib.cathode.d.ts                    #   DOM + JS type surface (generated from the FBoundMember bindings; 025 S1)
    css-allow-list.toml                 #   supported CSS properties (generated from litehtml string_id.h; 025 S2)
  Source/
    CathodeRuntime/                     # the runtime module (scaffold; not compiled in 001)
      CathodeRuntime.Build.cs           #   deps: Core, CoreUObject, Engine, RHI, RenderCore, Slate; ThirdParty block stubbed
      Public/CathodeRuntimeModule.h
      Private/CathodeRuntimeModule.cpp  #   IMPLEMENT_MODULE(FCathodeRuntimeModule, CathodeRuntime)
    ThirdParty/                         # SDL2-shaped per-lib homes; populated at 002 from source builds
      quickjs-ng/  litehtml/  harfbuzz/  blend2d/  asmjit/

vendor/lib/cathode/                     # the library SOURCE — gitignored, reproduced by install.sh
  quickjs-ng/  litehtml/  harfbuzz/  blend2d/  asmjit/
```

## The libraries

Cathode is assembled from four libraries, plus Blend2D's build dependency (AsmJit). Their source
is **not committed** — `vendor/` is gitignored. `install.sh` clones each at a pinned commit
(retaining `.git`/origin so a future version bump is `fetch` → pick a newer tag → update the pin →
re-run). Identity of each lib is its commit, verified by `git rev-parse HEAD == pin` at install.

| Library | Role | License | Pin (`tag @ commit`) |
|---|---|---|---|
| [quickjs-ng](https://github.com/quickjs-ng/quickjs) | JavaScript language (ES2023, native Promise) | MIT | `v0.15.1 @ fd0a0210b7be00957751871e7e01b8291268fc29` |
| [litehtml](https://github.com/litehtml/litehtml) | HTML parse + CSS cascade + layout + hit-test | BSD-3-Clause | `v0.9 @ 6ca1ab0419e770e6d35a1ef690238773a1dafcee` |
| [HarfBuzz](https://github.com/harfbuzz/harfbuzz) | text shaping + measurement (kerning, ligatures, OpenType) | Old MIT | `14.2.1 @ 56feae4035bdd48f62ba2b8d8c16232d4d89b3a4` |
| [Blend2D](https://github.com/blend2d/blend2d) | CPU rasterization of glyph runs, boxes, images | Zlib | *(no release tag)* `master @ 6dbc2cefbc996379e07104e34519a440b49b15d7` |
| [AsmJit](https://github.com/asmjit/asmjit) | Blend2D's JIT pipeline compiler (build dependency) | Zlib | *(no release tag)* `master @ 0bd5787b54b575ed94bf32ac452153b34385c514` |

Reproduce the staged source on any checkout:

```sh
bash game/Plugins/Cathode/install.sh
```

It is idempotent: an existing clone is fetched and re-checked-out to its pin; a missing one is
cloned fresh. See the script's header for the version-bump procedure.

## Quarantine

This scaffold cannot be seen, picked up, or broken by the shared Unreal build:

- `Cathode.uplugin` carries an **explicit** `"EnabledByDefault": false` — the deliberate quarantine
  choice, correct regardless of how the engine resolves an unset field. (The exact UE 5.7
  `IsEnabledByDefault` semantics for a *project*-loaded plugin with the field omitted is the one open
  confirmation 002 inherits; optionally closable via an `Engine/Source/Runtime/Projects` grep. We do
  not rely on that behavior — the explicit `false` settles quarantine either way.)
- Cathode is **absent** from `game/uptime.uproject` (`Plugins[]` and `Modules[]`); the `.uproject`
  is untouched.
- The descriptor references only the `CathodeRuntime` module, which has a real source directory and
  `Build.cs`, so project generation parses cleanly.

## Authoring gate

A Cathode screen is standard HTML/CSS/JS targeting a documented subset; the build **hard-fails** on
anything outside it. The gate is built from existing toolchains, driven by one source:

- **Model A (025):** the gate artifacts are **generated from the engine's own interfaces** — there is
  no hand-authored manifest. `lib.cathode.d.ts` is projected from the `FBoundMember` binding tables
  (025 S1); `css-allow-list.toml` is projected from litehtml's `string_id.h` CSS-property names minus
  the 7 audited exclusions plus the 2 cathode_computed props (025 S2). Each is regenerated by a
  cvar-gated automation test and **coherence-guarded** (committed == regenerated), so a binding /
  litehtml change flows through and the author gate and the engine cannot drift.
- `lib.cathode.d.ts` — author apps compile against this *without* the standard DOM lib, so the
  TypeScript compiler flags any unsupported API.
- `css-allow-list.toml` — a small linter validates author CSS against it.

In 001 these three are **structure-only skeletons**; the real surface is authored in sprint 003.
The `game.*` host API is intentionally unspecified until the simulation's shape is known.

## Integration-deferred (sprint 002)

Everything below is explicitly out of scope for the scaffold and owned by the integration sprint:

- **Build the five libraries from source** and produce platform binaries (headers + libs into
  `Source/ThirdParty/<lib>/`, binaries via Git LFS).
- **Uncomment the `Build.cs` ThirdParty block** — the include paths and the platform-gated
  `PublicAdditionalLibraries` / `RuntimeDependencies` / `PublicDelayLoadDLLs` lines — once those
  binaries exist.
- **Activate the plugin** — move it into the build's reach and enable it (the `.uproject` reference
  or `EnabledByDefault`), making the first compile an explicit, owned event.
- **Relocate AsmJit** to `vendor/lib/cathode/blend2d/3rdparty/asmjit` (or set `ASMJIT_DIR`) so
  Blend2D's JIT build finds it.
