# Cathode Egress — component coverage & accountable deferrals

Durable accountability record for **CathodeEgress** (sprint 024, Texture Egress). The **SOLE RHI touchpoint** in
the plugin: it subscribes to CathodeSurfaces' readiness relay, owns one UTexture2D per surface, and on each change
copies the surface's dirty bytes and uploads them to the GPU on the render thread. It consumes the 1a
vendor-**NEUTRAL** view (`Pixels`/`Stride`/dims/`Format`) — it never reads a vendor type (no CathodeRasterizer dep,
no `FBitmapData`, no `BL*`). It has **no dependents** (top of the dep graph, like CathodeComposition), so depping
the RHI confines it here.

Deps (Build.cs, raw-braced): Core + CoreUObject + Engine + RHI + RenderCore + CathodeSurfaces. No CathodeRasterizer,
no CathodeComposition. No dependents. Registered in `Cathode.uplugin`.

## Covered

| Responsibility | Slice | Mechanism |
|---|---|---|
| **The CathodeEgress module** (the sole RHI touchpoint, no dependents) | 1b | deps Surfaces + RHI/RenderCore/Engine; the RHI stays confined here. `FCathodeEgressModule` (passive). |
| **Consume the NEUTRAL view** (the 1a seam neutralization) | 1b | reads `FSurfaceReadiness.Frame.Buffer` as `{Pixels, Stride, Width, Height, Format}` — never a `void*`/`FBitmapData` (grep-clean: no CathodeRasterizer dep). The seam was neutralized in 1a (CathodeSurfaces + the CathodeComposition producer populate). |
| **Per-surface UTexture2D lifecycle** | 1b | lazily on the first readiness: `CreateTransient(W,H, format-map) → SRGB=true → UpdateResource → GetTexture2DRHI` (the render-thread init precedes the upload by FIFO). GC-held via `TStrongObjectPtr<UTexture2D>` in a `TMap<FSurfaceId,…>`; `Release(Surface)` drops the hold (collectable, destroyed on unregister). |
| **The format-map** (neutral → UE pixel format) | 1b | `ToTexturePixelFormat`: PremultipliedBGRA8/OpaqueBGRA8 → `PF_B8G8R8A8`, Alpha8 → `PF_A8`. BOUND by the readback (a wrong map swaps channels IN the texture → the on-GPU readback REDs); the seam-spy CANNOT see format (source bytes are `[B,G,R,A]` regardless). |
| **The by-value copy-at-callback** | 1b | at the readiness callback (game thread, borrow valid) the dirty bytes are `Memcpy`'d into a command-owned `TArray<uint8>` (`MoveTemp`'d into the render command). The borrow CANNOT cross the async hop — never the borrowed `Pixels` ptr (a UAF/tearing race otherwise). |
| **The injectable uploader seam** | 1b | `FEgressUploader` (Private header): production = the real `RHIUpdateTexture2D`; tests inject a spy. The egress's ONLY `RHIUpdateTexture2D` call (the test never calls it). |
| **The marshaled upload** | 1b | `ENQUEUE_RENDER_COMMAND` → resolve `GetTexture2DRHI()` inside the command → the uploader → `RHIUpdateTexture2D`. Render-thread affinity (`IsInRenderingThread()==true` at the upload). |
| **The dirty-rect REGIONAL upload** | 2 | ONE `FUpdateTextureRegion2D` + a TIGHT command-owned copy **per dirty rect** (read the rect's rows from the strided source into a no-padding buffer; `SrcX/SrcY=0`, `SourcePitch=R.Width*bpp`, `Dest`=R origin). UNIFORM-tight: the full-frame rect copies tight too (no special case). **NO bounding-box coalesce** (the per-rect loop emits N regions for N rects). The UPLOAD **and** the COPY are both bounded to R. |

**Proofs (Win64-content-gated, the real D3D RHI via the desktop session — the HARD GATE; full Cathode 123/123):**
- `Cathode.Egress.Readback.ProductionUploadRoundTrips` (1b) — **the HARD GATE**: drive the production egress →
  readback the egress's UTexture2D GPU pixels → `== the source bitmap` (match 64/64). NON-CIRCULAR (read the GPU,
  compare to the source; never a re-read). DISTINCT 3-channel (R-dominant) binds the format-map (a B↔R swap →
  readback blue → RED). Per-row-distinct (B=4+Y·8) binds the copy's strided read. The test never calls
  `RHIUpdateTexture2D` (only the egress does) and reads `egress->GetTexture` (no test-built texture).
