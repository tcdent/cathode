# Surface-load `document` wiring (forward spec)

**Status:** PLANNED forward roadmap item (booked by browse-pm, 012/forward). This is the next step of the
**tcdent-ratified per-surface document model** — NOT a gap-patch of a deferral. Slice 5 deliberately builds the
per-surface document's install MECHANISM; the per-surface install is *invoked* by the surface-load flow when a
surface mounts, which is the planned forward work this spec frames.

## The ratified model (architecture.md:70-73, components.md:80)

**One shared runtime, many surfaces; each surface = one litehtml document.** So `document` is **per-surface** —
created and installed when a *surface* loads, scoped to that surface — NOT a per-runtime always-present global.
At runtime startup no surfaces are loaded, so no document is installed (by design; the author-JS bundle loader +
surface-load flow are future components). tcdent ratified **gamma**: wire `document` at surface-load, no
CathodeRuntime seam-exposure, the worker's human-ruled auto-start **untouched**.

## What slice 5 built (the per-surface install MECHANISM — done + green)

- `RegisterDomDocumentClass` — the `FDomDocument` JS class via the single binding-definition mechanism.
- `FDomDocument::InstallAsGlobal()` — the per-surface install method: wrap the surface's document + set
  `globalThis.document`. Proven by the slice-5 oracles driving everything through `globalThis.document`
  (getElementById === reflected-id; createElement/NS/TextNode). `InstallAsGlobal` is production code.
- The document members the mount needs: getElementById, createElement, createTextNode, createElementNS.

## The forward work (this spec — surface-load flow)

When the surface-load flow lands (the per-surface architecture's surface mount path):

1. On surface mount, create the per-surface `FDomDocument` over the shared runtime (for that surface's context).
2. Call `Doc->InstallAsGlobal()` so `globalThis.document` is the mounting surface's document.
3. On surface teardown, dispose the per-surface document with the surface (`FDomDocument::Dispose()` releases the
   tree's root ref; the runtime reclaims it — see the slice-5 Dispose-reclaim test).

No CathodeRuntime → CathodeDom dependency is formed (gamma): the install lives in CathodeDom; the surface-load
owner (a future component depending on both) calls `InstallAsGlobal` directly. The dep-inversion negative
(CathodeRuntime does not relink on a CathodeDom change) is preserved.

## Ownership / routing

Forward roadmap item, booked by browse-pm. Owner: browse (CathodeDom's `InstallAsGlobal` is ready; the future
surface-load component invokes it). Grounded in the tcdent-ratified per-surface model.
