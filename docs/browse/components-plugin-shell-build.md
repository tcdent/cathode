# Cathode — Plugin Shell & Build (detail)

Detail for the **Plugin Shell & Build** component (see `components.md`). What/why, not how. The
repo's existing plugin conventions are grounding.

This is the Unreal plugin packaging and build wiring — the foundation every other component
compiles and ships within. It is what makes Cathode a buildable, reusable Unreal plugin.

## Responsibilities

**Package the plugin.** The `.uplugin` (modules, loading phases, platform allow-list) and the
`Build.cs` (module dependencies, include paths, link libraries, runtime dependencies). *Why:* this
is what Unreal Build Tool discovers and compiles; nothing runs without it.

**Vendor the dependencies.** Stage the four libraries — quickjs-ng, litehtml, HarfBuzz, Blend2D —
plus **AsmJit alongside Blend2D**, and wire their headers/libs into the build. *Why:* the runtime
is assembled from these; AsmJit specifically is *not* in Blend2D's git repo (only in its source
releases), so it must be vendored explicitly. *Ground:* the JoystickPlugin/SDL2 pattern
(headers + platform libs; `PublicAdditionalLibraries`, `RuntimeDependencies`,
`PublicDelayLoadDLLs`); the `cpp_httplib` header-only `ModuleType.External` pattern; LFS for
binaries; `docs/dev/toolchain.md` policy.

**Wire modules and enablement.** Declare the plugin's module(s) and enable it in
`game/uptime.uproject`. *Why:* Unreal must know the plugin exists and load it.

**Follow repo conventions.** Match how the project already vendors native libraries and builds
(the Windows box over SSH). *Why:* consistency with the existing toolchain keeps it maintainable.

## Key concern — dependency acquisition

How the four libraries' source is obtained and built is this component's central decision:
recommended approach is **git-submodule the sources and build from source to static libs** (matches
the "sub-repos for dependencies" intent and stays version-pinned), linked via `Build.cs`.
Alternatives: vendored copied source, or prebuilt libraries (the SDL2 shape, but needs a
per-platform build first). Ship Blend2D **with JIT** (the no-JIT path is incomplete).

## Boundaries

Owns packaging, the build, and vendoring. Not runtime behavior. It is the substrate the other
twelve components compile within. Cathode depends only on Unreal modules (RHI, RenderCore, Slate)
and the bundled libraries — never on `Source/Uptime`.

## Open / deferred (team's call)

- Dependency acquisition mechanism (submodule-source-and-build recommended).
- Module split (a single runtime module vs more) and loading phases.
