# Cathode — Texture Egress (detail)

Detail for the **Texture Egress** component (see `components.md`). What/why, not how.

This is the render-thread side of the pipeline: the single, RHI-agnostic touchpoint that gets a
surface's pixels onto a UE texture a material can sample — and it does so only when content
changed.

## Responsibilities

**Upload on "frame ready."** Take the surface's new frame and copy the dirty sub-rectangle into
its texture. *Why:* this is how rendered pixels become visible in the world, and uploading only the
damage keeps the per-change cost to a few pixels for a ticking digit. *Ground:* regional
`RHIUpdateTexture2D`.

**Own the texture resources.** Create, resize, and destroy each surface's `UTexture2D`. *Why:*
the GPU texture's lifecycle must track the surface's, and the game's materials reference it.

**Respect render-thread affinity.** Do the RHI work on the render thread, marshaling from the
"frame ready" signal. *Why:* RHI calls are render-thread-bound; the worker cannot touch the GPU
device, which is precisely why the design uses a CPU bitmap + upload rather than worker-side GPU
raster.

## Key concern — it is the only RHI touchpoint

By keeping all GPU contact here and RHI-agnostic (no D3D/Vulkan/Metal-specific interop), the rest
of Cathode stays platform-neutral and the design avoids the cross-device shared-texture complexity
it deliberately rejected.

## Boundaries

Owns the texture resources and the upload. Consumes "frame ready" + the bitmap/dirty rect from
Surfaces. Provides the `UTexture2D` the game's materials sample. Touches no JS, DOM, or layout.

**Home — a dedicated `CathodeEgress` module** *(confirmed 2026-06-21, 024)*. It deps `CathodeSurfaces`
(the readiness relay) + UE RHI only, and has **no dependents** — so the RHI/Unreal coupling and the
render-engine deps are confined to one site at the edge of the dep graph. Folding egress into
`CathodeRuntime` is rejected: `CathodeRuntime` is the **base** module everything deps, so it would leak
`Surfaces`/`Rasterizer` and the **RHI/Unreal** coupling transitively up to *everything* (including
`CathodeVerify`), breaking the platform-neutrality this component exists to protect. Same dep-confinement
principle as the 023 composition root — stronger here, because `CathodeRuntime` is the foundation.

**The bitmap-bytes seam — `CathodeSurfaces` neutralizes; egress consumes a vendor-neutral view**
*(ruled 2026-06-21, 024)*. `Surfaces` hands out a deliberately-**opaque** `FSurfaceBufferRef.Handle`
(`void*`). Egress does **not** pierce it. Instead `Surfaces` — which **owns** the bitmap and is the only
party that knows the handle's true type — exposes a plain, vendor-neutral `Pixels`/`Stride`/`Format`
(a non-owning **view**, no copy), and egress uploads from that. Rationale: (1) **only the owner can
safely interpret its own opaque handle** — egress assuming the internal `FBitmapData` type is fragile;
(2) the deliberate opacity stays a **real boundary**, not pierced-but-confined — egress never couples to
`Surfaces`' internal bitmap representation, so that representation can evolve without breaking egress;
(3) it's the same **neutral-seam** pattern as 021/022/023 (the owner produces a neutral struct the
consumer reads, never the consumer reaching into the owner's internals). *Lifetime:* the `Pixels` view is
valid only for the readiness window — egress uploads synchronously within it (same borrow-contract as the
022 glyph-run byte-view).

## Device / material reference (resolved 2026-06-22, 026)

A Cathode-surface device is a screen: **one surface per device** (1:1 device : surface : texture : MID —
the simple model). The device samples egress's `UTexture2D` through a per-device
`UMaterialInstanceDynamic`. Conventions established now (the rest is the dev's):

- **Unlit / emissive, faithful (WYSIWYG) display.** The device material is **unlit** (a screen *emits*;
  it is not lit by the scene) and outputs the sampled texture as **emissive**. The acceptance bar is
  *what-you-rasterize-is-what-you-see*: the rasterized pixel equals the displayed pixel. The sRGB chain
  must be consistent end-to-end — the rasterizer bitmap is sRGB `PF_B8G8R8A8` (024), the `UTexture2D`
  carries the **sRGB** flag (decode-to-linear once on sample), and the unlit-emissive output reaches the
  display without a **second** gamma. *Verify no double-gamma and that the tonemapper doesn't distort the
  UI* — faithful display is the criterion; the color-management mechanics are the dev's.
- **Texture-param-name contract.** The base material exposes a **single, conventionally-named** texture
  parameter (recommend `SurfaceTexture`) bound via `SetTextureParameterValue`. The name is the contract
  between the material asset and the device code — fix one shared name so every Cathode-surface material
  uses it; the exact string is the dev's, the *single shared name* is the convention.
- **MID lifecycle — rebind on texture realloc.** The device **owns** its MID (created at device init,
  lifetime tracks the actor) and binds `egress.GetTexture()`. It **re-binds the texture param whenever
  egress (re)creates the texture** — at init *and* on the resize path (surface reformat → texture realloc
  invalidates the old `UTexture2D`). Missing the rebind-on-realloc is a stale-texture bug. (Egress's
  realloc + the device's rebind are the two halves of the resize path.)

*Left to the dev:* the material asset name (`M_…` per the naming convention), the material graph (the
unlit/emissive nodes), the MID creation call site, and the device mesh/placement.

*Realized (026 slice 1–2):* the game-side module `UptimeCathodeDevice` (deps the Cathode plugin + Engine;
the main `Uptime` stays Cathode-free — the engines reach only the device module's link). `UCathodeDeviceSubsystem`
(`UTickableWorldSubsystem`, RQ4) holds ONE `FCompositionSurfaceOwner` (CathodeComposition), which installs the
production relayout seam on the **worker** (the worker builds the per-surface `FDomDocument`, reads+evals the author
bundle, composes); the subsystem **Pumps** the owner each Tick (drain the worker frame-ready + the egress upload half).
(028 S5 — the game-thread `DriveSurfaces` interim is RETIRED; the composition runs on the worker.)
`ACathodeDeviceActor` is the screen quad (`/Engine/BasicShapes/Plane`) carrying a device-owned MID of the
unlit/emissive **texture-sampling** `M_CathodeScreen` (emissive = a `TextureSampleParameter2D` named `SurfaceTexture`,
not a constant); `RefreshSurfaceBinding` binds `egress.GetTexture()` to `SurfaceTexture` **post-first-drive** (the
egress texture is null until the first drive — binding at `BeginPlay` would bind null) and **rebinds on a
texture-ptr change** (the realloc guard — exercised by `Cathode.Device.Realloc.RebindOnRealloc` against the production
wire). The production wire is proven by `Cathode.Device.Production.BundleWireAndDisplay` (028 S5) — the **real spawned
device** sampling the **worker-composed bundle** through a created-world `SceneCapture2D`, the capture RT tracking the
bundle GREEN (`G-dominant ∈ [0.01, 0.60]`) + production **at-the-raster == worker**: the binding display **hard gate**
(RQ3, tcdent), folding the retired slice-2 display proof. The single-gamma
sRGB chain held (the captured red was undistorted). The MID's shader-force is **test-only** (a created-world-automation
enabler; production renders compiled shaders normally).

## Open / deferred (team's call)

- The egress-side resize realloc itself (texture reformat without a hitch) — the producer half of the
  resize path above; pairs with the device's **rebind-on-realloc** (built + source-confirmed, not yet
  empirically exercised — the RQ5-static floor triggers no realloc).
