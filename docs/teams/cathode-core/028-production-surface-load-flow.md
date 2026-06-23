# Sprint 028 — Production Surface-Load Flow (the founding worker-thread architecture)

> **CLOSED (2026-06-23) — all six slices accepted + committed. RQ1 (b) the founding worker-thread model; RQ2
> CathodeComposition (023) the owner. The production path IS built — no capability-proof, no deferral. The in-world
> device displays a real, bundle-authored, dynamically-updating DOM, composed on the worker, only the bitmap crossing.
> S1 `ad84217` · S2 `ebd8253` · S3 `dd398aa` · S4 `1671ccf` · S5 `86cedf7` · S6 `9d4fe37`.**

## Scope Boundary

Build the production surface-load flow on the **FOUNDING worker-thread architecture** (`architecture.md:85-112`): the
per-surface `FDomDocument` is built on the worker, the composition (litehtml layout + HarfBuzz shaping + Blend2D raster)
runs on the worker, and **only the bitmap crosses** to the render thread (the regional `RHIUpdateTexture2D`). This
**realigns 026's INTERIM game-thread composition** to the founding model (the 028-realignment note, `:107-112`) and
displays a real bundle-authored DOM on the in-world device, replacing the inline `FCathodeDeviceDocument` stub on the
production path.

**Owns:** the worker-relocation of the composition (the Phase-5 seam); the **egress thread-split** (worker-emit +
game-thread texture-create/upload); the **double-buffer cross-thread synchronization**; the worker-thread per-surface
`FDomDocument` build; the author-bundle loader; the per-surface production wire (CathodeComposition the owner) +
teardown; and — riding on top — the live two-frame.

**Does NOT own (named):**
- **The (a) proxy-serialize-handoff** — MOOT under (b): the `FDomDocument` + composition co-locate on the worker, so
  `GetDocumentHtml` is worker-local (no cross-thread serialize). The whole (a) string-proxy/version mechanism is gone.
- **The install MECHANISM** — `InstallAsGlobal`/`Dispose`/`RegisterDomDocumentClass`/the members are PROVEN (slice 5).
- **Per-device dims / egress resize-realloc** (026/RQ2 residual); **the A2 font** (027/RQ3 residual).
- **Multi-surface / N-runtime scale** — one surface, one shared runtime this sprint.

## Problem