- `Cathode.Egress.SeamSpy.AssemblesAndGatesUpload` (1b; copy-shape adapted in 2) — the seam-spy's **UNIQUE**
  binders the readback cannot see: **on-change** (idle drive → no upload-call) + **render-thread**
  (`IsInRenderingThread`). Corroborating-diagnostic: region, pitch (the uniform-tight `W*4`), bytes (bounded
  `W*H*4`), egress-assembled vs the independently-known doc (not circular).
- `Cathode.Egress.Regional.DirtyRectRegionIsSubRect` (2) — the **dirty-rect-only discriminator**: frame 2's region
  `== R=4×4@(2,2)` (an offset sub-rect), NOT the full texture (a full-upload → region=full → RED); the copy is
  bounded to R (64 bytes, NOT the full 256).
- `Cathode.Egress.Regional.SubRectContentAndUnchangedIntact` (2) — the sub-rect received the new content B at the
  right offset (R-match-B 16/16); the unchanged region retained frame-1's per-row A (nonR-retains-A 48/48).

## The hard gate (tcdent's ruling)

The on-GPU readback (`RHICmdList.ReadSurfaceData` off `GetTexture2DRHI()`, `FlushRenderingCommands` first) `== the
bitmap` runs **every egress run** on the real D3D RHI (the desktop session up is the bar) — NOT a non-blocking
upgrade. The feasibility was de-risked in a slice-0 spike (a known gradient round-trips headless on the box —
matched 64/64). The team's "flaky session" premise for a non-blocking upgrade was wrong (the lapses were our own
022 box-crash, not ambient flakiness), so the proof is not weakened. The seam-spy binds what the readback cannot.

## The readback-vs-seam-spy division

The readback is the PRIMARY on-GPU binder: source + region + pitch + FORMAT are all bound by "the GPU pixels == the
bitmap" (a wrong any-of-them → wrong on-GPU color → RED). The seam-spy is REQUIRED for the two the readback CANNOT
see — **on-change** (an idle frame's readback reads the stale-but-correct texture → passes trivially; only the spy
binds idle → no upload-call) and **render-thread** (the readback sees the result, not the thread). The spy's
source/region/pitch/bytes are corroborating-DIAGNOSTIC (the readback binds them via the result; the spy pinpoints
WHICH on a readback RED).

## Residuals (024 leaves)

- **(a) The composition→egress REAL wiring** — the egress proved its path with a SYNTHETIC source (a test
  render-target publishing a known bitmap), preserving CathodeComposition's no-dependents confinement and because
  the egress is source-agnostic (it consumes the neutral readiness). The real CathodeComposition-surface → egress
  bind is a later cross-module integration pass.
- **(b) The N-rect no-coalesce EMPIRICAL bind** — the per-rect loop emits N regions for N dirty rects (no
  bounding-box coalesce); this is STRUCTURAL (source-re-derivable, no coalesce code). The slice-2 proof uses a
  SINGLE sub-rect; a 2-disjoint-rect proof (2 regions ≠ the bounding box) is a later durable regression guard.
- **(c) The device/world spawn path** — who binds the egress UTexture2D to a device material + calls
  `Register`/`DriveAll`. Integration, out of 024.
- **(d) The deferred cook-pak + the slice-1 vendor-lib patch** — the 023 packaged-build cook pass + the 022
  `patches/blend2d-assign-data-underalloc.patch` (the external-data bypass ships unpatched), tcdent-sequenced.
