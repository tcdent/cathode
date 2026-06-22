# Cathode Surfaces — component coverage & accountable deferrals

Durable accountability record for Surfaces (sprint 006, no-build/no-run scaffold). It maps every
responsibility in [`docs/browse/components-surfaces.md`](../../../../../docs/browse/components-surfaces.md)
to its status: **COVERED** (slice + mechanism) or **DEFERRED** (reason · dependency · what unblocks it ·
first-execution risk). One responsibility is **N/A-by-design** (resize), called out as such — not a silent
omission, not a deferral.

This file is the accountability of record. Every in-tree reference (file/type/test) resolves.

Surfaces is the first **pure-bespoke** component — it wraps no vendor. Crucially, 003 (Rasterizer) already
built the per-surface **producer** (`FSurfaceRasterizer` owns the `FDoubleBuffer` + `FDirtyRegion` + emits
`FFrameReadySeam`). So Surfaces **composes** that producer through the neutral `ISurfaceRenderTarget` seam
and **does not duplicate** buffering / dirty-tracking / frame-ready. What Surfaces owns is the
world-object layer 003 lacks.

## Covered

| Responsibility | Slice | Mechanism |
|---|---|---|
| **Define the per-surface object** (document + output buffer(s) + dirty-rect tracking) | S1 + S2 | `FSurface` (`Private/CathodeSurface.cpp`) binds identity (`FSurfaceId`) + **fixed** dims/format + the document handle (`ISurfaceDocument`, ← 005) + the render-target handle (`ISurfaceRenderTarget`, ← 003). The **buffer(s) + dirty-tracking are 003's producer, composed** via `ISurfaceRenderTarget` — never owned or duplicated; Surfaces never touches a CPU bitmap. |
| **Map one runtime to many surfaces** | S3 | `ISurfaceRegistry` / `FSurfaceRegistry` (`Private/CathodeSurfaceRegistry.cpp`): `Register`/`Unregister`/`Find`/`Num`/`DriveAll`; one runtime drives each live surface. `CreateSurfaceRegistry()` is the single construction point. Proof: `Cathode.Surfaces.Registry.RegisterLookupCount`, `…DriveAllVisitsLiveSet`. |
| **Manage buffers** (double/triple) | S1 (composed) | **By composition, not duplication:** the double-buffer + swap is 003's `FDoubleBuffer`/`FSurfaceRasterizer`, behind `ISurfaceRenderTarget` (`ProduceFrame → changed`, `GetReadyFrame → front-buffer ref + dirty rects`). Surfaces inherits the buffer-count knob (2, fixed) and never re-declares a buffer. (Triple buffering, if ever wanted, is 003's to add behind the same seam.) |
| **Track readiness** (emit frame-ready + dirty rect to Egress) | S2 + S4 | `FSurface::RenderAndRelay` assembles a **surface-addressed** `FSurfaceReadiness` (surface-id + ready front-buffer ref + dirty rects) and emits it to the neutral Egress seam `FSurfaceReadinessSeam` — **event-driven, only on a real change** (idle surfaces stay silent). Adds the surface identity 003's per-producer `FFrameReadySeam` lacks (a relay, not a re-emit). Proof: `Cathode.Surfaces.Registry.DriveAllEmitsOnlyChanged`, `Cathode.Surfaces.Relay.RenderAndRelayIsolation`, `…PayloadAssembly`. |
| **Own surface lifecycle** (create/destroy on device spawn/despawn) | S3 | `Register` creates a surface; `Unregister` + registry teardown free it (and, transitively, its document + target handles) via `TUniquePtr` — **no leak**, proven by construct==destruct counts in `Cathode.Surfaces.Registry.LifecycleNoLeak`. |
| **Registration API** (doc open item: a device declares "I have a screen" w/ explicit dims) | S1 + S3 | `FSurfaceRegistration` (explicit fixed dims/format + the document + target handles, ownership → registry) + `ISurfaceRegistry::Register → FSurfaceId`. Generalizable; the device-spawn **caller** is integration. |
| **Delegate rendering / GPU upload; created by the game** | by design | Rendering is delegated via `ISurfaceDocument` (← 005) — the surface's paint ops come from its document; GPU upload via `FSurfaceReadinessSeam` (→ Texture Egress). No `BL*`/`hb_*`/`litehtml`/sibling-module coupling (grep-clean) — "couples to none". |

## Deferred — with accountability

### 1. Live cross-module wiring — the seam BACKINGS RETIRED (023); the device-spawn caller + Egress still deferred
- **Retired by:** sprint 023 (the integration phase). The new composition root **CathodeComposition** backs the
  neutral seams with the real engines, injected into `FSurface` (Surfaces stays Core-only — the backers live in
  the composition root, never inside Surfaces): `ISurfaceDocument` ← `FLayoutSurfaceDocument` (the real
  CathodeLayout `ILayoutEngine` + the production resource resolver over `/Game/Cathode`); `ISurfaceRenderTarget`
  ← `FRasterizerSurfaceRenderTarget` (the real CathodeRasterizer producer via the `ISurfaceProducer`/
  `CreateSurfaceRasterizer` factory + the `FLayoutPaintOp`→`FPaintOp` mapper). The per-frame tick is driven via
  `ISurfaceRegistry::DriveAll` → `FSurface::RenderAndRelay`.
- **Proven:** box fill, text (one `FFontHandle` through measure+shape+raster), and linked resources all render
  through the composed tick (the `Cathode.Composition.Tick.*` tests; full Cathode 119/119).
- **Still deferred:** the device/world spawn path (who calls `Register`) + Texture Egress (the
  `FSurfaceReadinessSeam` → GPU copy, see #2) — the next epics, out of 023.

### 2. The buffer PIXELS + the single CPU→GPU copy
- **Reason:** the front-buffer pixel content is the Rasterizer's (ultimately Blend2D's) vendor output, carried through (`FSurfaceBufferRef`, opaque) and **never asserted by value**; the actual copy of the dirty sub-rect into the surface's texture is Texture Egress's render-thread work (`RHIUpdateTexture2D`).
- **Dependency:** the Rasterizer producing real pixels (execution sprint) + Texture Egress doing the GPU copy.
- **Unblocks:** the execution-approval sprint + the Texture Egress component.
- **First-execution risk:** no pixels reach a texture until the producer renders and Egress copies on the readiness event; our relay + bookkeeping are tested, the pixels/copy are the siblings'.

### 3. First-build compile of the module
- **Reason:** this is a no-build/no-run scaffold sprint; the module has never been compiled.
- **Dependency:** the execution-approval sprint's build (the plugin is enabled + referenced there).
- **Unblocks:** the execution-approval sprint.
- **First-execution risk:** the usual first-compile surface (signatures, includes) is unverified until built; the code is authored to the established module conventions (003/004/005).

## N/A by design (not deferred)

### Resize / reformat
- **Decision (human):** "we never resize canvases, so the browser scene is created with explicit dimensions on instantiation, and those never change."
- **Consequence in the code:** the `ISurfaceRenderTarget` seam carries **no `Resize`**; `FSurface` dims/format are captured at construction and **immutable** (zero `Width=`/`Height=` assignments after the ctor). The doc's "resize/reformat handling" open item is resolved as **N/A** — there is no resize path to build, defer, or test. This is a closed decision, not a gap.