After 026, the composition runs **synchronously on the GAME thread** (the device subsystem's `Tick` → `DriveAll` →
`RenderFrame` → litehtml/HarfBuzz/raster) — an **INTERIM build** (`architecture.md:107-112`) contradicting the founding
worker-thread model (`:85-112`: "the entire runtime … runs on a single dedicated worker thread … rasterizes on the CPU
(worker thread) and uploads to the GPU (render thread)"). No production `FDomDocument` is built (only the inline stub);
`InstallAsGlobal` has no production caller; the Phase-5 seam `FSurfaceRelayoutSeam` is **UNSET** (`CathodeTick.cpp:107`,
early-return; `SetSurfaceRelayoutSeam` no production caller); `LoadAuthorScript` is a no-op. The egress's `OnReadiness`
has a **game-thread-only UObject half** (`CreateTransient`/`UpdateResource`/the `TStrongObjectPtr`-`TMap`,
`CathodeEgress.cpp:156-167`) that cannot run on the worker; the double-buffer (`FDoubleBuffer`) exists but has **never
crossed a thread** (`SwapIfChanged` a plain index toggle, no atomics/fence/lock, `CathodeDoubleBuffer.cpp:25-35`).

## Goal

> tcdent (2026-06-22, RQ1 (b)): return to the founding architecture — litehtml layout + HarfBuzz shaping + Blend2D
> raster move to the worker thread; only the bitmap crosses (render-thread upload). **"The design-of-record already
> said this; we should not have escalated it."**

Finishing: the in-world device displays a real bundle-authored `FDomDocument` — **built and composed on the worker**,
only the bitmap crossing — and a worker-side mutation is visible as a v1→v2 change.

## Final State

- **The composition on the worker.** `DriveAll`/`RenderAndRelay` + litehtml `Relayout` + HarfBuzz `ShapeText` + Blend2D
  `RasterizeFrame` + the double-buffer swap run on the worker thread (asserted via `Affinity.IsOnBoundThread`), driven
  by the Phase-5 seam (`SetSurfaceRelayoutSeam` SET to a body that drives the registry + emits readiness as
  `FHostOutboundMessage`).
- **The egress thread-split.** `OnReadiness` splits: a WORKER-side emit (the `FHostOutboundMessage`) + a GAME-thread
  half (drained from the worker Outbound) that does the UObject work (`CreateTransient`/`UpdateResource`) + the
  render-thread upload (the existing `RHIUpdateTexture2D`, unchanged).
- **The double-buffer crosses the thread.** The worker rasterizes into BACK; the game/render reads FRONT;
  `SwapIfChanged` hardened with cross-thread synchronization (lock/atomic).
- **The device Tick shrinks.** Display (`GetSurfaceTexture`, game-thread) + DRAIN the worker Outbound (to trigger the
  game-thread texture upload) — NOT compose (the worker composes).
- **CathodeComposition the owner.** A new CathodeRuntime dep (to `SetSurfaceRelayoutSeam`) + the registry/egress
  handles (shifted from the device) + `InstallAsGlobal` + the per-surface `FDomDocument` build.
- **The per-surface `FDomDocument`.** Built on the worker over the shared runtime, seeded body (S1), `InstallAsGlobal`,
  the author bundle evaluated (mounts into `document.body`), wired as the device's source (replacing the inline stub),
  `Dispose` on teardown.
- **The live two-frame.** A worker-side mutation → worker re-compose → bitmap → the device shows v2, not v1.

## In Scope (ordered slices)

- **S1 — Body-seed.** DONE (GREEN, the `FDomDocument` ctor seeds `documentElement`>[head,body]; at the reviewer's
  gate). RQ1-independent.
- **S2 — The worker-relocation crux (the #1 risk — the egress thread-split; spike-first).** Wire
  `SetSurfaceRelayoutSeam` to a body that drives `DriveAll` **ON THE WORKER** (litehtml/Blend2D on the worker — asserted
  `Affinity.IsOnBoundThread`, `CathodeTick.cpp:39`) + the **EGRESS THREAD-SPLIT** (the worker emits readiness Outbound
  → the game-thread half creates/uploads the texture, drained via `DrainOutbound`) → the device displays the
  worker-composed box. The double-buffer cross-thread sync STUBBED (single-buffer / copy-under-lock) for the
  discriminating spike, HARDENED before S2 closes. *Proof — de-risk spike first:* the test enqueues "compose surface X",
  then **WAITS for the worker's FRAME-READY Outbound** (the `SurfaceTag` frame-ready IS the composition-done signal —
  the (b) readiness analog of a sync handshake; the worker composes ASYNC on its own FRunnable tick,
  `CathodeRuntimeWorker.cpp:109`) BEFORE the game-thread upload + the scene-capture (without the wait the capture races
  the async compose → a flaky/false result). Then the device displays the worker-composed box (the scene-capture) + the
  **WORKER-COMPOSITION marker** — the seam body RECORDS `Affinity.IsOnBoundThread` (`CathodeTick.cpp:39`) WHEN it
  composes; the test READS the recorded marker (a still-game-thread/interim path leaves it unset → RED; without the
  recorded marker "composed on the worker" is not observable); the egress thread-split proven (the worker emits
  Outbound, the game-thread does the UObject work + upload — no UObject work off the game thread).
- **S3 — Worker JS-build → worker-compose → device display.** The worker builds the per-surface `FDomDocument` (over
  the shared runtime) + composes on the worker → bitmap → the device displays. *Proof (027 slice-1 discriminators, now
  WORKER-built + WORKER-composed):* BLUE-dominant range-asserted (B>1.5R AND B>1.5G, frac [0.01,0.60]) +
  read-from-the-real-tree + IS-A `FDomDocument` + the worker-build & worker-composition markers. The test waits for the
  worker frame-ready Outbound before the capture (the S2 async-sync). Depends on S1 + S2.
- **S4 — The author-bundle loader.** Wire `LoadAuthorScript` (read the Axis-1 IIFE from the resolver → `Evaluate` →
  mounts into `document.body`). *Proof:* a real evaluated bundle authors a **THIRD distinct color (GREEN)**, distinct
  from S3's BLUE mount AND the inline RED — a no-eval (BLUE/RED, never GREEN → RED) cannot masquerade; the bundle's
  `createElement` reflected in the tree.
- **S5 — Per-surface wire + teardown + double-buffer hardening.** CathodeComposition (the owner: the new CathodeRuntime
  dep + the registry/egress handles + `SetSurfaceRelayoutSeam` install + `InstallAsGlobal`) wires the worker-built doc
  as the device's production source (replacing the inline stub); `Dispose` on teardown; the double-buffer cross-thread
  sync HARDENED (if not in S2). The device's DISPLAY half re-points to the OWNER's egress (`GetSurfaceTexture` queries
  CathodeComposition's egress, since the egress handle shifts from the device, `CathodeDeviceSubsystem.h:72`). *Proof:*
  the production device (no test seam) displays the bundle-authored DOM end-to-end; teardown disposes (the slice-5
  reclaim); the double-buffer sync correct (no tear/race).
- **S6 — The live two-frame.** A mutation via the 018 API **ON the worker `FDomDocument`** (the production path; v1 and
  v2 both worker-built+composed) → worker re-compose → bitmap → game-thread re-display. *Proof — two-sided (capture
  BOTH frames):* v1 (RED) → a visible worker mutation → v2; assert **v2 == mutated (BLUE) AND v2 != v1 (RED gone)**
  (bites no-re-render, stale overlay, smear).

## Non-Goals

- The (a) proxy-serialize-handoff (MOOT under (b)).
- Re-proving slice 5 / the 023/024/026 pipeline in isolation — but the relocated 023/024/026 tests UPDATE to the worker
  model (see Method; this is a real surface, not a no-op).
- The egress resize-realloc / per-device dims (RQ2 residual); the A2 font (RQ3 residual); multi-surface / N-runtime.
- **Do not delete a fallback just because production does not hit it** — the inline `FCathodeDeviceDocument` is the
  source-agnostic default until S5 wires the real source; trace callers/tests first.

## Starting Surface (audit before editing)

Produce a `piece -> proven/gap -> file:line -> proposed action` inventory before editing. Grounded:
- **The Phase-5 seam:** `FSurfaceRelayoutSeam = TFunction<void(TArray<FHostOutboundMessage>&)>` (`CathodeTick.h:21`),
  stored on the worker (`CathodeRuntimeWorker.h:90`), `SetSurfaceRelayoutSeam` (`:64`, no production caller);
  `RelayoutAndEmitFrames` (`CathodeTick.cpp:60,100-118`) UNSET → early-return (`:107`), SET → `Relayout` +
  `Outbound.Enqueue`. The BODY (drive the registry + convert `FSurfaceReadiness` → `FHostOutboundMessage`) is the (b)
  build. A typed/tested empty socket — LOWEST risk.
- **The relocation surface (game-thread today):** `DriveAll` (`CathodeSurfaceRegistry.cpp:41-52`) + `RenderAndRelay`
  (`CathodeSurface.cpp:18-47`) + litehtml `Relayout` (`CathodeComposition.cpp:56`) + HarfBuzz `ShapeText` (`:147`) +
  Blend2D `RasterizeFrame` + the double-buffer swap (`CathodeSurfaceRasterizer.cpp:57-88`).
- **The egress (#1 RISK — the thread-split):** `OnReadiness` copies the dirty bytes synchronously
  (`AssembleDirtyUploads`, `CathodeEgress.cpp:53-100,182`) → `ENQUEUE_RENDER_COMMAND` → `RHIUpdateTexture2D`
  (`:196-211`, thread-agnostic, STAYS); BUT the game-thread-only UObject work (`CreateTransient`/`UpdateResource`/the
  map, `:156-167`) must SPLIT to a game-thread half drained from the worker Outbound. The SOLE RHI touchpoint
  (`CathodeEgress.h:9`) — a structural change.
- **The double-buffer (#2 RISK):** `FDoubleBuffer` (front/back, `CathodeDoubleBuffer.h:13-14`) EXISTS — "Rasterizer
  draws BACK, Egress reads FRONT" — but never crossed a thread (the front `Pixels` borrow valid only in the sync
  callback, `CathodeSurfaces.h:67-71`; `SwapIfChanged` a plain index toggle, no atomics/fence/lock,
  `CathodeDoubleBuffer.cpp:25-35`). The cross-thread ownership/synchronization is NEW.
- **The device Tick:** `DriveSurfaces` (moves to the worker seam) + DISPLAY (`GetSurfaceTexture`, game-thread,
  `CathodeDeviceSubsystem.cpp:84`, STAYS) + DRAIN the worker Outbound (NEW).
- **CathodeComposition (RQ2 the owner):** PASSIVE today (`CathodeCompositionModule.cpp:5-9`, a pure factory); needs a
  NEW CathodeRuntime dep + the registry/egress handles (DEVICE-owned today, `CathodeDeviceSubsystem.h:71-72` → shift to
  the owner).
- **PROVEN (reuse):** the composition components (023 litehtml + 021 HarfBuzz + 022 Blend2D raster), the egress UPLOAD
  (024, `RHIUpdateTexture2D`), the worker runtime + the FIFO (`CathodeMessageQueue.h` `Enqueue`/`Drain`,
  `FHostInboundMessage{Apply}`/`FHostOutboundMessage{SurfaceTag}`), slice-5 `InstallAsGlobal`/`Dispose`/members, the
  `FDomDocument` + S1 body-seed.

## Method

- **Relocate, don't rebuild.** The composition components (023/021/022) + the egress upload (024) + the double-buffer
  are PROVEN; the (b) work is RELOCATING the drive onto the worker (the Phase-5 seam) + SPLITTING the egress
  (worker-emit + game-thread upload) + HARDENING the double-buffer cross-thread sync.
- **The no-regression bar is UPDATE-not-revert.** (b) relocates the composition DRIVE onto the worker + re-routes 024's
  egress source + shrinks 026's device Tick — so the affected tests **UPDATE to the worker model** (NOT revert the
  relocation, NOT made vacuous to pass); the **SCENE-CAPTURE (the on-screen content — where the composition RAN doesn't
  change what's displayed) is the INVARIANT.** The DRIVE-relocation regression weight is **026** (the device's
  `DriveSurfaces` moves to the worker seam) + **024** (the egress `OnReadiness` splits) — 023's composition-COMPONENT
  tests drive the components DIRECTLY on the test thread (thread-agnostic; the (b) change is the drive LOCATION, not the
  components, so 023 is less affected; the exact scope surfaces at build).
- **The worker-composition marker is the (b) discriminator.** Every display slice asserts the composition RAN on the
  worker (`Affinity.IsOnBoundThread` — NOT game-thread); a still-game-thread (interim) path REDs. **Bind the marker AT
  the composition itself** (the litehtml `Relayout` / Blend2D `RasterizeFrame` asserts `IsOnBoundThread`), not only at
  the seam's entry — so a future refactor that dispatched the raster off the seam's thread cannot slip a mislocated
  raster past a seam-only marker (the seam check is a valid proxy today since the seam calls the composition
  synchronously, but bind the raster/layout thread directly for durability).
- **Spike the egress thread-split (S2) first** — the #1 risk (the SOLE RHI touchpoint splits). The double-buffer
  cross-thread sync is #2 (stub for the spike, harden before S5/at S2 close). Every downstream piece (the composition
  factory, the double-buffer, the egress upload, the device readback) exists + is tested in isolation; only the
  worker-side drive body + the egress thread-split + the double-buffer sync are net-new.
- **The display proof is the 026 hard-gate scene-capture** (the on-screen content). Freshness / dep-confinement carry.

## Acceptance

- **S1:** `documentElement`>[head,body], `document.body` resolves; slice-5 green; the relocated DOM tests updated. (At
  the gate.)
- **S2:** the device displays a WORKER-COMPOSED box (the scene-capture + the worker-composition marker —
  `Affinity.IsOnBoundThread`, NOT game-thread); the egress thread-split proven (the worker emits Outbound, the
  game-thread half does the UObject work + the render upload — no UObject work off the game thread); the double-buffer
  sync (stub OK for the spike, hardened before close).
- **S3:** the device displays a WORKER-BUILT + WORKER-COMPOSED `FDomDocument`'s BLUE mount (BLUE-dominant range-asserted
  + read-from-the-real-tree + IS-A + the worker-build/compose markers).
