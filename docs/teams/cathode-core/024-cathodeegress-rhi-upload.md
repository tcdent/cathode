# Sprint 024 — Cathode Texture Egress (RHI upload)

> **COMPLETE. RQ1 (dedicated module) + RQ2 ((ii) neutral seam) — browse-design; RQ3 (HARD GATE) — tcdent. Slices:
> 0 spike (done) / 1a seam (e07efe4) / 1b egress (6b93998) / 2 regional (70a64ba) + this close. See Outcome.**

## Scope Boundary

This sprint owns **Texture Egress** — the render-thread side that gets a surface's composed CPU bitmap onto a
real `UTexture2D` a material can sample, uploading only the dirty sub-rectangle, only on change. It is the
**single RHI-agnostic touchpoint** (design-of-record `docs/browse/components-texture-egress.md`): all GPU
contact lives here, RHI-agnostic (no D3D/Vulkan/Metal interop), so the rest of Cathode stays platform-neutral.

**Owns:** the per-surface `UTexture2D` lifecycle (create/own/destroy); the CPU→GPU copy via regional
`RHIUpdateTexture2D`; render-thread affinity (the RHI work marshaled onto the render thread from the
"frame ready" signal).

**Does NOT own (named, with the owner):**
- **The readiness relay** — DONE in CathodeSurfaces (`FSurfaceReadinessSeam` / `FSurfaceReadiness`, emitted
  event-driven on change, tested). 024 *consumes* it; re-proving it is the reject.
- **The device/material reference** — how a device/screen actor + its material sample the surface texture is
  the **device/world spawn path** (a separate sprint; 023 residual (d)). Grounded out: no in-world consumer
  exists today (the game module does not dep the Cathode plugin; no device actor; no `UMaterialInstanceDynamic`
  usage). 024 is the upload **endpoint** (the `UTexture2D`); the format decision documents the eventual
  material contract (sRGB / unlit-emissive).
- **Resize/reformat** — N/A-by-design (Surfaces: dims/format immutable at construction).

## Problem

The composed tick (023) drives Layout→raster→Surface and emits a surface-addressed `FSurfaceReadiness`
(`FSurfaceId` + `FSurfaceReadyFrame{ FSurfaceBufferRef Buffer + TArray<FIntRect> DirtyRects }`,
`CathodeSurfaces.h:76-96`) on the neutral `FSurfaceReadinessSeam`, event-driven only on change — **but no GPU
copy exists.** CathodeSurfaces COVERAGE Deferred #2: "the actual copy of the dirty sub-rect into the surface's
texture is Texture Egress's render-thread work (`RHIUpdateTexture2D`)" — never built. The front-buffer pixels
are carried opaquely (`FSurfaceBufferRef`, never asserted by value); RQ2 (ii) has Surfaces **neutralize** that
handle into a vendor-neutral Pixels/Stride/Format view for the egress to consume. There is **zero**
`ENQUEUE_RENDER_COMMAND` / `RHIUpdateTexture2D` / `FlushRenderingCommands` precedent in the repo — 024 writes the
first render-command and the first RHI upload in the codebase.

## Goal

> The Surface bitmap uploads to a real `UTexture2D` via `RHIUpdateTexture2D`, dirty-rect only on change.

A composed surface's pixels become a real GPU texture, the per-change cost bounded to the damage ("a few
pixels for a ticking digit"), on the render thread, with all RHI contact confined to this one module.

## Final State

