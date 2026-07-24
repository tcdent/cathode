# Cathode Text & Fonts — component coverage & accountable deferrals

Durable accountability record for Text & Fonts (sprint 004, no-build/no-run scaffold). It maps every
responsibility in [`docs/browse/components-text-fonts.md`](../../../docs/browse/components-text-fonts.md)
to its status: **COVERED** (slice + mechanism) or **DEFERRED** (reason · dependency · what unblocks it ·
first-execution risk). Nothing in the component doc is silently missing.

This file is the accountability of record.

## Covered

| Responsibility | Slice | Mechanism |
|---|---|---|
| **Shape text** (string + font → positioned glyph runs, kerning/ligatures/features) | S3 | `FHarfBuzzShaper` wraps `hb_shape` (hb_* confined to the shaper `.cpp`); raw glyphs → `AssembleGlyphs` → `FShapedRun.Glyphs` (cumulative fractional positions) |
| **Measure text** (advances/metrics for layout) | S4 + S5 | `DeriveMetrics` (width = our sum of advances; ascent/descent/line-gap from `hb_font_get_h_extents`, scaled) → `FShapedRun.Metrics`; the **measurement seam** `GetMetrics(FShapedRun)` to Layout |
| **Manage fonts** (load + register) | S2 | `FFontRegistry` — `RegisterFontFile` loads each font **once** (injected loader; local FS / UE file APIs), engine-neutral `FFontHandle`, shared heap-stable bytes via `IFontByteStore::GetFontBytes` |
| **Manage shape plans** (cache repeated shaping) | S4 | `FCachingShaper` — caches `FShapedRun` by `FShapeKey`; a repeat same-key request does not re-shape (bounded FIFO default, no tuning surface) |
| **Per-run feature control** (`kern=0` retro-CRT) | S3 + S5 | `FTextFeatures` in `FShapeRequest` is the per-run input; `BuildShapeFeatures` maps it to the explicit feature set (our `FShapeFeature` → `hb_feature_t` at the thin edge); features are part of the cache key |
| **Feeds two consumers from one shaped result** | S4 + S5 | One shape → one `FShapedRun`; the measurement seam (`GetMetrics`) and the glyph-run seam (`GetGlyphRun`) both read the **same** result — no double-shape |
| **Glyph-run seam to the Rasterizer** (ids + fractional positions + **extents** + handle) | S5 | `GetGlyphRun(FShapedRun) → FGlyphRun` — engine-neutral, **no `BL*`**. The **run extents** (`DeriveExtents`, S4) retire the 003 `COVERAGE.md` #1/#2 *dependency* (glyph-only frames can dirty; the Rasterizer gets a `BLFont` via the byte store) |
| **Delegates glyph rasterization; serves Layout; OS-independent** | by design | No `BL*` / no `CathodeRasterizer` coupling (grep-clean); the OS text stack never participates |
| **Shape-plan/result cache policy** (doc-open) | S4 | DECIDED — a sensible fixed default (bounded, FIFO), no tuning surface (human-ruled) |

## Shaping execution status (sprint 021)

The **FIRST real HarfBuzz `hb_shape` execution.** `FHarfBuzzShaper` (real `hb_shape`) now runs over a real font,
proven DIRECTLY on CathodeText's API — a server **LEAF** (NO `CathodeLayout` dep, NO `ITextMeasurer` adapter):

