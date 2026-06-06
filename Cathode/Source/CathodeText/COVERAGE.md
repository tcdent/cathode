# Cathode Text & Fonts — component coverage & accountable deferrals

Durable accountability record for Text & Fonts (sprint 004, no-build/no-run scaffold). It maps every
responsibility in [`docs/browse/components-text-fonts.md`](../../../../../docs/browse/components-text-fonts.md)
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

## Deferred — with accountability

### 1. Live cross-module seam consumers (the two seams' callers + the glyph-run→BLFont map)
- **Reason:** Text & Fonts *produces* the measurement + glyph-run seams; who *calls* them is future. The glyph-run seam is the neutral shape the integration sprint maps to the 003 `FPositionedGlyphRun`, and the Rasterizer maps `FFontHandle` → `BLFont` over the same bytes on its side.
- **Dependency:** Layout & Style (calls measure), the Rasterizer (consumes the glyph run + maps the handle), and an integration sprint.
- **Unblocks:** the integration sprint.
- **First-execution risk:** nothing measures or renders text across modules until the seams are wired. (Both seams exist + are produced + populated now; only the cross-module callers/map are future. Text & Fonts stays `BL*`-free.)

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

### 4. Shaping / typography fidelity proof
- **Reason:** the shaped positions/advances/ids are HarfBuzz's output; no shaping runs this sprint, so fidelity cannot be asserted (the tautology guard — the shaper's output is never hand-asserted).
- **Dependency:** an execution-sprint build + run; HarfBuzz itself owns the shaping correctness.
- **Unblocks:** the execution-approval sprint.
- **First-execution risk:** kerning/ligature/feature fidelity (and the 4K typography bar) is proven only when shaped + rendered; our orchestration is tested, the vendor shaping is assumed correct.