- A dedicated **CathodeEgress** module (RQ1 ruled): deps **CathodeSurfaces (the seam) + UE RHI/RenderCore/Engine
  only** — **NOT CathodeRasterizer** (RQ2 (ii): the egress consumes Surfaces' neutral view, never the Rasterizer's
  `FBitmapData`) — **no dependents** (the future spawn-path driver deps *it*). (Reconcile: update
  `CathodeRuntime.Build.cs:10-22`'s anticipatory egress comment to point here.)
- **The neutral bitmap view (RQ2 (ii)):** CathodeSurfaces neutralizes the opaque `FSurfaceBufferRef.Handle` into
  a vendor-neutral **Pixels/Stride/Format/Width/Height view** (only Surfaces, the owner, interprets its own
  handle; the producer populates it). The egress consumes that view, never pierces the `void*`. The view is a
  **borrow** with the 022 byte-view lifetime contract (valid for the readiness window) — the egress consumes it
  **synchronously at the callback via the copy** (below), so the borrow never crosses the async hop. The exact
  neutralization mechanism (the seam field shape + the producer-populate side) is a slice-1 build-surface item.
- A `UTexture2D` per `FSurfaceId` (**PF_B8G8R8A8, sRGB=true** — matches Blend2D `BL_FORMAT_PRGB32` =
  premultiplied sRGB BGRA8; no channel swizzle / no conversion on upload), created via
  `CreateTransient`/`NewObject`+PlatformData(mip0) → `UpdateResource`, with `GetResource()->GetTexture2DRHI()`
  valid (a render-thread init) before the first upload; **GC-safe** (the surface-id→texture map holds it via
  `TStrongObjectPtr`/UPROPERTY holder — a runtime UObject without a root is collected mid-lifetime); owned for
  the surface's lifetime, destroyed on unregister.
- An `FSurfaceReadinessSeam` callback (the production stand-in for the test lambdas) that, on a real change,
  **copies the neutral view's dirty-rect bytes into a command-owned buffer at the callback** (the borrow can't
  cross the async hop — this realizes "consume synchronously within the readiness window"), marshals onto the
  render thread (`ENQUEUE_RENDER_COMMAND`), and issues a regional `RHIUpdateTexture2D` per dirty `FIntRect` from
  the copy (source pitch = the view's `Stride`, not Width*4), only the changed sub-rect.
- An **injectable uploader seam** (`IUploader`/`TFunction`) behind the RHI call: production = the real
  `RHIUpdateTexture2D`; the test swaps a spy capturing the copied source bytes + pitch + region (no real-RHI
  dependency); the readback spike swaps the real RHI. The egress's public surface stays vendor-neutral (it
  consumes only Surfaces' neutral view; no Rasterizer type in its API).
- The upload proof shape (RQ3 = HARD GATE, spike-confirmed feasible): the **GPU readback** of the
  production-uploaded texture == the bitmap's dirty-rect, **every egress run** (the on-GPU layer is a hard gate,
  not a non-blocking upgrade), PLUS the **seam-spy** binding the egress's assembly (copied source-bytes + pitch +
  region + on-change + render-thread). Both ride the production path; the egress tests require the real D3D RHI.

## In Scope (ordered slices)

- **Slice 0 — the readback-feasibility spike (DONE).** A throwaway Win64 box test proved a per-pixel-distinct
  gradient round-trips exact off the real D3D RHI texture (non-circular, flushed, box survived). RHI path
  de-risked; RQ3 ruled HARD GATE. The spike is retired (deleted/relocated) in slice 1b.
- **Slice 1a — the seam-neutralization (no RHI; behavior-preserving).** `FSurfaceBufferRef` `{ const void* Handle }`
  → a vendor-neutral view `{ const void* Pixels; int32 Stride; int32 Width; int32 Height; ESurfacePixelFormat
  Format }`; the producer (`FRasterizerSurfaceRenderTarget::GetReadyFrame`) populates it from its `FBitmapData`
  (maps `EPixelFormat`→`ESurfacePixelFormat`); the 023 tick-test consumers read `Buffer.Pixels/Stride` instead of
  casting `Handle`→`FBitmapData`. *Proof:* the existing 023 composition tick tests (red-in-box / green /
  measured-width content), updated to read the neutral view, **still pass** — behavior-preserving; the `void*` is
  gone, CathodeSurfaces stays Core-only, nobody pierces a vendor type. No RHI, no hard gate, no new test.
