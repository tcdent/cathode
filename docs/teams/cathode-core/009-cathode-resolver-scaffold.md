# Sprint 009 — Cathode Resource Resolver scaffold

## Nature

A **no-build / no-execution scaffold** sprint, continuing the per-component house pattern (002–008).
The component is the **Resource Resolver** (`docs/browse/components-resource-resolver.md`) — the one
place that maps logical URLs to bundled content, so neither the layout engine nor the JS loader ever
needs an HTTP server. It is **small and pure-bespoke**: it wraps no vendor and **couples-to-none**
(it defines neutral seams; 005/002/004 are the consumers). It does **no network I/O**.

It serves three consumers: it **backs 005's litehtml resource callbacks** (image / linked-CSS / font —
which are TODO stubs today), provides the **author-JS bundle bytes** to 002, and is the generic
**url→bytes** byte-source 004 consumes for url-addressed fonts.

## Scope Boundary

**This sprint owns:**
- **URL→bundle resolution** — the URL-namespace mapping/normalization logic (relative vs a document/
  stylesheet base; `/`-rooted vs the virtual root) over a neutral byte source;
- the **neutral seams** it backs — a `IBundleSource` (logical-URL → bytes) and the resource-resolution
  seam 005's `document_container` calls;
- the **005 container hook** — the blessed evolution that replaces 005's `TODO(Resource Resolver)`
  stubs with calls into the resolver.

**This sprint does NOT own** (named, with the owner):
- **bundling / packaging** — the resolver reads from wherever assets are bundled; the UE content-dir +
  UFS staging is the packaging concern;
- the **UE-asset `IBundleSource` backend** — built at **integration** behind the neutral seam (the
  resolver stays backend-agnostic + testable via an in-memory source);
- **network I/O** — none, ever;
- the **002 JS module-loader hook** — deferred (author JS is one pre-bundled file; 002 `Evaluate`s it
  as a Module — no `JS_SetModuleLoaderFunc` needed until multi-module / dynamic-import is real);
- **004's font system** — 004 owns `RegisterFontFile` + the font byte-store + shaping; the resolver is
  only the byte source *behind* the read (the 004↔resolver wiring is integration).

**Sibling evolution:** 005 `CathodeLayout` gains the resolver hook in its `document_container`
(the analog of 007's one-accessor evolution of 002) — `load_image` / `get_image_size` / `import_css`
route to the resolver. 005's rest stays byte-identical.

## Problem

005's litehtml resource callbacks are **TODO stubs / no-ops** (`CathodeLayoutEngine.cpp`:
`get_image_size` → 0×0, `load_image` no-op, `import_css` no-op, each commented `TODO(Resource
Resolver)`); litehtml cannot load images, linked CSS, or fonts. Author JS must come from the bundle.
No URL→bundle mapping exists. An internal HTTP server to serve files already held in memory would be
pure ceremony.

## Goal

The one place that maps logical URLs to bundled content — backing 005's resource callbacks, providing
author-JS bytes to 002, and sourcing url→bytes for 004's fonts — authored as bespoke URL-namespace
logic over a neutral `IBundleSource`, backend-agnostic, with the UE backend deferred to integration.

On the asset location:

> "keep in mind how Unreal Engine developers target assets via paths. They expect to start at a certain
> directory … standardize on a location that a game developer implementing the system would expect. If
> I put assets in that location, the developer should know I can get to them in the browser."

> "We save a lot of the nuance by making the path configurable. We can default it to content/cathode,
> and then we expect most users to change it."

A grounding correction the audit surfaced: the human's "Unreal Asset Management" intent is honored by
the **UE content directory + UFS staging + file APIs** (`FFileHelper`), **not** the `UAssetManager` /
Primary-Asset system — that is **UObject-only** and raw web files (`.html`/`.css`/`.js`) are not
UObjects, so they are not addressable through it. All web resources are **raw bytes** (images bundled
as raw `.png`, not `UTexture2D` — cook recompresses a texture to a GPU format and drops the original
encoded bytes), decoded by Blend2D.

## Final State

1. **Module `CathodeResolver`** — `Build.cs` depends on `Core` only; **couples-to-none** (no vendor;
   the litehtml / JS / font sides are consumers via the neutral seams the resolver defines). Quarantine
   intact: `.uplugin` `Modules[]` gains `CathodeResolver`; plugin `EnabledByDefault:false`; `Cathode`
   absent from `game/uptime.uproject` `Plugins[]` — disabled-not-compiled.
