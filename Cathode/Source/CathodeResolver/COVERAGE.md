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

### 3. The UE-asset `IBundleSource` backend
- **Reason:** the resolver stays backend-agnostic behind `IBundleSource`; the UE-asset impl (content-dir + UFS + `FFileHelper`, rooted at the configurable `/Game/Cathode/`) is integration.
- **Dependency / unblocker:** the integration sprint + the project-setting plumbing (a `UDeveloperSettings`/value → the root).
- **First-execution risk:** no resource resolves to real bytes until a backend is wired; the in-memory source proves the logic.

### 4. Caching of resolved resources
- **Reason:** bundled bytes are already in memory / the UE asset system caches; a resolver-level cache is likely redundant ceremony.
- **Dependency / unblocker:** a measured repeated-resolution cost.
- **First-execution risk:** redundant re-reads if a resource is resolved repeatedly; correctness unaffected.

### 5. Real bytes / UE-asset load + UFS-staging-in-a-COOKED-build — validation-pass
- **Reason:** no build runs; the in-memory source + the routing helpers are tested, but real `FFileHelper` reads of UFS-staged content only run in a cooked build. UFS staging (`DirectoriesToAlwaysStageAsUFS`) has version-dependent reliability reports; the editor reads the loose source files regardless, which can MASK staging bugs.
- **Dependency / unblocker:** the execution/validation pass — verify in a real cooked build (not editor/PIE).
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