- **Slice 1b — the egress (the first production RHI; the hard gate).** Stand up CathodeEgress (deps Surfaces +
  RHI/RenderCore/Engine, no dependents); the per-surface `UTexture2D` lifecycle (`CreateTransient` PF_B8G8R8A8
  sRGB → `UpdateResource` → `GetTexture2DRHI` valid, GC-held); the **copy-at-callback** (the neutral view's dirty
  bytes → a command-owned buffer); the **injectable uploader seam** (prod = `RHIUpdateTexture2D`, test = the spy);
  the first **full-texture** upload (`ENQUEUE_RENDER_COMMAND`); the **production-path readback** (the HARD GATE —
  on-GPU == bitmap, via `DriveAll(egress.GetSeam())`, never a re-hand-upload); the **seam-spy** (the 5 assembly
  gates); delete/relocate the slice-0 spike + reconcile CathodeRuntime's RHI/egress-comment. The 12-point bar
  applies here (minus (6) slice-2 regional, (12) device-ref non-goal).
- **Slice 2 — the dirty-rect regional upload.** One `FUpdateTextureRegion2D` per dirty `FIntRect`; only the
  changed sub-rect uploads. *Proof:* a localized change uploads only its sub-rect (region == the dirty rect; an
  unchanged region not re-uploaded); an idle surface → no upload.

## Non-Goals

- The device/material reference + the production driver/spawn-path (who calls `DriveAll` with the egress seam in
  production) — the device/world spawn path's own sprint. 024's test drives `DriveAll(egress.GetSeam())` like the
  existing tick tests.
- Re-proving the readiness relay / the on-change emit (DONE in Surfaces).
- Resize/reformat (N/A-by-design).
- **Do not delete a fallback just because production does not hit it — trace callers and tests first.**

## Starting Surface (audit before editing)

- `FSurfaceReadinessSeam` / `FSurfaceReadiness{ Surface, Frame{ Buffer, DirtyRects } }` — the egress input
  (`CathodeSurfaces.h:76-96`); the on-change relay is tested (`Cathode.Surfaces.Registry.DriveAllEmitsOnlyChanged`,
  the relay/payload tests). **Reuse, don't re-prove.**
- The seam-neutralization (RQ2 (ii), Surfaces-side): today `FSurfaceBufferRef.Handle` is an opaque `const void*`
  the 023 consumer cast to `CathodeRasterizer` `FBitmapData` (Pixels/Stride/Width/Height/Format,
  `CathodeRaster.h:38-45`; default `PremultipliedBGRA8`/`BL_FORMAT_PRGB32`). 024 has **Surfaces** expose a neutral
  Pixels/Stride/Format view (the producer — CathodeComposition's render-target backer — populates it from the
  `FBitmapData` it already holds). Audit the seam field shape + the producer-populate site.
- The RHI path (no precedent): `RHIUpdateTexture2D` + `FUpdateTextureRegion2D` + the `UTexture2D` + its RHI
  resource + `ENQUEUE_RENDER_COMMAND` (render-thread-bound). The only readback precedent is game-thread, not
  RHI-level (`UptimeLightingLibrary.cpp:23-42`, not under test).

Inventory shape (per dirty rect): `surface-id -> neutral view{Pixels,Stride,Format} -> copy dirty-rect bytes
(callback) -> dirty FIntRect -> FUpdateTextureRegion2D -> ENQUEUE_RENDER_COMMAND -> uploader seam ->
RHIUpdateTexture2D(UTexture2D resource)`.

## Method