2. **`IBundleSource`** — a neutral logical-URL → bytes seam (hit/miss). The resolver's logic is
   backend-agnostic; the UE-asset backend (content-dir / UFS / `FFileHelper`) is a separate impl wired
   at integration.
3. **The URL-namespace resolution logic** — normalize a relative URL against a document/stylesheet
   base; a `/`-rooted absolute against the **virtual root**; map the normalized root-relative path to an
   `IBundleSource` lookup. **No custom author scheme** — authors write ordinary web URLs.
4. **The resource-resolution seam** 005's `document_container` calls — resolve an image (bytes + the
   size info `get_image_size` needs), resolve linked CSS (`import_css`), resolve url-addressed font
   bytes; plus the author-JS bundle bytes for 002.
5. **The 005 evolution** — litehtml's `document_container` gains the resolver hook;
   `load_image`/`get_image_size`/`import_css` route to it (replacing the TODO stubs). 005's rest is
   byte-identical.
6. **The configurable standard root** — a **project-setting default**, defaulting to **`/Game/Cathode/`**
   (`Content/Cathode/`), **expected to be changed by most users**. The resolver normalizes against the
   configured root; the integration backend reads `ProjectContentDir()/<root>/path` via `FFileHelper`.
7. **Robustness** — a missing / unresolvable resource returns a **clean not-found** (image → 0×0 size,
   font → skip, CSS → empty, JS-bundle-missing → a surfaced error), never a crash. Trusted-only /
   no-network → **no path-traversal or network theater** (a robustness posture, not security).
8. **Tests** (006-style, recording/in-memory `IBundleSource`, our bookkeeping): URL normalization
   (relative vs base, absolute vs root, `@import` vs stylesheet base); resolve hit/miss; the 005
   callbacks now route to the resolver (`load_image`/`get_image_size`/`import_css` →
   resource-resolution seam). **Zero** assertions on real UE asset loading or real bytes.
9. **`CathodeResolver/COVERAGE.md`** — every responsibility covered-or-deferred with
   reason/dependency/unblocker/first-exec-risk. Deferred: the 002 module-loader hook (single-bundle —
   not needed until multi-module); caching; the UE-asset backend + the 004 font wiring (integration);
   UFS-staging-in-a-cooked-build + real bytes (validation-pass). The configurable-root +
   content-dir/UFS/`FFileHelper` coupling recorded.
