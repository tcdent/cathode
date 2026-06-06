# Cathode — Text & Fonts (detail)

Detail for the **Text & Fonts** component (see `components.md`). What/why, not how. HarfBuzz is
grounding.

Text fidelity is a first-class goal (4K target, a high typography bar), and correct text
measurement is a layout dependency — so this component sits across the boundary between layout and
rasterization, serving both.

## Responsibilities

**Shape text.** Turn a string + font into positioned glyph runs with real kerning, ligatures, and
OpenType features applied. *Why:* the difference between typeset text and naive advance-width
layout — and the whole reason text fidelity holds at 4K. *Ground:* HarfBuzz GSUB/GPOS.

**Measure text.** Provide the advances/metrics layout needs. *Why:* box sizes depend on shaped
widths; layout cannot be correct without shaped measurement.

**Manage fonts and shape plans.** Load and register fonts; cache shape plans. *Why:* determines
which fonts ship and keeps repeated shaping cheap.

**Expose per-run feature control.** Let features be toggled per text run. *Why:* deliberate,
unkerned/raw text (retro CRT screens) is an aesthetic choice, not a limitation — `kern=0` on that
run.

## Key concern — it feeds two consumers

Shaped results feed both Layout & Style (measurement) and the Rasterizer (glyph runs). Keeping a
single shaped result usable by both, without double work, is the design tension here.

## Boundaries

Owns shaping, measurement, fonts, and feature control. Delegates glyph rasterization to the
Rasterizer. Serves Layout & Style. Rendering is OS-independent by design (the OS text stack never
participates).

## Open / deferred (team's call)

- Which fonts ship and how they're bundled/resolved (via the Resource Resolver).
- Bidirectional (RTL) ordering — deferred (a separate small lib) until non-Latin RTL is real.
- Shape-plan cache policy.
