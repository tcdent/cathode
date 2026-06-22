# Sprint 026 — Device/World Spawn Path (the first live in-world Cathode surface)

> **COMPLETE — the first end-to-end visible Cathode screen. RQ1+RQ2 (browse-design), RQ3 (HARD GATE) + RQ5
> (static) (tcdent). Slices: 0 spike (feasible) / 1 real composition→egress bind `0ea086c` / 2 the device display
> + this close. The slice-2 code+asset commit lands with the env-authored `M_CathodeScreen.uasset`. See Outcome.**

## Scope Boundary

This sprint owns the **device/world spawn path** — wiring the proven composed pipeline (023) + texture egress
(024) into a real in-world device whose material samples the live `UTexture2D`, so a composed HTML/CSS surface is
**visible on a screen in the world**. It closes 024's residual (a) (the composition→egress *real* bind, replacing
024's synthetic source).

**Owns:** the production driver (create the registry + Register the real CathodeComposition surface + create the
egress + `DriveAll(egress.GetSeam())` per frame, owning the DOM); the device actor (a screen mesh + an
unlit/emissive material + a `UMaterialInstanceDynamic` sampling `egress.GetTexture(SurfaceId)`); the in-world
display proof.

**Does NOT own (named, with the owner):**
- **The scale-demo** (a thousand clocks) — a later stress/perf sprint (`scale-demo.md`, explicitly deferred).
- **The egress upload / the composed tick** — proven (024 / 023); re-proving is the reject.
- **A live DOM-driven content update** — RESOLVED out (PM RQ5): "live" means driven by the real composition
  pipeline (the static display + the per-frame drive); the animated content-change (the DOM-mutate two-frame) is a
  residual / follow-on.

## Problem

The seams exist but nothing wires them in production: `MakeCompositionSurfaceRegistration` → `Register` →
`ISurfaceRegistry::DriveAll(egress.GetSeam())` → `egress.GetTexture(FSurfaceId)` is the design's *anticipated*
device-spawn path (`CathodeComposition.h:19`, `CathodeEgress.h:43`), but **no production driver, no device actor,
no material bind** exists. 024 proved the egress with a **synthetic** source + a GPU readback; the real
composition→egress drive is unbuilt (024 residual a). The material bind is the central gap — **zero**
`UMaterialInstanceDynamic` / `SetTextureParameterValue` in `game/Source`. The game module does **not** dep Cathode
today.

## Goal

> An in-world device displays a live Cathode surface.

A real composed HTML/CSS surface, driven per frame, appears on an in-world device's screen via the egress
`UTexture2D` — the first end-to-end visible Cathode screen.

## Final State

- A dedicated **Cathode-device module** (RQ1 — browse-design): deps Cathode (Composition + Egress + Surfaces) +
  Engine, holding the device actor + the driver. 026 gives CathodeComposition its **first dependent** → the
  vendored engines (Blend2D/HarfBuzz/litehtml/quickjs, confined in Composition) reach this module's link; a
  dedicated module **confines** that reach (keeps the game module decoupled), vs the game depping Cathode directly.
