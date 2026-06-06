# Cathode — Surfaces (detail)

Detail for the **Surfaces** component (see `components.md`). What/why, not how.

A surface is one in-world display: a document plus the buffers and bookkeeping that turn its
rendered pixels into something ready to upload. This component also realizes the many-surfaces
model — one shared runtime driving many independent displays, from wall dashboards down to
single-character LCDs.

## Responsibilities

**Define the per-surface object.** A litehtml document + its output buffer(s) + dirty-rect
tracking. *Why:* each display is an independent render target with its own content and damage,
even though they share one runtime and global state.

**Map one runtime to many surfaces.** Hold the set of live surfaces and let the shared runtime
drive each. *Why:* this is the design's defining property — a tiny LCD and a big dashboard are the
same kind of object, both reading the same global state.

**Manage buffers.** Double/triple buffering so a stable frame can be handed to the render thread
while the next is being drawn. *Why:* the worker and render thread run concurrently; they must not
tear or race on one buffer.

**Track readiness.** Emit "frame ready" with the dirty rectangle when a surface has a new frame.
*Why:* it's the signal and payload Texture Egress consumes.

**Own surface lifecycle.** Create and destroy surfaces as their devices spawn and despawn. *Why:*
surfaces are tied to world objects and must come and go cleanly without leaking documents or
buffers.

## Boundaries

Owns surface objects, their buffers, and dirty tracking. Delegates rendering to the web-engine
components and GPU upload to Texture Egress. Is created/destroyed in response to the game (device
spawn/despawn), and is the natural place a device declares "I have a screen."

## Open / deferred (team's call)

- The surface registration API (how a device declares/sizes a surface).
- Resize/reformat handling (and how it cascades to relayout + texture realloc).