- **Real shaping runs + the fidelity is ASSERTED** (retires the tautology guard, deferral #4):
  `Cathode.Text.Shaping.KerningRelation` registers a committed OFL font, runs real `hb_shape`, and asserts the
  **TAUTOLOGY-FREE kerning relation** — `measure("AV") < measure("A")+measure("V")` (kern-delta **+3.562** on
  Liberation Sans v2.1.5; only real `hb_shape` applying the font's GPOS/kern yields the `<`; a naive/proportional/
  fixed path gives `==`) + the **EXACT** non-kerning control (`measure("II") == I+I`). HarfBuzz's exact advances are
  NEVER hand-asserted (it owns shaping correctness) — the RELATION is what a stub fails.
- **Win64-executed, non-vacuous:** the shaping is `#if PLATFORM_WINDOWS` (HarfBuzz is Win64-only); a non-Win64 build
  `AddError`s (never a vacuous `#if`'d-out green). The `[WIN64-EXECUTED]` marker + real non-zero advances prove it ran.
- **Font source:** a committed **test fixture** — Liberation Sans Regular **v2.1.5 (SIL OFL 1.1)**,
  `Private/Tests/Fixtures/LiberationSans-Regular.ttf` (binary-verbatim via `*.ttf binary`; sha256-pinned in
  `PROVENANCE.md`), loaded via `FFontRegistry::RegisterFontFile` → `FFileHelper` → `hb_blob_create`. Test-wired.
- **Stays INTEGRATION (the leaf ruling — Text does NOT dep Layout):** the real `ITextMeasurer` adapter (CathodeLayout's
  seam ← CathodeText's shaper) + "the measurement reaches Layout" (Layout's `text_width` → the adapter → the shaper),
  and the glyph-run → 003 map, are cross-module composition-glue built at integration — NOT 021. See deferral #1.

## Deferred — with accountability

### 1. Live cross-module seam consumers (the two seams' callers + the glyph-run→BLFont map) — partially addressed (021)
- **Reason:** Text & Fonts *produces* the measurement + glyph-run seams. **Sprint 021: the shaper now RUNS and the
  measurement seam (`GetMetrics`) executes — proven directly on CathodeText's API (the kerning relation, see above).**
  What stays deferred is the CROSS-MODULE wiring: the real `ITextMeasurer` adapter so Layout's `text_width` reaches the
  shaper, and the glyph-run → 003 `FPositionedGlyphRun` / `FFontHandle` → `BLFont` map. The leaf ruling keeps Text
  dep-free of Layout; the adapter (deps both Text + Layout) is the integration sprint's.
- **Dependency:** Layout & Style (calls measure via the adapter), the Rasterizer (consumes the glyph run + maps the handle), and an integration sprint.
- **Unblocks:** the integration sprint.
- **First-execution risk:** nothing measures or renders text ACROSS MODULES until the seams are wired (the shaper itself runs, 021). Text & Fonts stays `BL*`-free + Layout-dep-free (the leaf ruling).

### 2. Which fonts ship / how they are bundled
- **Reason:** the loading *mechanism* is covered (single-load via local FS / UE file APIs); *which* fonts ship and how they are bundled is a content decision, not a shaping concern.
- **Dependency:** the bundled-content decision (and, eventually, the Resource Resolver for logical paths).
- **Unblocks:** content/bundling decisions; the Resource Resolver component.
- **First-execution risk:** screens reference fonts by a path/handle that must resolve to a shipped font; until the font set is decided, paths are placeholders.

### 3. Bidirectional (RTL) ordering
- **Reason:** RTL reordering is a separate concern (a small dedicated lib), not in the baseline (per the component doc).
- **Dependency:** a bidi/RTL library + real non-Latin RTL content.
- **Unblocks:** when non-Latin RTL content becomes real.
- **First-execution risk:** RTL text shapes/orders left-to-right (visually wrong) until added.

### 4. Shaping / typography fidelity proof — RETIRED (sprint 021)
- **RETIRED:** the FIRST real `hb_shape` ran (021) and the shaping fidelity IS now asserted — the tautology-free
  **kerning relation** (`measure("AV") < measure("A")+measure("V")`, kern-delta +3.562 on OFL Liberation v2.1.5; only
  real GPOS/kern yields the `<`) + the exact non-kerning control. See **Shaping execution status (sprint 021)** above.
  HarfBuzz's exact advances are still never hand-asserted (it owns correctness) — the RELATION is the tautology-free proof.
- **Still deferred:** ligature/feature fidelity beyond the kern relation, and the 4K typography bar — proven only when
  shaped + **RENDERED** (the glyph-raster sprint, the other engine stub). The measurement path is proven (021); the
  raster path is separate.