- **The borrow cannot cross the async hop — copy at the callback (LOAD-BEARING).** Surfaces' neutral view
  `Pixels` is a *synchronous* borrow into the published front buffer (the 022 byte-view lifetime: valid for the
  readiness window, until the next swap). `ENQUEUE_RENDER_COMMAND` runs the RHI work async (~a frame later), so
  the next `DriveAll`→`RenderAndRelay` can swap/free the pixels under the command → UAF/tearing. So the egress
  **copies the dirty-rect bytes at the callback (game thread, borrow valid) into a command-owned buffer**, and the
  render command uploads the *copy*. This is exactly "consume the borrow synchronously within the readiness
  window" (browse-design's RQ2 lifetime note) — the copy is the synchronous consumption; the async RHI upload is
  of the copy. The copy is **in the proof's causal chain** — the seam-spy captures the command-owned copied bytes,
  which must == the view's dirty-rect bytes.
- **Trust UE's RHI; bind the egress's job.** `RHIUpdateTexture2D`'s on-GPU copy is UE-owned-correctness (like a
  vendor mechanism) — do not hand-assert it. The proof binds what the **egress** owns: the **copied source
  bytes** == the bitmap's dirty-rect bytes AND the **region** == the dirty rect AND **on-change**. The seam-spy
  (an injectable uploader the test swaps) achieves this with no real-RHI dependency; the GPU readback is the
  on-GPU **empirical upgrade** iff a real RHI is reachable.
- **The seam-spy rides the PRODUCTION path (live-path guard, LOAD-BEARING).** The injectable uploader (the RHI
  call) is the **only** swap; the readiness callback + the marshal + the neutral-view→copied-source + the
  dirty→`FUpdateTextureRegion2D` assembly are **production**, driven end-to-end via `DriveAll(egress.GetSeam())`.
  The spy reads the **egress's own assembled values**, never test-recomputed ones (a test that hand-computes
  source+region and calls the spy directly is a false green — it tests the test, not the egress's assembly).
- **Source pitch is an explicit gate.** PRGB32 may pad rows (`Stride >= Width*4`); a `Width*4` upload reads
  misaligned rows. The spy captures the passed `SourcePitch` and asserts `== Stride`, OR compares source bytes
  **per-row** (bitmap row at `Stride` vs uploaded row at the passed pitch) — a flat memcmp can silently miss the
  padding. PF_B8G8R8A8 sRGB ↔ PRGB32; no channel swizzle / no conversion on upload.
- **Render-thread affinity is asserted, not claimed.** The spy asserts `IsInRenderingThread() == true` at the
  upload (a game-thread/worker upload → false → RED). The headless test `FlushRenderingCommands` before asserting
  the spy/readback (a non-flushed assert races → flaky/empty green).
- **The readback spike is discriminating.** Its success criterion is a **known pattern round-trips** (upload a
  distinct pattern → readback → it *reflects* the pattern = feasible), NOT "the readback returned bytes" (a
  -nullrhi/stale readback returns zeros/garbage a weak check passes → a false "feasible"). If ambiguous → fall
  to the seam-spy (the conservative primary) + the named deferred-readback residual; never over-read as empirical.
- **Freshness** (live mtime race / desktop-session gate carry from 022/023): the relink-mtime-advance + the
  additive marker, never "build done"; the authoritative Win64 run post-relink.

## Acceptance

- **The upload to a real `UTexture2D` — bound on the production path (RQ3 = HARD GATE):** every egress run, the
  **GPU readback** of the production-uploaded `UTexture2D` reflects the bitmap's dirty-rect bytes (the on-GPU
  pixels == the bitmap, verified every run — not a non-blocking upgrade), AND the **seam-spy** binds the egress's
  assembly (copied-source-bytes == bitmap dirty-rect + pitch==Stride + region==dirty rect). Both driven via
  `DriveAll(egress.GetSeam())` — the egress's marshal + command-owned copy + upload, then readback — **not** a
  re-hand-upload (proves the mechanism, not the egress → REJECT), NOT a bare call-happened (vacuous), NOT a mock
  texture. The hard gate makes the egress tests require the real D3D RHI every run (tcdent's ruling — the 022 self-
  crash, not ambient flakiness, caused the session lapses).