- A **`UTickableWorldSubsystem` driver** (RQ4 — proven precedent `UUptimeSimSubsystem`): owns the `ISurfaceRegistry`
  + the `CathodeEgress` + the **DOM** (`ILayoutDocumentSource`, held BY REFERENCE by the composition surface — the
  caller must outlive it); `Register`s the composition surface; `DriveAll(egress.GetSeam())` per Tick — the one
  per-frame drive for all registered surfaces (the design's "one runtime drives each live surface").
- A **device actor** (RQ2 conventions ruled): a screen quad + an **unlit/emissive base material** sampling the
  `SurfaceTexture` param (the sRGB chain consistent end-to-end — sRGB PF_B8G8R8A8 → sRGB-flagged `UTexture2D` →
  unlit emissive, no second gamma) + a device-owned `UMaterialInstanceDynamic` that **`SetTextureParameterValue("SurfaceTexture",
  egress.GetTexture(SurfaceId))` on BOTH init AND egress-texture realloc** (the rebind-on-realloc guards the
  stale-texture bug); 1:1 device:surface:texture:MID. (Left to the dev: the `M_` asset name, the graph internals,
  the MID creation call site, the mesh/placement.)
- The **composition→egress real bind** (024 residual a): `DriveAll(egress.GetSeam())` drives the real composition
  surface → `egress.GetTexture(Sid)`'s content == the real composed bitmap.
- The **in-world display proof** locked by the slice-0 spike (RQ3): the empirical scene-capture if feasible, else
  the bind+content+placement floor + a named on-screen-display residual.

## In Scope (ordered slices)

- **Slice 0 — the display-proof feasibility spike (the crux de-risk; throwaway, mirrors 024's readback spike).**
  Grounds, discriminatingly, at three levels → locks RQ3's proof shape:
  1. **The world mechanism** (the load-bearing sub-unknown): try the **created `UWorld` (EWorldType::Game) +
     `USceneCaptureComponent2D::CaptureScene()`** path FIRST — `CaptureScene` forces an explicit render into the
     RT, which may work **without a game viewport/PIE** (far lighter); fall to a **PIE world**
     (`GEditor->RequestPlaySession`, editor-attached, heavier) only if the created world won't render a capture.
     The world test must **actually spawn + tick a world** (not a degenerate pure-unit posing as a world test).
     **Name which mechanism works** — it decides whether the display proof is a box-automation test or an
     editor/PIE channel.
  2. **The capture round-trip** (discriminating): an **emissive** quad + a known **distinct** texture →
     `CaptureScene` → read back the **capture's RT** via `ReadLinearColorPixels` (non-circular — the capture's RT,
     NOT a re-read of the source texture; tonemapper-off; flushed) → the captured pixels reflect the texture. NOT
     "the capture returned pixels" (a black/Lumen-fogged/default capture a weak check passes). The
     *emissive-sidesteps-Lumen* hypothesis (an unlit emitter has no GI dependency) is the thing to confirm headless.
  3. **The match tightness:** the capture reads the rendered material through the capture's color pipeline
     (`SCS_SceneColorHDR` vs `SCS_FinalColorLDR`, tonemapper, sRGB encode) — NOT the texture's raw bytes (024's
     `RCM_UNorm`), so the match is likely **tolerant** (a per-channel threshold), not byte-exact. Establish the
     achievable tightness so slice 2 picks the right discriminator (a distinct-channel composed color stays
     discriminating under a tolerant match). **Conservative on ambiguity at any level → the bind+content+placement
     floor + the named display residual** (never over-read a degenerate world test or a non-discriminating capture).
- **Slice 1 — the device-home module + the subsystem driver + the composition→egress real bind.** Stand up the
  game-side integration module (RQ1); the `UTickableWorldSubsystem` (registry + egress + DOM + `DriveAll`/frame).
  **Production DOM construction** (a slice-1 grounding item — the 023 tick tests fed a test-double; production
  needs a REAL DOM): the DOM factory (does CathodeDom expose `CreateDomDocument(html[,css])` → `ILayoutDocumentSource`?)
  + the content source (an inline HTML string for the proof, or a `/Game/Cathode` file via the resolver) — the
  driver constructs + **owns** it (the by-ref lifetime). *Proof — the real composition (NOT synthetic; the gate
  that closes residual a):* `Register(MakeCompositionSurfaceRegistration(real DOM))` → `DriveAll(egress.GetSeam())`
  → the composition's real Layout/Rasterizer produced the bitmap the egress uploads → `egress.GetTexture(Sid)`'s
  on-GPU content == the **real composed bitmap**, the composed color **read from the composition's layout result**
  (the 023 read-from-layout discipline), NOT a hardcoded value and NOT 024's `FSyntheticEgressTarget` (a synthetic
  source re-proves 024's upload, not the bind → residual a NOT closed).
