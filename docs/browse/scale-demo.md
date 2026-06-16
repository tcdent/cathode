# Cathode — Scale Demo & Stress Bench ("a thousand clocks")

> **Later sprint — deferred.** Needs the engine actually rendering first: the rendering pipeline,
> Surfaces, and Texture Egress in place (i.e. after the de-risk spikes and first compile).
> Captured here so the idea isn't lost.

A demo map that drives on the order of **a thousand live HTML clocks**, serving two ends at once.
A third kind of testing — scale/performance — distinct from functional gates (`testing.md`) and
rendering parity (`observation.md`).

## Purpose

- **Technical demo / promotion.** A recognizable "it scales" flex that makes Cathode legible as a
  real library to outsiders — README headline material.
- **Scale bench.** Establish a **credible, demonstrable number** ("Cathode drives N live screens at
  once") and, knobbed, find where it degrades.

Framing: the goal is a **demonstrable scale, not the absolute limit** — though the parameterized
version finds the ceiling too.

## Two modes (different ceilings)

- **Many surfaces** — ~1000 separate surfaces/documents/textures under one runtime. Stresses the
  many-surfaces model: surface management, texture egress, resident memory. **This validates the
  device-LCD extrapolation** and is the better promo (a wall of live screens).
- **One big document** — ~1000 clock elements in a single surface. Stresses litehtml's
  layout/relayout of a large DOM and one big raster — the "huge complex dashboard" case.

Do both; they prove different things.

## Knobs (the cost axes)

Parameterize along **frequency × dirty-area × count**:

- A thousand clocks at **1 Hz updating only the seconds digit** (tiny dirty rect) is the *cheap*
  regime — perfect for the headline "look how many" number, but it stresses nothing.
- Crank **update frequency**, **dirty area** (full redraw per tick), and **simultaneous updates**
  to push toward the ceiling — the stress half. (The deliberate inverse of "don't put a microsecond
  clock on a hundred screens" — here we do it on purpose, to measure.)

## What to measure (ideally engine-self-reported)

- **Worker tick time** — the real cost.
- **Frame-ready rate**.
- **Resident texture memory** (validates the per-LCD ~KB math; mip-streaming keeps it
  coverage-bounded).
- GPU side (N textured quads) is trivial and won't be the constraint — don't chase it.

## What it validates

Turns the "single-thread has a lot of headroom" reasoning into a **real number**, and confirms the
**dirty-driven, shared-runtime, single-thread scaling thesis** and the device-LCD extrapolation
empirically — not just on paper.

## Shape

A **demo map** (UE side — spawns the surfaces) plus the **clock app** (panel side — the HTML/JS
clock, authored to the supported subset). Self-reported metrics make it a machine-readable bench,
not an eyeballed one.