10. **`TODO-PREBUILD` tags** on any fork-variable touches introduced by the 005 hook (the litehtml
    container edit lives in 005's impl; the resolver itself is couples-to-none).

## In Scope (ordered slices)

1. **Surface + neutral seams + the 005-hook contract.** Define `IBundleSource`, the resource-resolution
   seam (image/CSS/font/JS), the URL-namespace shape, and the configurable-root; specify the 005
   `document_container` hook contract (the blessed evolution the TODO stubs name). Produce the
   Starting-Surface inventory. *Settle:* the **image-size seam** — does the resolver decode the image
   to answer `get_image_size` (which couples it to a decoder/003), or does it return bytes and the size
   is resolved consumer-side / deferred? Flag up if a real fork.
2. **Module + resolver logic + the 005 hook applied.** Establish `CathodeResolver` (Core only,
   couples-to-none); the URL-namespace resolution logic over `IBundleSource` (+ a recording/in-memory
   backend for tests); **apply** the 005 container hook (`load_image`/`get_image_size`/`import_css` →
   the resource seam), 005's rest byte-identical. Tests (our bookkeeping): normalization, hit/miss, the
   005 callbacks routing to the resolver.
3. **COVERAGE + finalize.** Author `COVERAGE.md`; record the deferrals + the configurable-root + the
   content-dir/UFS/`FFileHelper` coupling; place any `TODO-PREBUILD` tags; confirm the close-state
   checklist.

## Non-Goals

- Do **not** do any **network I/O** — bundled content only, ever.
- Do **not** build the **UE-asset backend** — the resolver stays backend-agnostic behind `IBundleSource`;
  the backend is integration.
- Do **not** use `UAssetManager` / the Primary-Asset API for raw web files — the coupling is the UE
  content-dir + UFS staging + `FFileHelper`; images are raw `.png`, not `UTexture2D`.
- Do **not** add a **002 module-loader hook** — single pre-bundled file; deferred.
- Do **not** add a resolver **cache** — deferred.
- Do **not** duplicate 004's **font system** — keep `RegisterFontFile`; the resolver is the byte source
  behind the read.
- Do **not** add path-traversal / sanitization theater — trusted-only, no attack surface (robustness,
  not security).
- Do **not** build, run, or link. Do **not** delete a fallback just because production does not hit it —
  trace callers and tests first.

## Starting Surface

Before editing, the developer produces the inventory in the shape:

```
responsibility -> seam (IBundleSource / resource-seam / 005-hook) or bespoke -> consumer (005 / 002 / 004) -> proposed action
```

so every piece maps to a neutral seam or bespoke URL logic, the consumer it serves, and the 005/004
deferral boundary.

## Method

The no-build discipline every slice follows: author the functionality complete per the component doc
even though it cannot run yet. The resolver **defines neutral seams** and wraps no vendor
(couples-to-none); 005 is **evolved** to consume the resolver hook (its container TODO stubs), 005's
rest byte-identical. Tests assert **our** bookkeeping (normalization results, hit/miss, the 005
callbacks routing to the resolver via an in-memory `IBundleSource`) and **never** real UE asset loading
or real bytes (validation-pass). Quarantine stays disabled-not-compiled. The accepted 002/004/006/007/008
baselines stay byte-identical; 005 changes only by the resolver hook.

## Acceptance

- `CathodeResolver` module exists, Core-only, couples-to-none (no vendor token in it), quarantine intact
  (`.uplugin` += `CathodeResolver`; `EnabledByDefault:false`; `Cathode` absent from uproject).
- `IBundleSource` + the resource-resolution seam + the URL-namespace logic + the configurable-root are
  authored per Final State; no custom author scheme.
- The 005 `document_container` routes `load_image`/`get_image_size`/`import_css` to the resolver; 005's
  rest is byte-identical to its accepted baseline.
- Robustness: a miss returns a clean not-found on every path (no crash).
- Tests assert our bookkeeping only — **zero** real-UE-asset-load / real-bytes assertions; normalization
  + hit/miss + the 005-callback routing are exercised.
- `COVERAGE.md` complete; references resolve; deferrals carry reason/dep/unblocker/first-exec-risk
  (002 module-loader = single-bundle; caching; UE backend + 004 wiring = integration; UFS-staging-cooked
  + real bytes = validation-pass).
- The 002/004/006/007/008 baselines are byte-identical; 005 changes only by the resolver hook.
- No build / run / link occurred and none is required to verify the above.

## Review Questions (open forks)

1. **The image-size seam** — `get_image_size` needs image dimensions; decoding the bytes to get them
   couples the resolver to a decoder (003/Blend2D). Does the resolver decode (coupling), or return bytes
   with the size resolved consumer-side / deferred (keeping couples-to-none)? Settle in S1; flag up if a
   real fork.
2. **Who defines the resource-resolution seam** — the resolver defines its own neutral seam and 005
   imports it (005 → `CathodeResolver` dependency, like the DOM imported 005's `ILayoutDocumentSource`),
   keeping the resolver couples-to-none. Confirm the dependency direction in S1.
3. **Configurable-root surface** — a project-setting default (`/Game/Cathode/`) the resolver reads;
   the exact config plumbing (a `UDeveloperSettings` vs a plain config value) is an integration detail
   bounded here to "the resolver takes the configured root as input." Settle the scaffold shape in S1.

---

# Postmortem (sprint close)

Sprint 009 closed with all three slices accepted. It is the **second couples-to-none pure-bespoke**
component (after Surfaces) and the **first to evolve a sibling** — 005 gains the resolver hook in its
litehtml `document_container` (the analog of 007/008 evolving 002 with an accessor; here a container
hook + a `CathodePaintRouting`-style routing layer). The defining work was at **framing**: the audit
caught that the human's "Unreal Asset Management" directive named the wrong API for raw web files, and
corrected it before the contract set.

## Lead — uptime.browse-lead

- **The framing-time correction is the win.** When the human's directive named "Unreal Asset
  Management," I gave the dev framing-time space to ground the mechanism rather than building on the
  label — and it surfaced that `UAssetManager` is UObject-only (raw web files aren't UObjects), correcting
  to the content-dir + UFS staging + `FFileHelper` coupling *before* the contract. I relayed the
  correction to you with the fact-vs-inference flagged and the intent preserved (couple to UE, a
  predictable Content location), and you ratified the configurable root. Lesson: when a directive names
  a specific engine system, the highest-leverage move is to ground that the system fits the data shape
  at framing — surface the correction, keep the intent.
- **Recognize a constraint-determined settlement, don't escalate it.** I flagged RQ1 (the image-size
  seam) as a possible fork, but the reviewer's own doc-map showed image *size* was never a resolver
  responsibility (the doc scopes it as returning *content*), and couples-to-none — which the human
  already ratified — dictated no decode. So it settled by construction; I confirmed rather than bringing
  you a non-decision. The genuine residual (the decode-for-size seam ownership) is recorded as an
  integration open.
- **Hold the surgical-evolution gate literally.** The 005 evolution had to be exactly the hook deltas;
  I had the reviewer diff the existing 005 files against baseline *and re-diff at close*, and adjudicate
  the new routing-helper files as a precedent-following realization, not creep.

## Developer — uptime.browse-dev

- **When a directive names an engine system, verify the system fits the data shape at framing.**
  `UAssetManager`/Primary-Assets is UObject-only; raw `.html`/`.css`/`.js` aren't UObjects — the correct
  coupling is the content-dir + UFS staging + `FFileHelper`, images as raw `.png` (cook drops a
  `UTexture2D`'s encoded bytes). A loosely-correct term can point at the wrong API; ground the mechanism,
  flag fact-vs-inference, don't build on the label.
- **A standing invariant often resolves a "fork" deterministically.** Couples-to-none decided RQ1: the
  resolver returns bytes and never decodes (decode = couple to 003), and the doc scoped images as
  *content* not size. Recognize when the constraint already decides — settle it, and surface only the
  genuine consequence (`get_image_size` now needs a decoder at integration; the 0×0 is recorded so it
  can't silently become permanent).
- **When a gate (surgical) and a requirement (testable) conflict for a vendor-confined consumer, reuse
  the sibling's established pattern.** 005's own `CathodePaintRouting` (litehtml-free helper files the
  container delegates to) resolved the tension — new `CathodeResourceRouting` helpers kept the existing
  005 files surgical and made the routing testable without standing up litehtml.
- **A comment must describe what the code does, not the aspiration** (`RouteImageSize`'s "confirm the
  image exists" overstated a hit/miss gate the code doesn't have).

## Reviewer — uptime.browse-review

- **Build your own doc-map first — it finds silent misses *and* tells you whether a "fork" is even in
  scope.** A responsibility implied by a vendor callback (litehtml wanting a size) is not automatically
  the component's; the doc's scoping word (content vs dimensions) is decisive. The own-map dissolved RQ1
  rather than escalating it.
- **Distinguish a constraint-determined settlement from a genuine human fork** — re-asking a settled
  constraint wastes the human as much as silently settling an open one. Record the residual (the
  decode-for-size seam ownership) regardless.
- **Gate an accepted-module evolution by a literal existing-file diff = exactly the contracted deltas,
  and re-diff at close** (later slices perturb); verify the null/unset path is byte-equivalent to prior
  behavior.
- **New files in an existing module aren't auto-creep** — verify they follow the module's own pattern
  (`CathodePaintRouting`, grep-confirmed litehtml-free), keep accepted files surgical, and sit on the
  correct side of the boundary (consumer-side mapping in 005; the resolver stays generic).
- **A deferral is honest when it's a sibling's domain / cooked-build-only / undefined-design** — and
  credit a `wiring-is-live` assertion (`ByteCalls==1`) that stops a deferred-value path (0×0) from
  masquerading as a hardcoded bypass.

## Inheritance for the next sprint / integration

- **The UE-asset `IBundleSource` backend** (integration): content-dir + UFS staging + `FFileHelper`,
  rooted at the configurable `/Game/Cathode/` (project setting, expected-to-change). **Verify UFS staging
  in a *cooked* build** — the editor reads loose source files and can mask staging bugs.
- **The decode-for-size seam ownership** (RQ1 integration open): who decodes the resolver's bytes for
  `get_image_size` (resolver-via-003-seam / 003 / the 005 container) is undefined; until wired, images
  lay out at 0×0. Fold the `RouteImageSize` resolve-then-discard into that path.
- **004 font wiring** (integration): keep `RegisterFontFile`; route 004's font-file read through the
  resolver (don't duplicate 004). **002 JS wiring** (integration): the resolver provides the author-JS
  bundle bytes; 002 `Evaluate`s them as a Module; no module-loader hook until multi-module/dynamic-import.
- **Caching:** deferred until a measured repeated-resolution cost. **Validation-pass:** real bytes /
  real UE-asset load / cooked-build UFS staging.
- **The predictability contract to preserve:** a game dev drops a file in `Content/<root>` and the
  browser reaches it via ordinary relative / `/`-rooted URLs, no Cathode-specific scheme.
