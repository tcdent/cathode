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

## Open / deferred (team's call)

- Texture format and sRGB/color handling, matched to the rasterizer's bitmap format.
- The resize path (surface reformat → texture realloc) without a hitch.
- How a device/material references its surface texture.