- **Slice 2 — the device actor + the material bind + the display.** The screen quad + the unlit/emissive base
  material (the `SurfaceTexture` param, the sRGB chain) + the device-owned MID +
  `SetTextureParameterValue("SurfaceTexture", egress.GetTexture)`; the device placed in a world. **Bind timing /
  rebind (RQ2(3) + the lazy-texture ordering):** the egress creates the `UTexture2D` LAZILY on the first readiness,
  so `egress.GetTexture` is null until the subsystem has driven once — the device binds **post-first-drive** (never
  at BeginPlay before the subsystem ticks → binds null) AND **rebinds on egress-texture realloc** (the stale-texture
  guard). A distinct-channel composed color (a strong R-dominant box) keeps the display assertion discriminating
  even under a tolerant capture match. *Proof:*
  per slice-0 — (i) the scene-capture of the device's screen == the composed content (empirical display), or (ii)
  the floor: the MID texture-param == `egress.GetTexture` (the bind) + the content (slice 1) + the **MID is the
  placed mesh's material** (applied to the mesh's slot, not bare placement / not the default) + a named display
  residual (the material-graph correctness — the emissive node samples the param — rides the residual; the
  empirical capture catches a graph error, the floor assumes the RQ2 contract). The RQ2 device/material fork lands here.
- *(The live content-change update — a DOM mutate → re-`DriveAll` → the device shows v2 — is a **residual /
  follow-on**, NOT in 026 (PM RQ5 ruling): "live" means driven by the real composition pipeline, not animated
  content changes. 026 = the static display + the per-frame drive.)*

## Non-Goals

- The scale-demo / N-surface stress bench.
- Re-proving the egress upload or the composed tick.
- The live DOM content-change update (PM RQ5: out of 026 — a residual; static display + per-frame drive is 026).
- **Do not delete a fallback just because production does not hit it — trace callers and tests first.**

## Starting Surface (audit before editing)

- The seams (PROVEN): `MakeCompositionSurfaceRegistration` (the doc backer holds `Document` **by reference** — the
  driver owns the DOM lifetime); `ISurfaceRegistry` (`CreateSurfaceRegistry`/`Register`/`DriveAll`); `CathodeEgress`
  (`GetSeam`/`GetTexture`/`Release`). Reuse, don't re-prove.
- The driver pattern (PROVEN): `UUptimeSimSubsystem` (`UTickableWorldSubsystem`, "the one place the headless sim
  touches UE per frame"); siblings Historian/SaveLifecycle.
- The mesh spawn (PROVEN): `NewObject<UStaticMeshComponent>` + `SetStaticMesh` + `RegisterComponent`
  (`RoomActor.cpp:1857`, `CableSandbox.cpp:321`); RoomActor is `bCanEverTick=false` (the live device needs a tick →
  the subsystem).
- The RT readback half (PROVEN): `UptimeLightingLibrary::ReadLinearColorPixels` (the game already deps RHI/RenderCore).
- GAPS: the MID/texture-param bind (central — zero in game); a screen quad + an unlit/emissive base material; a
  world/PIE test precedent + a C++ `SceneCapture2D` (the producer half is Python/editor-only, Lumen-headless risks
  documented); the DOM-mutate-and-redrive (for live).

## Method

- **Spike the display proof first (the 024 pattern), the created-world + `CaptureScene` path first.** The crux is
  the empirical "displays" — ground the world-mechanism (try a created `UWorld` + `USceneCaptureComponent2D::CaptureScene`,
  which forces a render without a viewport/PIE, before falling to a heavier PIE world) + the capture round-trip +
  the match tightness (the emissive-sidesteps-Lumen hypothesis) in a throwaway slice-0 before committing the device
  build. The **bind+content+placement** is the always-available floor; the scene-capture is the empirical display
  upgrade.
- **The DOM lifetime is the driver's.** `MakeCompositionSurfaceRegistration` holds the DOM by reference; the
  subsystem owns the DOM so it outlives the surface (a dangling DOM is a UAF).
- **Unlit/emissive material** so the captured pixels == the texture (no GI/tonemapper shift) — the capture must be
  raw/tonemapper-off + sRGB-handled for an exact match (else a tolerant match).
- **Freshness** (carry from 024): the relink-mtime-advance + the additive marker; the authoritative Win64
  (multi-pass: gate the Found-N pass). A world/PIE test on the box requires whatever session 024's readback needed.

## Acceptance

- **The composition→egress real bind (residual a closed):** `DriveAll(egress.GetSeam())` over the real
  CathodeComposition surface → `egress.GetTexture(Sid)`'s on-GPU content == the real composed bitmap (a known
  HTML/CSS → a known composed color), NOT 024's synthetic source.