- **S4:** a real evaluated bundle authors a THIRD distinct color (GREEN; a no-eval → RED).
- **S5:** the production device displays the bundle-authored DOM end-to-end (CathodeComposition the owner; the inline
  stub replaced); teardown disposes; the double-buffer sync correct.
- **S6:** the two-frame two-sided (v2==BLUE AND v2!=v1 RED gone) on a worker-side mutation.
- **No regression (UPDATE-not-revert — an explicit GATE-SWEEP, the S1 discipline):** the reviewer INDEPENDENTLY SWEEPS
  each relocated test (the relocation weight is 026/024), confirming EACH (i) drives the composition via the WORKER seam
  (a retained game-thread `DriveAll` = a REVERT → reject), (ii) keeps its assertion intact (weakened-to-pass = VACUOUS →
  reject), (iii) asserts the SAME on-screen content (box/color/position UNCHANGED — a content change is a REAL
  regression, not a thread move). The relocated-test UPDATE shape = drive-via-the-worker-seam + the worker-composition
  marker + the same-content scene-capture. Plus: the full Cathode suite + the 027-slice-3 baseline green; dep-confinement
  intact. Owed evidence, not a claim.

## Review Questions

1. **RQ1 — the handoff thread model — RESOLVED (tcdent, 2026-06-22): (b) the founding worker-thread architecture.** The
   composition (litehtml + HarfBuzz + Blend2D raster) runs on the worker; only the bitmap crosses (render-thread
   `RHIUpdateTexture2D`). The design-of-record (`architecture.md:85-112` + the 028-realignment note) already settled it;
   the (a) proxy-serialize is moot. (Lead lesson recorded: a thread-model fork checks the FOUNDING architecture, not
   just the forward spec + the current — possibly interim — code, before escalating.)