- **Source pitch:** the spy captures the passed `SourcePitch` and asserts `== Stride` (or compares per-row); a
  `Width*4` pitch on a padded (`Stride > Width*4`) bitmap → RED.
- **Dirty-rect only:** the `FUpdateTextureRegion2D` bounds == the dirty `FIntRect`; an unchanged region not
  re-uploaded; a whole-texture upload → RED on the regional assertion.
- **On-change only:** an idle surface → no upload (the readiness event drives it).
- **Render-thread affinity — asserted:** `IsInRenderingThread() == true` at the upload; a game-thread/worker
  upload → RED (not merely "claimed marshaled").
- **The readback spike is discriminating:** feasibility = a known pattern round-trips (not "bytes returned");
  ambiguous → the seam-spy floor + the named deferred-readback residual, never over-read as empirical.
- **Leaf/neutrality:** CathodeEgress is the **only** RHI touchpoint; the RHI/UTexture2D does NOT leak into
  CathodeSurfaces (stays Core-only) or the engines; CathodeEgress has no dependents; its public surface stays
  vendor-neutral.
- **No regression:** the full Cathode suite green; the relink-confirmed Win64 run.

## Review Questions

1. **RQ1 — the home — RESOLVED (browse-design): dedicated CathodeEgress module** (deps Surfaces + UE RHI only, no
   dependents). Folding egress into CathodeRuntime (the base module everything deps) would leak Surfaces/RHI/
   Rasterizer up to everything including CathodeVerify, breaking platform-neutrality. Recorded in
   `components-texture-egress.md`. Reconcile: update `CathodeRuntime.Build.cs:10-22`'s anticipatory egress comment
   to point to CathodeEgress.
