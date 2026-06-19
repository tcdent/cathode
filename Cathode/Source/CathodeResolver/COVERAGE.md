# Cathode Resource Resolver — component coverage & accountable deferrals

Durable accountability record for the Resource Resolver (sprint 009, no-build/no-run scaffold). It maps every
responsibility in [`docs/browse/components-resource-resolver.md`](../../../../../docs/browse/components-resource-resolver.md)
to its status: **COVERED** (slice + mechanism + proof test) or **DEFERRED / RESOLVED-OPEN-ITEM** (with
reason · dependency · unblocker · first-execution risk). Nothing in the component doc is silently missing.

This file is the accountability of record. Every in-tree reference (file / type / test) resolves.

The resolver is **small + pure-bespoke** — it wraps no vendor and **couples-to-none** (CoreMinimal + UniquePtr
+ StringConv only): it DEFINES the neutral seams (`IBundleSource`, `IResourceResolver`) and the URL-namespace
logic; 005 (litehtml resource callbacks), 002 (author-JS bundle), and 004 (url-addressed font bytes) are the
CONSUMERS. **No network I/O, ever.**

## Covered

| Responsibility | Slice | Mechanism | Proof |
|---|---|---|---|
| **Resolve layout resources** (images, linked CSS, fonts → bundled content) | S1 + S2 | `IResourceResolver::ResolveBytes` (images, url-fonts) / `ResolveText` (linked CSS) over `IBundleSource`; the **005 container hook** routes `load_image`/`get_image_size`/`import_css` to it via the litehtml-free `CathodeResourceRouting` helpers (the CathodePaintRouting precedent) | `Cathode.Layout.Resource.ContainerRouting` |
| **Resolve JS/module loads** (author JavaScript from the bundle) | S2 | the author-JS bundle is `ResolveBytes`(bundle-url) → bytes → 002 `Evaluate`s it as a Module. No module-loader hook (single pre-bundled file). The 002 `Evaluate` wiring is integration. | `Cathode.Resolver.Resolve.HitMiss` (the bytes path) |
| **Define the URL namespace** (virtual root; ordinary relative URLs) | S2 | `ResolveUrlPath` — `/`-rooted → virtual root; relative → vs the document/stylesheet base; collapse `./`+`../`; `..` clamps at root; root-relative output. **No custom author scheme.** | `Cathode.Resolver.Url.Normalization` |
| **The bundle backend seam** (url → bytes) | S1 + S2 | `IBundleSource` (hit/miss). The resolver's logic is backend-agnostic; the UE-asset backend is integration; an in-memory source backs the tests. | `Cathode.Resolver.Resolve.HitMiss` |
| **Boundaries** (serves 005 + the JS loader; no network; doesn't decide bundling) | by design | couples-to-none; the resolver reads from `IBundleSource`, never packages; no network I/O | — |

## Production read-mechanism status (sprint 019 — resolved)

The first PRODUCTION `IBundleSource` resolves a URL to **real file bytes**. `FUEFileBundleSource`
(`CreateUEFileBundleSource(RootDir)`, `Private/CathodeFileBundleSource.cpp`) reads via
`FFileHelper::LoadFileToArray` over a **supplied (REQUIRED) RootDir** — `Read(path)` -> `RootDir/path` -> the real
file; `false` + empty `OutBytes` on a clean miss (the `FFontRegistry` `CreateFontRegistry`->`LoadFileToArray`
precedent, `CathodeText/Private/CathodeFontRegistry.cpp:43-50`). NOT `UAssetManager` (raw `.html/.css/.png` are not
UObjects — UFS non-asset, per the audit correction below).

| Proof | What |
|---|---|
| `Cathode.Resolver.FileSource.RealBytes` (019 s1) | a GENUINE `FFileHelper` disk read of the NEW backend: `Read("probe.txt")` -> `OutBytes ==` a HARDCODED 35-byte marker (full-array `==`, NON-circular oracle, NOT `Contains`, not echoable from the path); a missing path -> `Read==false` + empty (the clean miss) |
| `Cathode.Resolver.FileSource.ResolverOverProduction` (019 s2) | the goal END-TO-END: `CreateResourceResolver(CreateUEFileBundleSource(...))` -> `ResolveBytes("../probe.txt","sub/index.html")` -> `ResolveUrlPath` (BaseDir + relative-join + `..` collapse) -> `"probe.txt"` -> the production read -> byte-exact `==` the marker (a NON-trivial URL: URL `!=` the resolved path, a genuine normalization, not a passthrough); a miss-URL -> `false` |

The committed fixture (`Private/Tests/Fixtures/probe.txt`, located via `IPluginManager`->`GetBaseDir()`) is
**checkout-stable** (single-line, NO trailing newline, 35 bytes / 0 CR; a scoped `.gitattributes -text` overrides
the repo-root `* text=auto eol=lf`) so the byte-exact match does not false-RED on the WINDOWS build box (git
autocrlf) — proven on the box (35 bytes, 0 CR). The test's `Projects` dep (IPluginManager) is non-Shipping /
test-only (the CathodeDom convention); a Shipping `CathodeResolver` stays Core-only.

**The DEPLOYMENT is deferred-TOGETHER (the coupled integration item).** 019 ships the read MECHANISM with a
configurable RootDir and **NO baked production default-root**. The production default-root (`/Game/Cathode/`) + the
`DirectoriesToAlwaysStageAsUFS` staging config + the cooked-pak read validation (Deferred #3's project-setting
plumbing + #5's cooked-staging) ship **together** at integration — shipping a content root the cook cannot stage
would be incoherent, so 019 ships neither. The **container wiring** (`SetResourceResolver` into the live litehtml
container) is **integration-deferred** (the 014/018 layout/render analog; no production caller — null = the
pre-hook clean-miss stub; the routing is already tested via a stub resolver).

**Scaffold-prose coherence note:** the pre-existing sprint-009 prose (`CathodeResolver.h:23, 65, 68`) describes the
EVENTUAL integration default-root (`ProjectContentDir()/<root>`); 019's backend takes a **configurable RootDir** (no
baked default). That prose documents the deferred-integration intent (accurate), NOT a 019 baked default — the
default-root is the deferred factory.

## The doc's three open items — resolved

- **Virtual URL scheme/root** → a **configurable root**, default **`/Game/Cathode/`** (expected to be changed by
  most users), **no custom author scheme** (ordinary relative / `/`-rooted web URLs). The resolver works in
  root-relative virtual space; the root lives in the `IBundleSource` backend.
- **Where assets live + how addressed** → the **UE content directory + UFS staging + `FFileHelper`**, RAW bytes
  (images bundled as raw `.png`, decoded by Blend2D — **NOT** `UAssetManager`/`UTexture2D`). *Audit correction:*
  "Unreal Asset Management" (the `UAssetManager` Primary-Asset API) is **UObject-only**; raw web files
  (`.html`/`.css`/`.js`) aren't UObjects, so they're staged as non-asset files (`DirectoriesToAlwaysStageAsUFS`)
  and read via `FFileHelper` at `ProjectContentDir()/<root>/path` — the backend's concern (root-in-Source).
  A `UTexture2D` would drop the original encoded bytes on cook, so images stay raw `.png`.
- **Caching of resolved resources** → **DEFERRED** (below).

## Deferred — with accountability

### 1. The 002 JS module-loader hook
- **Reason:** author JS is one pre-bundled file → 002 `Evaluate`s it as a Module; no `JS_SetModuleLoaderFunc` is needed.
- **Dependency / unblocker:** multi-module / dynamic-`import` becoming real (then 002 gains a module-loader hook pointing at the resolver).
- **First-execution risk:** none now — a single bundle needs no loader.

### 2. The 004 font wiring
- **Reason:** 004 owns the font system (`RegisterFontFile` + the byte-store + shaping); the resolver is only the generic url→bytes source *behind* the read. Don't duplicate 004.
- **Dependency / unblocker:** the integration sprint (route 004's font-file read through the resolver).
- **First-execution risk:** url-addressed `@font-face` fonts aren't resolved until that wiring lands; locally-registered fonts are unaffected.

### 3. The UE-asset `IBundleSource` backend — RESOLVED (read mechanism, sprint 019); deployment deferred-together
- **RESOLVED:** `FUEFileBundleSource` / `CreateUEFileBundleSource(RootDir)` reads real bytes via `FFileHelper` over a
  supplied RootDir (proven: `Cathode.Resolver.FileSource.RealBytes` + `…ResolverOverProduction`). See **Production
  read-mechanism status** above. The resolver logic was reused (not re-proven), now over the production source.
- **Still deferred (coupled with #5 — the DEPLOYMENT):** the production default-root (`/Game/Cathode/`) + the
  project-setting (`UDeveloperSettings`) plumbing + the `DirectoriesToAlwaysStageAsUFS` staging config — shipped
  **together** with #5's cooked-pak validation. 019 ships NO baked default-root (the configurable RootDir is the
  whole contract).
- **Container wiring:** `SetResourceResolver` into the live litehtml container is integration-deferred (no production
  caller — the 014/018 analog).

### 4. Caching of resolved resources
- **Reason:** bundled bytes are already in memory / the UE asset system caches; a resolver-level cache is likely redundant ceremony.
- **Dependency / unblocker:** a measured repeated-resolution cost.
- **First-execution risk:** redundant re-reads if a resource is resolved repeatedly; correctness unaffected.

### 5. Cooked-pak UFS-staging validation — deferred-TOGETHER with #3's default-root (the coupled integration item)
- **Reason:** the read MECHANISM is proven in the EDITOR (a genuine `FFileHelper` disk read of a committed loose
  fixture, sprint 019 — `FileSource.RealBytes`/`ResolverOverProduction`); but real `FFileHelper` reads of
  UFS-STAGED content only run in a COOKED build. UFS staging (`DirectoriesToAlwaysStageAsUFS`) has version-dependent
  reliability reports; the editor reads the loose source files regardless, which can MASK staging bugs. This couples
  with #3's production default-root + staging config — shipped together (a default-root the cook can't stage is
  incoherent, so neither ships in 019).
- **Dependency / unblocker:** the integration pass — the default-root + the staging config + verify in a real cooked
  build (not editor/PIE).
- **First-execution risk:** a resource present in editor but not staged into the pak would 404 only in a packaged build.

### 6. The decode-for-size seam-ownership (RQ1) — integration open
- **Reason:** `get_image_size` needs image dimensions; the resolver returns BYTES only (couples-to-none → no decode), so `RouteImageSize` returns 0×0 this sprint. WHO decodes the bytes for the size (the resolver via a 003 seam / 003 directly / the 005 container) is **undefined** — an integration open recorded here so the 0×0 does not silently become permanent.
- **Dependency / unblocker:** the integration sprint defining a decode-for-size seam (likely 003/Blend2D image-info over the resolver's bytes).
- **First-execution risk:** images lay out at 0×0 until the decode-for-size path is wired.

### 7. `RouteImageSize` resolve-then-discard tidy — integration
- **Reason:** `RouteImageSize` currently routes to the resolver (proving the hook is wired) then DISCARDS the bytes and returns 0×0 — a forward-wiring placeholder that does not gate on hit/miss.
- **Dependency / unblocker:** fold it into the decode-for-size path (#6) — decode the resolved bytes → real dimensions.
- **First-execution risk:** a redundant resolve whose bytes are dropped until #6 lands; no incorrectness.

## The 005 evolution (the blessed, surgical hook)

005 `CathodeLayout` changed ONLY by the resolver hook — its existing files are byte-identical except: (a) the
`CathodeResolver` Build.cs dependency; (b) a forward-declared `IResourceResolver` + the non-pure-default
`ILayoutEngine::SetResourceResolver(IResourceResolver*)` (null == the pre-hook stub; keeps 008's mock
implementers byte-identical); (c) `FLiteHtmlLayoutEngine` overrides it + threads the resolver to the
backend/container; the container's three `TODO(Resource Resolver)` stubs now delegate to the new litehtml-free
`CathodeResourceRouting` helpers. The other accepted modules (002/003/004/006/007/008) are byte-identical.