2. **RQ2 — the surface-load owner's home — RESOLVED (browse-design): CathodeComposition (023).** It needs a new
   CathodeRuntime dep (to `SetSurfaceRelayoutSeam`) + the registry/egress handles (shifted from the device).
3. **De-risk (NOT a design fork — the spike de-risks; routes up only if a fork emerges).** The egress thread-split
   (#1 — the SOLE RHI touchpoint splits into worker-emit + game-thread upload) and the double-buffer cross-thread sync
   (#2 — the lock/atomic primitive) are implementation risks the S2 spike proves (#1) and stubs-then-hardens (#2). If
   the split or the sync reveals a genuine design fork (not an implementation choice), it routes up.

## Outcome (closed 2026-06-23)

028 productionized the **founding worker-thread architecture** (RQ1 (b)) end-to-end. The in-world device now displays a
real, bundle-authored, **dynamically-updating** DOM — built, evaluated, and composed on the single worker thread, with
only the bitmap crossing to the render thread. The six slices, each accepted on an independent reviewer ACCEPT and
committed:

- **S1 — body-seed (`ad84217`):** the `FDomDocument` ctor seeds `documentElement`+`head`+`body` (the mount contract).
- **S2 — worker-relocation crux (`ebd8253`):** the composition (litehtml/HarfBuzz/Blend2D) relocates to the worker; the
  egress thread-split (worker-emit + game-thread upload), spike-first on the #1 risk; proven on the real auto-started
  worker (at-the-raster == worker).
- **S3 — worker JS-build (`dd398aa`):** a real `FDomDocument` built on the worker, co-located with compose; the read
  respects quickjs-affinity (worker-serialized HTML, no game-thread read).
- **S4 — author-bundle loader (`1671ccf`):** the production resolver reads a committed Axis-1 fixture; the worker
  evaluates it; the app's own IIFE self-mounts (a distinct third color proves the eval, not S3's hardcode).
- **S5 — production owner-wire (`86cedf7`):** `FCompositionSurfaceOwner` installs the seam on the worker via the
  device's production lifecycle; the game-thread interim retired (grep-clean + production at-the-raster == worker); the
  device deps narrow to CathodeComposition only.
- **S6 — live two-frame (`9d4fe37`):** one shared real DOM-layout engine backs both the doc and the composition, so a
  worker-side 018 mutation's `MarkNodeDirty` IS the render driver; v1→v2 swap, build-count==1 (dirty-mark not rebuild).

**Scope history.** tcdent ruled (B) the full production surface-load flow over the capability-proof ("the pattern of
deferring essentials and asking him to ratify the deferral ends here"), then ruled RQ1 (b) the founding worker-thread
model over the team's lean (a) the proxy-serialize handoff ("the design-of-record already said this; we should not have
escalated it"). Both rulings are honored end-to-end.

**Named deferred residuals (carried forward, not 028 blockers).** (1) the lock-free triple-buffer — an OPTIMIZATION
(the egress HolderLock + deep-copy is correct-by-construction; lock-free is contention avoidance, an architecture
follow-on); (2) the production GPU-readback green-count BANDING (S5 uses `>0`; band like the scene-capture — a
hardening); (3) the module-startup `LoadAuthorScript` no-op (now vestigial dead code — a cleanup); (4) multi-surface
(the single relayout seam fanning to N surfaces — 028 is one-surface by non-goal). The FStubDomLayout → real DOM-layout
deferral RESOLVED at S6.

## Retrospectives

### Lead Retrospective — uptime.browse-lead

028 was the hardest sprint of the Cathode epic — a full founding-architecture reshape arriving after a scope
correction. What I carry forward:

- **Check the FOUNDING architecture before escalating an architecture fork.** I took the worker→game handoff to the
  human as RQ1 with the team's lean (a) the proxy-serialize — but `architecture.md:85-112` had already specified the
  worker-thread composition; the game-thread build was *interim*, not an open question. A design fork is the human's
  only when the design-of-record (incl. the founding layer, not just the forward spec + current code) does not settle
  it. I grounded against the founding doc before treating the S6 two-engine unify as a fork — and it settled in-group.
- **Don't defer an essential and ask the human to ratify it.** The pattern I'd fallen into (026's live-change deferral,
  027's capability-proof reshape) ended here: when grounding shows the essential is hard/forward, scope the hard build
  and build it — never reshape it to a cheap proof + a named-forward residual routed upward.
- **Contradictions pause the work; route the smallest grounding.** Q4 (the dev's egress-holder grounding vs the
  reviewer's FDoubleBuffer (3)) I refused to average — paused the double-buffer ruling, routed an independent
  re-derivation, and the reviewer corrected its own (3). The owner-wire thrust (independent) kept moving in parallel.
- **Rule build-surface forks fast from the grounded constraints; escalate only genuine forks.** S2–S6 each surfaced
  several build-surface questions (the owner form, the dep-shift, the read-thread, the loader home, the engine unify) —
  all ruled in-group from the runtime constraints + the design-of-record, none manufactured into an escalation. The
  human owns priority/taste/genuine architecture; the lead owns the slicing.
- **Concede framing misses cleanly; the dev's grounding catches phantoms.** My "the production subsystem builds the
  doc" and "the `CreateDomDocument(html)` factory" framings were phantoms the dev grounded away before they became
  dead-end builds. Confidence is not a sensor; the team's grounding is.
- **Parallelize the genuinely-independent slice.** 027's rebind-on-realloc ran in parallel with the RQ1 ruling; S5's
  owner-wire ran in parallel with the Q4 re-derivation. Don't idle a slice behind a decision it doesn't depend on.

### Dev Retrospective — uptime.browse-dev

- **Ground architectural forks against the founding doc — before escalating or building.** `architecture.md` had ruled
  the worker-thread model all along; the game-thread composition was an *interim* build. A conflict with current code
  means the code is interim, not the architecture open. Caught the "production-subsystem-builds-the-doc" and
  "CreateDomDocument(html) factory" phantoms before dead-end builds.
- **Worker-affinity is the load-bearing constraint.** Every DOM build/mutation/read is worker-affine; the game thread
  only gets the bitmap + serialized strings. Read on the worker, never a game-thread quickjs read. Corollary: the
  worker IDLES after compose — a queued worker action must WAKE it (PostInbound).
- **De-risk the crux as a spike, then productionize** (S2 spike → S2-proper → S5 wire).
- **Two collaborating engines that look like one are the bug** (S6): the DOM's layout engine and the composition's
  render engine were separate; the UNIFY (one shared engine) makes `MarkNodeDirty` the render driver.
- **Discriminators that separate the real path from a plausible fake:** build-count==1 (real dirty-mark, not a rebuild);
  the relative SWAP when an absolute threshold is floored by a measurement artifact (characterize the artifact first).
- **When a green build fails at runtime, the production path is usually sound — the fault is the test harness or a
  worker-lifecycle assumption** (the S5 double-Deinitialize crash; the three S6 owner fixes).
- **Freshness gotcha — the inconsistent-DLL set:** a mixed multi-module relink wedges the editor; reconcile to a
  fully-green build before launching (highest risk on the biggest relinks).

### Review Retrospective — uptime.browse-review

- **Name a transient scaffold's retirement as a standing residual at introduction; discharge it only at the resolving
  slice with a production-path verification** (grep-clean + the live marker), not the gate test — held the
  game-thread-interim-GONE check from S2, discharged at S5.
- **Production-vs-test-local at a productionization slice:** the proof must drive the REAL lifecycle (the durable
  guard); a re-used test-local install proves the mechanism again, not the productionization.
- **The UPDATE-not-revert gate-sweep on a relocation** (drive-via-the-new-seam + intact assertion + same on-screen
  content), run as an independent whole-suite sweep; the content is the invariant, the thread the change.
- **Worker-affinity on the READ (S3); loader-not-the-eval (S4, the resolver-read provenance + defend the fixture bytes
  against the CRLF trap); the marker AT the load-bearing site, set-iff-the-property** (the at-the-raster inside
  RasterizeFrame; real-worker-not-FScopedTestRuntime).
- **The two-sided proof, measured relatively when an absolute floor is unmeetable** (the S6 swap cancels the constant
  artifact, still bites stale-overlay + no-re-render); **carry-not-rediscover** (build-count==1).
- **The two-direction retire check** (grep file names AND emitted markers); **a coverage FOLD must preserve every
  folded readback**; **freshness by the additive marker + pin the IN-SUITE pass** (a green-only-isolated is a
  discarded-harness reject).
- **What I corrected:** Q4 — I named FDoubleBuffer (worker-only, plain toggle correct) as the cross-thread-sync target,
  inferring from architecture.md's "double-buffer" noun; re-derived from source — the actual hop is the egress holder,
  correct by construction. *Lesson: re-derive the actual cross-thread ACCESS from source, never infer a sync target
  from an overloaded doc noun; prove concurrency by the lock/fence ORDERING, not a non-deterministic stress run.* And:
  the worker-vs-game-thread fork — I leaned (a) on the forward spec + interim code without checking the founding
  `architecture.md`.