- **The device displays the composed content (RQ3 shape):** the device's in-world screen reflects the composed
  surface — empirically (slice-0 scene-capture: the captured device-screen pixels reflect the composed content, a
  tolerant per-channel match through the capture color pipeline, a distinct-channel composed color), or the named
  floor (the MID texture-param == `egress.GetTexture` + the content + the **MID applied to the placed mesh's
  material slot** — not bare placement; display inferred, the material-graph correctness on the residual).
- **The material bind:** the per-device MID's texture param IS `egress.GetTexture(SurfaceId)` (one surface per
  device); the screen mesh placed.
- **Leaf/dep-confinement:** the Cathode engines reach only the device-home module's link, not the game module's
  (per RQ1); the game stays decoupled (or deps only the device module's minimal surface).
- **No regression:** the full Cathode suite green; the 023/024 baselines exact.

## Review Questions

1. **RQ1 — the device-home — RESOLVED (browse-design): (b) a dedicated game-side integration module.** A
   `.uproject` module under `game/Source/` (e.g. `UptimeCathodeDevice`), deps the Cathode **plugin** + Engine,
   holding the device `AActor` + the `UTickableWorldSubsystem` driver. Dep-confinement is decisive: (a) `Uptime`
   depping Cathode would spread Blend2D/HarfBuzz/quickjs/litehtml + RHI **transitively across the entire main
   Uptime module**. The RoomActor precedent does NOT carry — RoomActor is **Engine-only**; the device drags the
   heavy plugin/engine dep (a different profile). The Cathode plugin stays world-agnostic.
2. **RQ2 — the device/material reference — RESOLVED (browse-design) conventions** (the rest left to the dev):
   (1) **Unlit/emissive faithful display** — the sRGB chain consistent end-to-end (sRGB PF_B8G8R8A8 → sRGB-flagged
   `UTexture2D` → unlit emissive, NO second gamma; verify no double-gamma/tonemapper distortion). (2)
   **Texture-param name `SurfaceTexture`** — one shared conventionally-named param all Cathode-surface materials
   bind. (3) **MID lifecycle** — the device OWNS its MID (lifetime tracks the actor); **rebind the texture param on
   BOTH init AND egress-texture realloc** (missing the rebind-on-realloc = a stale-texture bug); **1:1
   device:surface:texture:MID**. Left to the dev: the material asset name (`M_` convention), the graph internals,
   the MID creation call site, the device mesh/placement.
3. **RQ3 — the display proof shape (slice-0 spike resolves; one human branch).** The empirical scene-capture
   (feasible iff a world/PIE box test runs + the headless emissive capture works) vs the bind+content+placement
   floor + a named display residual. If feasible → **is the scene-capture a hard gate or the empirical upgrade
   over the floor?** — the human's risk-tolerance call (the 024-RQ3 precedent), routed after the spike. (NB: unlike
   024-RQ3, the feasibility here is a genuine render/env fact the spike grounds, not a misattributed premise.)
