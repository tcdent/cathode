# Sprint 022 — CathodeRasterizer: Blend2D real glyph raster

Complete the `DrawGlyphRun` production path (the **last engine stub**): map the neutral `FPositionedGlyphRun`
→ `BLFont` (via `BLFontFace::createFromData`) + `BLGlyphRun` and call the real `BLContext::fill_glyph_run`,
using **021's real shaper output** over the same font bytes, and prove a laid-out glyph run **rasterizes to
correct pixels in the Blend2D bitmap**. The final engine integration — real shaped text becomes real pixels.

> Scope from the PM (tcdent-approved): "CathodeRasterizer — Blend2D real glyph raster. Complete the
> DrawGlyphRun production path (the last engine stub) using real glyph output from 021's shaper. Prove: a
> laid-out glyph run rasterizes to correct pixels in the Blend2D bitmap."

## Scope Boundary

**This sprint owns** the real Blend2D glyph fill — growing the neutral `FPositionedGlyphRun` seam to carry
the font **bytes** + neutral glyph ids/positions, the real `FBlend2DContext::DrawGlyphRun`
(`createFromData` → `BLFont` + `BLGlyphRun` → `fill_glyph_run`), and a proof that 021's real shaper output
rasterizes to **correct pixels** in the Blend2D bitmap — the first real `BLContext` invocation + the first
real pixel-read under test.

