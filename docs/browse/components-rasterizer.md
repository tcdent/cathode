# Cathode — Rasterizer (detail)

Detail for the **Rasterizer** component (see `components.md`). What/why, not how. Blend2D is
grounding.

The Rasterizer turns the layout engine's draw calls into actual pixels in a CPU bitmap, on the
worker thread, off the game's render path. It is where the frame is produced.

## Responsibilities

**Implement the paint backend.** Service litehtml's `document_container` draw operations —
backgrounds, borders, boxes, images — with Blend2D. *Why:* layout emits draw intents; something
must execute them as pixels.

**Draw shaped text.** Rasterize the positioned glyph runs Text & Fonts produces. *Why:* text is
most of a screen's content and must render at the fidelity bar.

**Rasterize dirty regions only.** Redraw the changed sub-rectangles, not the whole surface. *Why:*
this is what makes per-change cost small and keeps idle surfaces near-free.

**Produce the CPU bitmap.** Maintain the pixel buffer the Surfaces component hands onward. *Why:*
the bitmap (and its dirty rect) is the unit Texture Egress uploads.

## Key concern — fidelity at 4K

Anti-aliasing, hinting philosophy, and overall quality at a 4K target are the bar this component is
judged against — and a named de-risk item. Getting Mac-class text/edges on Windows is the goal.
*Ground:* Blend2D, JIT-backed via AsmJit (zlib; ship with JIT — the no-JIT path is incomplete).

## Boundaries

Owns the Blend2D integration and the CPU bitmap. Consumes draw calls from Layout & Style and glyph
runs from Text & Fonts. Hands the bitmap + dirty rect to Surfaces. Does not touch the GPU.

## Open / deferred (team's call)

- Bitmap pixel format, chosen to match the egress texture format (avoid a conversion on upload).
- Dirty-region granularity (per-rect vs coarser).