4. **RQ4 — the per-frame drive (resolved): a `UTickableWorldSubsystem`** (proven precedent `UUptimeSimSubsystem`;
   the design's "one runtime drives each live surface" — one registry + `DriveAll`/frame). Lives in the RQ1 module.
5. **RQ5 — live vs static — RESOLVED (PM): static display + per-frame drive is the 026 floor.** The PM's "live"
   means *driven by the real composition pipeline* (the per-frame `DriveAll` of a real-composition surface,
   displayed in-world), NOT animated content changes. The DOM-mutate-and-redrive (the two-frame content-change
   proof) is a **residual / follow-on**, not in 026 scope. 026 delivers the static display + the per-frame drive.

## Outcome

026 realized the browse epic's culmination: **an in-world device displays a live Cathode surface — the first
end-to-end visible Cathode screen.** The dedicated game-side **UptimeCathodeDevice** module's
`UCathodeDeviceSubsystem` (a `UTickableWorldSubsystem` owning the DOM + the `ISurfaceRegistry` + the
`CathodeEgress`) drives the **real** `CathodeComposition` surface → the egress `UTexture2D` per frame, and an
`ACathodeDeviceActor` (a Plane + a texture-sampling `M_CathodeScreen` MID sampling `egress.GetTexture`, applied to
slot 0, bound post-first-drive) displays it; a created-world `SceneCapture` of its screen tracks the composed
content (the RQ3 hard-gate display proof).

**Shape (every fork to its owner):** RQ1 (the game-side module) + RQ2 (the material conventions) — browse-design;
RQ3 (HARD GATE) + RQ5 (static + per-frame drive) — tcdent. The slice-0 spike de-risked the first-ever-world
display proof (feasible). The re-slice: **0** spike / **1** the real composition→egress bind (closes 024 residual
a) / **2** the device display.

**Commits:** slice 1 `0ea086c` (the real composition→egress bind, residual a closed); slice 2 (the device actor +
material + display — code + the env-authored `M_CathodeScreen.uasset`, committed together for tree coherence); this
close (COVERAGE + retrospectives).

**The proof (reviewer-accepted, on-box, 125/125):** the device's screen displays the composed content — the
scene-capture R-dominant frac in **[0.01, 0.60]** tracking the composed red box (non-circular off the capture's RT,
the real composition→egress→MID→screen→capture chain, NOT a proxy/constant/re-read; the range bites a stray pixel
AND a full-screen constant); the MID bound post-first-drive (the lazy texture); the floor (MID-on-slot) the
structural binder beneath; the shader-force test-only (production-render the named inference); single-gamma sRGB.
**Uptime is now fully Cathode-free** (source 0-Cathode + the relink 0-pull — the engines confined to the device
module). **The browse epic's pipeline is complete end-to-end:** 016 real JS → … → 023 the composed pipeline → 024
the texture egress → **026 the live in-world display.**

**Residuals the next epic inherits:** the real `FDomDocument` HTML content (the JS stack — no HTML-string→DOM
factory); the rebind-on-realloc empirical exercise (deferred under RQ5 static); the production A2-staged font; the
production surface config (per-device dims/content); multi-device / the scale demo (the proof is 1:1); and the
in-flight `M_CathodeScreen.uasset` env-commit.

## Lead Retrospective — uptime.browse-lead

The epic's culmination was its longest, most layered de-risk — the first production world-rendering path in the
codebase, ground through an eight-cycle spike. What the loop got right: the spike **banked the genuinely-new
unknown early** (a created `UWorld` + `CaptureScene` renders headless — the first-ever world test) and then
isolated the emissive-render long tail rather than committing the device build against an unproven display path. A
two-variable **confound** (a material change paired with a removed light) was caught — the dev owned it and env
independently flagged the config delta — and I routed it to the reviewer rather than either rubber-stamping the
all-black as "infeasible" or letting it be waved away: re-isolating to get a *clean* dispositive signal is
consistent with the conservative line, not the over-read trap (which bites re-attributing a *clean* failure). The
granted one-fix was aimed at a verified defect (shader validity — grey == the engine default; `FinishAllCompilation`
a render-side force, not an authoring tweak, so it *discriminates*), and feasible meant the captured color tracked
the *specific* authored value, never "pixels returned." Every fork went to its owner — RQ1/RQ2 to browse-design,
RQ3/RQ5 to tcdent — and when tcdent asked a **factual** question (test segregation) before ruling RQ3, I grounded
it via the dev (the session is already required by 024's hard gate; the GPU set is name-segregable) with the
inferences marked, so he ruled on the merits, not a guess. The mechanism-vs-live-path discipline (real composition
not synthetic, real device not a proxy, texture-sampling not a constant) was relayed as a build requirement at each
slice; and I held the slice-2 code commit for the env-authored `.uasset` — a committed-but-failing test is worse
than waiting.

The honest miss is mine: my slice-1 framing assumed a `CreateDomDocument(html)` factory that doesn't exist
(CathodeDom has no HTML-string parser; a real DOM needs the full JS stack). The dev grounded it and correctly
scoped the slice to a real *inline* `ILayoutDocumentSource` with the real-DOM content deferred as a residual. The
lesson: a convenient factory in a framing is a phantom until grounded — the past-shaped diligence (does the API
exist?) belonged in the framing, and the dev's grounding caught it before it became a dead end.

## Review Retrospective — uptime.browse-review

(Appended via agent-message; folded for the record.) Wiring the proven pipeline (023 compose / 024 egress)
into the first **live in-world screen** made the load-bearing reviewer discipline **distinguishing the mechanism
from the live path at every integration level** — each slice had to prove the *real* thing, never re-prove a
mechanism a prior slice or the spike already owned. Slice 1 (the composition→egress bind) hinged on
**real-composition-not-synthetic**: a synthetic `FSyntheticEgressTarget` re-proves 024's *upload*, not the bind,
so residual (a) closes only when the *real* `MakeCompositionSurfaceRegistration` drives the egress — and the
expected color must be the **non-circular standalone Relayout** (an independent layout of the same DOM), *not*
the captured `GetReadyFrame` (which the egress *uploads*, so `readback==GetReadyFrame` proves the upload, not the
source). Slice 2 (the display) hinged on **real-device-not-a-proxy + texture-sampling-not-a-constant**: a
spike-style proxy quad or the spike's *constant* emissive re-proves slice-0's *capture mechanism*, not the real
device displaying the real composition — so the proof must spawn the real `ACathodeDeviceActor`, whose MID
*samples* `egress.GetTexture`, and the capture must track the **composed** R255G0B0 (the slice-1 content), not an
authored constant. Same trap at every level; the question is always "does this green prove the new live edge, or
re-prove the mechanism beneath it?"

The slice-0 **display-feasibility spike** was a multi-cycle de-risk where the discipline was *banking what was
clean and isolating what was confounded*. The world+capture infrastructure (the genuinely big first-ever-world
unknown — a created `UWorld` with a valid renderer `Scene`, `CaptureScene` headless, the non-circular RT readback)
banked early; the **emissive render** was the long tail. A first failure was an **all-black confound** (the dev
changed *two* variables — material *and* light — env independently flagged it): a confounded result is not a
clean dispositive signal, so **re-isolating to get one is consistent with the conservative line, not the
over-read trap** (which bites re-attributing a *clean* failure to invent an artifact). The clean isolation read
the default grey — a **verified** defect (grey==engine-default, not black [unconnected] or R-dominant [correct];
Unlit took; two materials → the same default), so the granted **one verified-defect fix** (`FinishAllCompilation`
— a render-side compile force, *not* an authoring tweak, so it discriminates an uncompiled-shader-map from a
deeper limitation) was spent on the right leg. Feasible meant the captured color tracked the **specific** authored
value (the `G=0.2`-matches-the-asset tell, |R−0.80|=0.0002), never "pixels returned" — and the shader-force was
named a **test-context enabler**, absent from device code, with the production render an explicit inference.

Two more that generalize. The display discriminator is a **two-sided RANGE** (`frac ∈ [0.01, 0.60]`): a >0-only
check passes a stray pixel, and a full-screen check passes the spike's old constant — the real ~10% composed box
lives *between* the degenerate forms, so the gate must bite **both** (a stray pixel below, a full-screen constant
above). And the **dep-confinement firewall is strongest by source+link together**: `Uptime.Build.cs` 0 Cathode
(in-tree dispositive) *and* `UnrealEditor-Uptime.dll` relinking with zero engine pull (the spike TU removed →
Uptime fully Cathode-free) — the engines confined to the dedicated device module. **Residuals the follow-on
inherits:** the live DOM-driven content-change (the two-frame v1→v2 proof, RQ5-deferred); the real `FDomDocument`
HTML content (the JS stack — `CreateDomDocument` has no HTML parser); the `M_CathodeScreen.uasset` env-commit (the
tree is not coherent until the box-authored asset is pulled+committed — a blocking follow-on, the proof valid on
the box); and the production-render inference (the shader-force is test-context-only).

## Dev Retrospective — browse-dev

The first end-to-end visible Cathode screen. What shaped the build:

**Spike-first de-risk paid off — but the tail was long.** Slice 0 banked the two genuine render/env unknowns early:
a created `UWorld(EWorldType::Game)` renders a headless `SceneCapture2D` IFF `World->Scene` is non-null, and an unlit
emissive sidesteps Lumen. Those banked in the first cycles. The long tail was the emissive RENDER: a pre-authored
material asset fell back to default-grey in the created-world automation context. The discipline that resolved it was
single-variable isolation — env flagged a confounded cycle (two variables changed: the material AND a removed light),
I re-isolated to ONE variable (material only, light back), and the one-fix landed: `ForceRecompileForRendering` +
`FinishAllCompilation` force the asset's shaders render-ready (the created-world loads them uncompiled). The
load-bearing read was grey-vs-Unlit=YES: the shading model took but the render fell back → the SHADER was invalid, not
the shading model and not an unconnected emissive (that reads black). The confound-catch saved a false "infeasible"
floor.

**The re-slice was the right shape.** The original framing bundled the whole device path into one proof. Grounding
showed the real seams: slice 0 (the display-proof feasibility spike, throwaway), slice 1 (the real composition→egress
bind — 024 residual (a), the load-bearing wiring the egress had only proven with a synthetic source), slice 2 (the
device actor + the empirical scene-capture display). Each slice had a clean discriminator; the spike's honest REDs
drove precise narrowing.

**The load-bearing catches (where a weaker proof would have passed):**
- *Texture-sampling material, not a constant.* The slice-0 `M_CathodeScreen` was a constant emissive (it proved the
  capture MECHANISM). Slice 2 re-authored it to sample a `SurfaceTexture` param — so the captured color tracks the
  COMPOSED content via the MID's texture sample, not an authored color. A constant would have passed a naive capture.
- *Capture of the REAL spawned device, not a proxy.* The scene-capture renders the actual `ACathodeDeviceActor`
  (mesh + MID), fed by the real composition→egress — not a spike-style stand-in quad with a hand-set texture. A proxy
  would have re-proven slice 0, not the real device displaying the real composition.
- *The discriminator range, not `>0`.* The reviewer's strengthening: R-dominant fraction in `[0.01, 0.60]` (the box
  is a solid ~10% block) bites a stray pixel (`<0.01`) AND a full-screen constant (`>0.60`), where `>0` would pass a
  single noise pixel. The capture landed at `0.1152` — the composed red box on the body's default-white background,
  the discriminator correctly isolating the box.
- *MID-bind post-first-drive (empirical).* The egress texture is lazy — null until the first `DriveAll`. Binding at
  `BeginPlay` binds null → no display. Proven empirically (null-before-drive, bound-after-drive). Rebind-on-realloc is
  built + source-confirmed but not exercised (the RQ5-static floor triggers no realloc) — flagged, not claimed.
- *Shader-force test-only.* The created-world shader force is a TEST enabler; production renders compiled shaders
  normally. Grep-absent from the device actor + the subsystem (and kept the literal tokens out of production comments
  so the gate-grep stays clean).

**Build gotcha banked.** A `UObject` holding `TUniquePtr<incomplete>` hits C4150 even WITH an out-of-line dtor — the
UHT `.gen.cpp` instantiates the dtor against the forward-decl. Fix: complete types in the header (the Cathode headers
are neutral, so dep-confinement is unaffected).

**The env-authored-asset commit-path finding.** `M_CathodeScreen.uasset` is authored on the build box (editor
Python); the proof is valid (the box ran green with the box-authored asset), but the TREE needs the asset
pulled-from-box + LFS-committed — without it a fresh checkout's `RealDeviceSceneCapture` goes RED (the code
`LoadObject`'s it). The code is correct + accepted; the coherent commit is the code + the asset together. Surfaced as
a required follow-on (env via the PM) rather than committing incoherent code. A reusable pattern for any
box-authored-asset deliverable.