**Premise (the 015–021 discipline — dev grounded + reviewer independently confirmed, citations):** the
Blend2D paint backend (`FillRect`/`FillBox`/`DrawImage`/border fills/`ClipToRect`/dirty-region/the CPU
bitmap, real `BLContext`) and the `DrawGlyphRun` **dispatch** + fractional positioning are **built**; only
`FBlend2DContext::DrawGlyphRun` (CathodeRasterBlend2D.cpp:155) is a **TODO no-op** (`(void)Origin;(void)Run;
(void)Color;` → a blank bitmap; COVERAGE Deferred #1). The `FFontHandle`→`BLFont` map is **entirely new**
(zero BLFont/createFromData/BLGlyphRun code); every rasterizer test today uses a **recording fake** (no
real `BLContext`, no real pixel-read). So 022 is the first real `fill_glyph_run` + the first real pixels —
substantive, not a re-proof of the (mock) dispatch.

**Layering — RESOLVED to byte-INJECTION, leaf-preserved (the 021 precedent + the seam's growth contract;
team-converged, dev + reviewer):** CathodeRasterizer is a **leaf** (Core + Blend2D, no Cathode-sibling dep).
022 **grows `FPositionedGlyphRun`** (its doc: "the fields grow with Text & Fonts on this live call site",
CathodeRaster.h:58 — the *planned* evolution) to carry the font **bytes** (a byte-view) + the Rasterizer's
own neutral glyph type (id + fractional x/y) + the font-size. `DrawGlyphRun` reads `Run.FontBytes` →
`createFromData` — **no CathodeText dep, no #include** (the production Rasterizer stays Core+Blend2D). The
`FGlyphRun`→`FPositionedGlyphRun` bridge (+ `FFontHandle`→bytes via CathodeText's byte store) is **test-wired**
(a test-only non-Shipping CathodeText dep, the 021 pattern). This applies browse-design's 021 leaf-preservation
ruling to the Rasterizer; flagged for browse-design's awareness, not a fresh escalation (no dep pulled).

**Out of scope (named, resolved):**
- **A CathodeRasterizer→CathodeText production dep** — rejected (byte-injection keeps the leaf). *Only* if
  the seam's byte-view hits a lifetime/ownership wall forcing a dep does it become a genuine escalation.
- **Glyph-run extents / glyph-only-frame dirty** (Deferred #2) — stays integration: the 022 proof is a
  **direct** `DrawGlyphRun` → bitmap → `GetData` (bypasses `RasterizeFrame`'s dirty-region path); the
  frame-path extents are a thin integration add.
- **The Layout→Rasterizer production drive** (the `FLayoutPaintOp`→`FPaintOp` map; who populates the seam
  from a shipped font) — integration / A2. 022 proves the FILL on 021's real shaper output, test-wired.
- **Styled/rounded borders, AA quality tuning** — deferred.

## Problem

`DrawGlyphRun` is a no-op and `FPositionedGlyphRun` is an empty placeholder — nothing maps a glyph run to a
`BLFont`/`BLGlyphRun`, no `fill_glyph_run` runs, and no test reads real pixels (text does not render;
COVERAGE Deferred #1). The dev's audit grounds: the seam growth (the byte-view + ids/positions + size);
`BLFontFace::createFromData` + `BLGlyphRun`; reaching 021's real glyph output (the `FGlyphRun` bridge, the
one-`FFontHandle` same-bytes anchor); the real pixel-read (`GetData`→`FBitmapData.Pixels`, PremultipliedBGRA8);
and Blend2D's Win64-only platform-gate.

## Goal

`FBlend2DContext::DrawGlyphRun` maps the grown `FPositionedGlyphRun` (font bytes + ids + positions) →
`BLFont` (`createFromData`) + `BLGlyphRun` and calls real `BLContext::fill_glyph_run`, and 021's real shaper
output (the actual `hb_shape` ids over the OFL Liberation Sans v2.1.5, **the same `FFontHandle` bytes** the
shaper used) **rasterizes to correct pixels** — proven **discriminatingly, tautology-free** (Blend2D owns
the rasterization, like HarfBuzz owned the shaping): the **two-sided position-aware ink/blank pair**.

## In Scope (one cohesive slice — the map + the proof are coupled; the strongest input is 021's real output)

1. **The map + the real `fill_glyph_run` + the localized-ink proof on 021's real shaper output.** Grow
   `FPositionedGlyphRun` (the neutral seam: font bytes + ids + fractional positions + size); implement
   `FBlend2DContext::DrawGlyphRun` (`createFromData` → `BLFont` + `BLGlyphRun` → `BLContext::fill_glyph_run`,
   retire the no-op). *Proof:* register 021's committed OFL fixture **once** → one `FFontHandle` →
   021's shaper (`hb_font`) → `FGlyphRun` (real ids); get the bytes (byte store) → `FPositionedGlyphRun`;
   `DrawGlyphRun` onto a real Blend2D bitmap → `GetData` → the **two-sided position-aware ink/blank pair**
   (below). The same `FFontHandle` drives both shape + raster → the rasterized glyphs ARE the shaped ids.
   Win64-gated.

## Non-Goals

- No re-proof of the paint backend / the mock dispatch (built; reused).
- No hand-assertion of Blend2D's exact glyph pixel values (the tautology guard — assert the ink/blank
  relation, never the vendor's pixels).
- No CathodeRasterizer→CathodeText production dep; no glyph-run extents/dirty (Deferred #2); no Layout
  production drive; no styled/rounded borders / AA tuning.

## Starting Surface (the audit the dev produced)

`FPositionedGlyphRun (grow: bytes + ids + positions + size, CathodeRaster.h:58) → DrawGlyphRun (createFromData
→ BLFont + BLGlyphRun → fill_glyph_run, retire the no-op :155) → real Blend2D bitmap → GetData →
FBitmapData.Pixels (PRGB32/PremultipliedBGRA8) → the ink/blank pair`. Test bridge: 021's OFL fixture (one
FFontHandle) → 021's shaper → FGlyphRun + the byte store → FPositionedGlyphRun. Audit targets:
`CathodeRasterBlend2D.cpp` (the no-op, the BLContext, `GetData`), `CathodeRaster.h` (`FPositionedGlyphRun`,
the format), `CathodeText.h` (021's `FGlyphRun`/`FFontHandle`/byte store), Blend2D `createFromData`/`BLGlyphRun`/
`fill_glyph_run` + Win64-only (Build.cs:38-40), 021's committed OFL fixture (reused — no re-commit). COVERAGE.md
(Deferred #1 retired) at close.

## Method

Grow the seam + implement `fill_glyph_run`; drive 021's real shaper output (one `FFontHandle` → the same
bytes shape + raster) → `DrawGlyphRun` → a real Blend2D bitmap; **read back the real pixels** (`GetData` →
the 4-BPP PremultipliedBGRA8 bytes, not the recording fake) and prove **tautology-free**: the **two-sided
position-aware ink/blank pair** — clear to a known background, draw a **dense** glyph ('M'/'A'/'B') in a
distinct Color, assert **(a)** ink (== the Color) within the glyph's laid-out box (Origin + the run's
position/advance) AND **(b)** blank (background) in a guaranteed-outside margin (before Origin + after
Origin+advance). The pair bites both degenerates (no-op → blank → (a) RED; fill-everything → no margin →
(b) RED); "any non-blank pixel" alone is too weak. NOT a hand-assert of Blend2D's pixels; ink-coverage
corroborating only. Win64-gate: `#if PLATFORM_WINDOWS`, the `#else` AddErrors (never a vacuous `#if`'d-out
green), a `[WIN64-EXECUTED]` marker emitting the ink-pixel-count (>0). Reuse 021's OFL fixture (the binary
`.ttf` sha-identity is 021's). Run the FULL CathodeRasterizer suite green; the paint/dispatch/frame/buffer
tests stay. Coherence + the authoritative-run discipline (the cross-team red-module case if it applies).

## Acceptance (reviewer-formulated)

- **Real `fill_glyph_run`:** `DrawGlyphRun` maps the grown `FPositionedGlyphRun` (bytes + ids + positions)
  → `BLFont` (`createFromData`) + `BLGlyphRun` and calls real `BLContext::fill_glyph_run` (not the no-op).
- **Correct pixels (tautology-free, the two-sided position-aware ink/blank pair):** ink (== the Color)
  within the glyph's laid-out box AND blank (background) in the outside margin — a no-op → blank → RED, a
  fill-everything → no margin → RED. NOT a hand-assert of Blend2D's exact pixels. A dense glyph.
- **The same-bytes anchor:** one `FFontHandle` drives both 021's shape and 022's raster → the rasterized
  glyphs ARE the shaped ids (a mismatched font → the ids index wrong glyphs).
- **Real pixel-read:** the actual Blend2D bitmap `GetData` → `FBitmapData.Pixels` (PremultipliedBGRA8), not
  the recording fake.
- **Leaf-preserved:** no CathodeRasterizer→CathodeText production dep / #include (grep-clean); the byte
  store bridge is test-only non-Shipping.
- **Win64 executed-not-skipped:** the raster assertions `#if PLATFORM_WINDOWS`; the `#else` AddErrors; the
  `[WIN64-EXECUTED]` marker + ink-count(>0) prove the gated path ran (not a vacuous non-Win64 green).
- **No regression:** the full CathodeRasterizer suite green; the paint/dispatch/frame/buffer tests stay.
- **Coherence:** the proof bites the no-op (blank → RED); the touched TUs recompiled (CathodeRasterizer +
  the test's CathodeText); the authoritative run.
- **COVERAGE updated** at close — Deferred #1 (vendor glyph rasterization) retired; the dirty/extents +
  Layout-drive recorded as integration.

## Review Questions (resolved)

1. **The byte-store layering** — RESOLVED (A, leaf-preserved): grow `FPositionedGlyphRun` to carry the bytes
   (the seam's planned growth, CathodeRaster.h:58); the production Rasterizer stays Core+Blend2D (no dep);
   the bridge is test-only. The 021 leaf precedent applied; flagged for browse-design's awareness.
2. **The pixel-correctness proof** — RESOLVED: the two-sided position-aware ink/blank pair (a dense glyph);
   ink-coverage corroborating; tautology-free.
3. **The glyph-run input** — RESOLVED: 021's real shaper output (the same `FFontHandle` bytes); 021's OFL
   fixture reused test-wired (no re-commit, no production font path).
4. **Platform-gating + dirty/extents** — RESOLVED: Blend2D Win64-only (the 021 executed-not-skipped
   discipline); the dirty/extents (Deferred #2) + the Layout drive stay integration (the proof bypasses the
   frame path).
5. **Scope vs the Layout drive** — RESOLVED: the FILL on 021's real shaper output; the Layout→paint-op→raster
   production drive is integration.

## Outcome

022 completed the `DrawGlyphRun` production path — the **last engine stub**. `FBlend2DContext::DrawGlyphRun`
maps 021's real shaped glyph (one `FFontHandle` → the same bytes → the same glyph-id space, so the rasterized
glyph IS the shaped id) to a `BLFont` (`createFromData`'s external-data form, the cache owning its byte copy)
+ a `BLGlyphRun`, and calls the real `BLContext::fill_glyph_run` → real pixels.

**Commit:** `0af506f` — the four CathodeRasterizer files (the impl + the grown seam + the test + the
test-only Build.cs deps). The vendor-lib patch (`patches/blend2d-assign-data-underalloc.patch`) + its
git-apply wiring are a **separate tracked follow-on**, not in this commit.

**The proof (reviewer-accepted, on-box):** `Cathode.Rasterizer.Glyph.RealRasterPixels` green — the
tautology-free **EXISTS-ink-in-the-laid-out-box (637) AND ALL-blank-margin (0)** relation on a real Blend2D
bitmap (`GetData`/`AlphaAt`); the same-bytes anchor (021's real shaped 'M', width=39.98); leaf-preserved (no
production CathodeText dep — bytes injected via the neutral `FPositionedGlyphRun`); cache-owns-bytes (the
`assign_data` heap-overflow bypassed via `createFromData`'s external-data ownership); Win64-executed; full
Cathode gate 116/116; the box **survived** (freshness by the crash→survive behavior-flip).

**The crash arc:** the first real exercise system-crashed the box; the cause was root-caused **off-box** (a
local ASan repro from the in-tree vendored Blend2D source) to a vendored `BLArray::assign_data` heap-overflow,
fixed by bypassing `assign_data` entirely. tcdent ruled (b): ship on the bypass (the box-green is the
production fix, green even on the unpatched lib); the vendor patch lands separately. **The epic's engine
integrations are complete** (016 real JS → 017 classes → 018 DOM → 019 resource loading → 020 litehtml on the
real Dom+Resolver → 021 real HarfBuzz shaping → 022 real Blend2D glyph raster) — real shaped text is now real
pixels, end to end over the actual engines.

## Lead Retrospective — uptime.browse-lead

The slice was small; the value was holding the loop's discipline through a system-crash arc. What the loop
got right: the crash surfaced immediately and was **owned, not guessed** — the dev refused a blind fix and
grounded the cause off-box rather than waiting on the symbolication gate; **no guardrail was bypassed** —
env's exfil refusal was endorsed and the artifact-move escalated to tcdent, not pressured past; the **vendored
landmine was grounded against the actual upstream state** (the pin *is* master HEAD; a "bump to latest" was a
no-op; a tag-bump would regress) and tracked as a portable `.patch` with the durability requirement named
(wire the git-apply, never a one-time manual apply); and the human's sequencing calls were made on **accurate,
corrected information** — I conceded my own imprecise attribution the moment the reviewer punctured it (the
box-green is the *production* bypass, not the patched lib). The scoped-commit verification earned its keep
again: it caught the agency WIP staged in the shared index at 021 and excluded the untracked `patches/` here.
The honest confidence I gave tcdent — grounded on five anchors, the off-box→on-box residual named rather than
overstated — was the right register for a forward call after a costly crash.

## Review Retrospective — uptime.browse-review

(Appended via agent-message; folded for the record.) The two sharpest re-derivations: the **crash→survive
behavior-flip** as the freshness signal for a system-crash fix (a stale crasher can't complete or emit the
marker, so "Target is up to date" is the wrong signal), and the **filtered-grep false-flag** — a leaf-clean
dep check grepped filtered to dep/Shipping lines *excluded the brace lines* and made a test-only dep look
unconditional (a false REJECT); the raw braced block is the ground truth, never a content-filtered grep. The
**two-sided EXISTS-ink/ALL-blank** is the recurring tautology-free proof for a vendor-owned-correctness
mechanism (the 020 import_css/null-control and 021 kerning/non-kerning analog): a relational property + a
control that bites both degenerate forms, never the vendor's exact values. The crash root-caused to a
non-in-tree vendor `.cpp` is corroborated-inference (version-match + asan stack + the patch diff), closed by
the on-box green, with the bypass-vs-patch attribution kept precise throughout.

## Dev Retrospective — Mundo (uptime.browse-dev)

The slice's code was small; the engineering was the crash arc. Durable lessons:

- **Root-cause a system-crash OFF-BOX.** The first `RealRasterPixels` run crashed the entire Windows build box;
  symbolication (PDB) was gated, and transferring the `.dll`/`.pdb` to my host tripped the exfil guardrail — a
  genuine deadlock (tcdent wanted the cause grounded *before* any re-run). I broke it without the box: a local
  Linux **AddressSanitizer** repro built from the **in-tree vendored Blend2D source**, replaying `DrawGlyphRun`'s
  exact sequence over the OFL font. asan pinpointed the fault in seconds — no box, no PDB, no exfil. When the box
  is the only way to *run* a thing but its source is vendored in-tree, asan off-box can ground the *why*.
- **The bypass vs the vendor patch are two different fixes.** The cause was a Blend2D bug (`bl_array_assign_data`
  sizes the new impl from the array's old size, not the new count → a heap-overflow copying into a fresh
  `BLArray`). The production fix is a **bypass** — `createFromData(ptr,size,destroy)` (external-data ownership),
  which never calls the buggy path — so 022 greened the box even on the **unpatched** lib. The vendor-source
  **patch** (`patches/blend2d-assign-data-underalloc.patch`) is the separate landmine fix, tracked as a follow-on.
  Shipping the slice did not require touching the vendored lib.
- **Cache-owns-bytes via external-data ownership.** The cache outlives the per-call `FontBytes` borrow, so the
  cached `BLFontFace` must own its bytes. `createFromData(ptr,size,destroy)` + a destroy callback gives the
  `BLFontData` ownership of a heap copy (stable address across a `TMap` rehash) — no UAF, no dangling, and it
  sidesteps the buggy `assign_data` copy. asan `detect_leaks` confirmed the callback frees it.
- **Crash→survive is the freshness signal for a system-crasher.** "Target is up to date" is not freshness. The
  dispositive proof the *fixed* binary ran: pre-fix the box crashed (no marker); the green run the box **survived**
  (MCP up, 0 Fatal) AND the additive `[WIN64-EXECUTED]` marker emitted real values (ink-in-box=637). A stale
  pre-fix binary would crash, not emit the marker.
- **Two-sided EXISTS-ink / ALL-blank is the tautology-free raster proof** (the 021 kerning-relation analog for a
  vendor-owned-correctness mechanism): EXISTS ink in the glyph's laid-out box AND ALL-blank in a guaranteed-outside
  margin — a no-op fill → blank → RED, a fill-everything → margin ink → RED — without hand-asserting Blend2D's
  exact pixels.
- **No-build-without-vendor is a structural seam.** `make build` = `setup.ps1 "all"` = vendor + UBT; every UE
  build re-runs the vendor step and rebuilds the lib if the source mtime is newer. The vendor lib can't be
  insulated from a UE build — which is exactly where the follow-on patch's `git apply` hook belongs (inside the
  vendor step, after checkout, before the blend2d build).