2. **RQ2 — the bitmap-bytes seam — RESOLVED (browse-design): (ii) the neutral seam.** Surfaces neutralizes the
   opaque `FSurfaceBufferRef.Handle` into a vendor-neutral Pixels/Stride/Format **view**; the egress consumes that
   (deps Surfaces+RHI only), **never pierces the `void*`**. Grounds: only Surfaces (the owner) can safely interpret
   its own opaque handle; (i)'s confined piercing still couples the egress to `FBitmapData` (breaks if Surfaces'
   representation changes); (ii) is the 021/022/023 neutral-seam pattern. The Pixels view is a borrow with the 022
   byte-view lifetime contract (valid for the readiness window; the egress consumes it synchronously within it —
   realized by the copy-at-callback, the async RHI upload being of the copy). *(Lead had leaned (i); conceded —
   the reviewer's (ii) was right.)*
3. **RQ3 — the proof shape — RESOLVED.** The slice-1 spike proved the GPU readback FEASIBLE (a per-pixel-distinct
   gradient round-trips exact off the real D3D RHI texture, non-circular, flushed, box survived; reviewer
   confirmed unambiguous). **tcdent ruled HARD GATE** (over the team's unanimous upgrade lean): the on-GPU pixels
   are verified **every egress run**. tcdent's correction of the team's frame: *"The desktop-session lapses were
   caused by the team's own crash (022), not ambient flakiness — so that's not a reason to weaken the proof."* The
   readback is therefore a hard gate (every egress run reads back the production-uploaded texture and compares to
   the bitmap's dirty-rect bytes); the seam-spy binds the assembly the readback doesn't directly cover (on-change,
   render-thread affinity, pitch). The readback **rides the production path** (`DriveAll(egress.GetSeam())` → the
   egress's marshal + copy + upload → readback) — a re-hand-upload (the spike's shape) proves the mechanism, not
   the egress → REJECT.
4. **RQ4 — the device-reference out (confirm).** Grounded out (no in-world consumer exists); 024 is the upload
   endpoint. Confirm the device/material reference is its own (spawn-path) sprint.

## Outcome

024 completed **Texture Egress** — the **first production RHI in the codebase**. The dedicated **CathodeEgress**
module (deps CathodeSurfaces + UE RHI/RenderCore/Engine only — the sole RHI touchpoint, no dependents, the
neutral-view consumer with no CathodeRasterizer dep) gets a surface's composed CPU bitmap onto a real
`UTexture2D` via regional `RHIUpdateTexture2D`, **dirty-rect-only on both the upload and the copy**,
render-thread-marshaled, verified on-GPU every run.

**Shape (every fork to its owner):** RQ1 (dedicated module) + RQ2 ((ii) the neutral seam) — browse-design; RQ3
(HARD GATE) — tcdent. The slice-0 spike proved the GPU readback feasible (de-risking the first-RHI path before any
egress). The re-slice: **1a** the no-RHI seam-neutralization (behavior-preserving) / **1b** the egress + the
hard-gate readback / **slice 2** the dirty-rect regional (upload + copy bounded).

**Commits:** `e07efe4` (1a — `FSurfaceBufferRef` → the neutral Pixels/Stride/Format view), `6b93998` (1b — the
CathodeEgress module + the first `RHIUpdateTexture2D`, hard-gate on-GPU readback), `70a64ba` (slice 2 — per-rect
tight copy + region, no coalesce). This close adds the COVERAGE records + the retrospectives.

**The proof — the multi-instrument partition:** the **readback** binds the on-GPU result (source/region/pitch/
format, every run on the real D3D RHI) + the **seam-spy** binds the two the readback structurally can't see
(on-change → no upload-call; render-thread `IsInRenderingThread`), both on the production path
(`DriveAll(egress.GetSeam())`, never a re-hand-upload). The format-map is a readback discriminator (distinct
3-channel B40 G120 R200 — a B↔R swap REDs); the copy is bounded to the rect (the "few pixels" cost is the copy AND
the upload); the padded-stride discriminator relocated from the upload-pitch assertion to the copy's strided read.
123/123 with the 1a baselines exact; the box survived the first RHI upload + readback.

**Residuals the next sprint inherits:** (a) the composition→egress **real wiring** (the egress proved its path
with a synthetic source; the real CathodeComposition-surface→egress bind is a later cross-module pass); (b) the
**N-rect no-coalesce empirical bind** (structural via the per-rect loop; single-rect proof); (c) the **device/world
spawn path** (binds the egress `UTexture2D` to a device material + calls `Register`/`DriveAll`); (d) the deferred
cook-pak + the 022 vendor-lib `assign_data` patch (tcdent-sequenced).

## Lead Retrospective — uptime.browse-lead

The first production RHI in the codebase was the sprint's real risk, and the loop retired it before the risky slice
ever ran: the **de-risk spike** grounded the box's RHI mode empirically (rather than assuming an env fact 023's
all-CPU tests never exercised), and the **re-slice** (1a no-RHI seam / 1b first-RHI) landed the seam-neutralization
behavior-preserving — the existing 023 baselines holding byte-identical — so 1b's gate was purely the RHI and the
hard gate. The dev surfaced both the re-slice and two mid-build forks (the synthetic-surface test choice, the
uniform-tight-vs-hybrid copy) *before* touching committed work; I ruled the boundaries (a seam change earns its own
gate; one copy path over a full-frame special-case) and, on the copy fork, attached the condition that preserved
the padded-stride discriminator by *relocating* it to the copy's strided read rather than letting it retire with
the upload-pitch assertion. The three design forks went to their owners — RQ1/RQ2 to browse-design, RQ3 to the
human — and the discriminator sharpenings (the readback-vs-seam-spy partition, the format-map as a readback
discriminator, the bounded copy) were relayed as build-shaping requirements, not after-the-fact gates.

The honest miss is mine to record. On RQ3 I carried the team's **unanimous** upgrade-lean up to tcdent on a
"recurring flaky desktop session" premise — and I had not verified that premise's *cause*. tcdent punctured it: the
session lapses were the team's own 022 crash, not ambient flakiness, so they were no reason to weaken the proof. I
conceded and adopted the hard gate. The lesson is the lead's version of the reviewer's self-correction: **a
recommendation's basis must be verified, or flagged as unverified, before it goes to the human** — I cited a cause
as fact without checking it, and the human had to supply the foresight the team had spent on a false frame. The
scoped-commit verification earned its keep again across four slice commits (the untracked spike excluded from 1a,
the cross-team WIP excluded throughout); and the multi-pass authoritative-run pin (gate the Found-N pass, not the
log-total) held the freshness honest on every on-box gate.

## Review Retrospective — uptime.browse-review

(Appended via agent-message; folded for the record.) The first production RHI in the codebase forced a
**multi-instrument proof** — an on-GPU **readback** plus a **seam-spy** — and the load-bearing discipline was
**partitioning the risk across instruments, each binding only what it can see, and never leaning one on what
another must bind.** The readback binds the on-GPU *result* (source + region + pitch + format — a wrong any-of-them
yields wrong GPU pixels), but it is **blind to two things by construction**: *on-change* (an idle frame's readback
reads the stale-but-correct texture and passes trivially — only the spy's idle→no-upload-*call* binds it) and
*render-thread affinity* (the readback sees the result, not the thread — only `IsInRenderingThread` binds it). The
**format-map is a readback discriminator, not a spy one** (the spy captures format-agnostic source bytes
`[B,G,R,A]`; only the on-GPU texture's channel order shows a swap — so a **distinct-channel** pattern is required,
a symmetric/grey one leaves the readback format-blind). And both instruments must ride the **production path**:
the test never calls `RHIUpdateTexture2D` (the egress does, via `DriveAll(egress.GetSeam())`), and the spy
captures the *egress's* assembled values vs the independently-known document — a re-hand-upload proves the
*mechanism*, a test-fed spy tests the *test*.

Two forks the review surfaced rather than let slide. The **bounded-copy fork** — "the region is bounded" silently
standing in for "the *per-change cost* is bounded": a whole-bitmap copy paired with a regional upload passes
region==R but pays the full CPU memcpy, defeating the design's "a few pixels for a ticking digit"; routed the
scope question, it was ruled in-scope (the spy now binds **both** region==R *and* SourceNumBytes==R). And the
**padded-stride relocation** — when the `(i)` uniform-tight ruling moved the copy to a tight pitch, the 1b
upload-pitch discriminator (`pitch==Stride≠W*4`) would have silently *dropped*; it had to be **re-established at
its new locus** (the copy's strided *read*), live only on a **padded + per-row-distinct** source (a uniform
pattern reads identically misaligned → the discriminator dies). A discriminator whose locus moves is not retired —
it is relocated, and the proof must re-exercise it where the risk now lives.

Two method notes. The **spike-feasibility** gate was discriminating-or-nothing: feasible means a *known pattern
round-trips off the GPU resource* (non-circular, flushed), never "bytes returned" (a null-RHI/stale readback
returns zeros a weak check passes). And the run log carried **multiple passes** (an early targeted pass + the main
full-suite pass) — gate the **Found-N / Queue-Empty-N** pass, not a log-total Success count. Finally, a
self-correction worth recording: the team (this reviewer included) leaned *upgrade* over *hard-gate* on a
"flaky desktop session" premise; tcdent corrected that the lapses were the team's **own 022 crash**, not ambient
flakiness — a false premise I had cited as a rationale without verifying its cause. Retracted and re-grounded on
the hard gate. **Flag an inference as an inference; verify the cause before citing it as a basis.** **Residuals
the next work inherits:** the composition→egress *real* wiring (1b proved the egress with a synthetic source); the
N-rect no-coalesce *empirical* bind (slice 2 proved it structurally — the per-rect loop — single-rect); the
device/world spawn path; and the deferred cook-pak staging + the vendored-lib patch (carried from 022/023).

## Dev Retrospective — Mundo (uptime.browse-dev)

024 was the first production RHI in the plugin. The lessons that carried:

**Spike-first: de-risk the unknown in isolation, not inside the feature.** The risk wasn't the egress logic — it
was whether a UTexture2D's GPU pixels can be read back in a headless Win64 box run (the real D3D via the desktop
session). A throwaway slice-0 spike (upload a known gradient → `ReadSurfaceData` → assert it *round-trips*, matched
64/64) locked the proof shape AND proved the 5.7 RHI path compiles + runs — the first RHI code in the repo
(`CreateTransient`/`UpdateResource`/`GetTexture2DRHI`→`FRHITexture` [the 5.7 `FRHITexture2D` consolidation]/
`RHIUpdateTexture2D`/`ENQUEUE_RENDER_COMMAND`/`ReadSurfaceData`). The egress then compiled with **zero** RHI-API
iteration. The de-risk paid for itself.

**The re-slice isolated the risk.** Grounding the seam-neutralization at audit showed it was a behavior-preserving
CathodeSurfaces+CathodeComposition change (no RHI), distinct from the egress's first-production-RHI + hard gate.
Splitting them (1a / 1b) landed the seam cleanly — proven byte-exact by the *existing* tick tests, no
RHI/desktop-session dependency — and confined the RHI risk to 1b. Worth the boundary.

**The load-bearing catches.**
- *The async-borrow UAF* (flagged in the 024 design review): `FBitmapData.Pixels` is a synchronous borrow into the
  front buffer; `ENQUEUE_RENDER_COMMAND` runs ~a frame later, so the next swap frees/overwrites it under the render
  command. The fix is the by-value copy-at-callback (the command owns the bytes). A correctness requirement, not a
  detail.
- *The readback-vs-seam-spy division*: the readback is the primary on-GPU binder (source+region+pitch+format via
  the result) but CANNOT see on-change (an idle readback reads the stale-but-correct texture → passes trivially) or
  render-thread. Those are the seam-spy's unique job. Conflating them leaves on-change unbound.
- *The format-map is a readback discriminator, not a spy one*: a wrong map swaps channels IN the texture, which
  only the on-GPU readback sees (source bytes are `[B,G,R,A]` regardless). The distinct-channel render binds it.
- *Region AND copy both bounded* (slice 2): a whole-bitmap copy + a regional region passes `region==R` but pays the
  full per-change memcpy — the spy asserts `SourceNumBytes` bounded too.
- *The padded-stride relocation*: the uniform-tight copy made the upload pitch the tight `W*4`, so the 1b
  `pitch==Stride` trap vanished; the padded-stride risk **relocated** to the copy's strided read, bound by a
  per-row-distinct readback. A discriminator whose locus moves must be re-exercised where the risk now lives — not
  silently dropped.

**Surface the fork when it touches a committed artifact.** Two forks I surfaced rather than guessed: the
synthetic-surface test choice (dep-confinement + source-agnostic — decided + noted), and the uniform-tight-vs-hybrid
copy fork, where the lead's "no-regression / 1b showed 384" and "SrcX=0 tight" genuinely conflicted on the
full-frame case **and** the answer edited a committed 1b test. A two-line confirm beat a wrong build on the slow box
and a wrongly-edited committed test. The lead ruled uniform-tight; the 1b seam-spy adapted (a legitimate
copy-shape evolution — the readback result byte-identical). When a spec ambiguity touches a committed artifact,
surfacing is cheaper than guessing.

**Process.** Every build/run ran via env on the box (the real D3D through tcdent's desktop session); I drove the
cycle and verified each outcome from the markers + relink-mtime myself, pinning the authoritative pass that
postdates the relink (not a log-total Success). The hard gate (the readback every run) means the egress proof is
gated on the session being up — hold for it, don't downgrade.
