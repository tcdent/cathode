# Sprint 032 — Cathode Demo Map (the standalone multi-surface demo)

> **DRAFT — team-forming.** PM direction (tcdent, 2026-07-01): "a standalone example map inside the
> Cathode subproject: screen surfaces positioned on a grid, multiple live Cathode surfaces rendering
> simultaneously... launch into the map and see many different screens running at once. tcdent
> provides the renderable screen-surface assets — scaffold the map and grid placement now, assets
> drop in when ready." References `grid-conventions.md` (the grid) + the 031 example (the wiring).
> Two light PM confirms folded in: **FORK B** (plugin-Content home — the host-agnostic reading) and
> **FORK D** (the N + grid arrangement — a demo-visual taste call).

## Scope Boundary

This sprint scaffolds a **standalone, host-agnostic** Cathode demo map: a plugin-side demo director
spawns N screen actors on a grid, installs N distinct live surfaces through the 029 #27 fan, binds
each actor to a surface, and pumps — so launching the map shows many different screens at once. The
C++ scaffold is dev-built and headless-proven; the `.umap` + placeholder assets are the editor/content
half; tcdent's real screen assets swap in later (a data-only swap).

**Owns:**
- **The demo owner-driver** — `ACathodeDemoDirector` (a plugin actor in **CathodeExampleDevice**
  [Engine-linking — already hosts the sampler actor + CathodeComposition; **+CathodeExample** for the
  shared `FExampleGameBridge`, a plugin→plugin dep, ZERO `Uptime*`], **demo-scoped**: placed in the demo
  map, NOT a global `UWorldSubsystem` that would run in every world). Owns the
  `FCompositionSurfaceOwner`, `Install`s N specs (N distinct placeholder bundles) + the shared bridge,
  `Pump`s per Tick.
- **The grid-spawner** — at `BeginPlay`, spawns N `ACathodeExampleDeviceActor` at `tile_center(i,j)`
  positions + facing (the grid CONVENTION re-expressed: `col*60+30`, host-agnostic — NOT a UPTIME
  `grid.py`/room-placer import), binds each to `GetSurfaceId(i)`.
- **N distinct placeholder screens** — N inline placeholder bundles, visibly distinct per screen
  (color/label, the #27 A=green/B=red shape) so "N different" reads immediately; each also READS `game.*`
  (a live `game.tickCount` per screen) — showcasing the 031 bridge (registration → the screen reads it),
  since the shared bridge is installed anyway.
- **The demo map `.umap`** — `Dev_LitBase`-duplicated (lighting never hand-authored) + the director
  actor; the placeholder screen mesh (Plane) + material.
- **The plugin Content setup** — `Cathode.uplugin` `CanContainContent=true` (false today) + a
  `game/Plugins/Cathode/Content/Maps/Dev/` dir + the `/Cathode/` mount + a plugin copy of the screen
  material.

**Does NOT own (named, with the owner):**
- **The real renderable screen assets** — tcdent's; they DROP IN later by re-assigning the actor's
  mesh/material (zero code change — the 031 seam).
- **Any `Cathode → host` dep** — no UPTIME `grid.py`, no game subsystem, no room-placer. The demo is
  host-agnostic (the arrow is `game → Cathode`, the 031 inversion — a UPTIME dep is the reject).
- **A global owner-driver** — the director is demo-scoped (a placed actor); a `UWorldSubsystem` would
  run in the real game.
- **Re-proving the 029 #27 fan / the 031 actor / the grid math** — the demo INSTANTIATES them; it does
  not re-prove the no-bleed isolation, the egress→MID sample, or the placement formula.

## Problem

No Cathode demo map exists (the plugin has the 031 actor but no map, no Content). And nothing
plugin-side owns+installs+pumps a multi-surface owner: 031's `ACathodeExampleDeviceActor` is ONLY the
per-screen sampler (`BindSurface`'d to an EXTERNAL owner — the "integrator's subsystem" it was designed
against); 030's `UCathodeDeviceSubsystem` is the owner-driver but host-side, single-surface, and
Uptime-tied. So a standalone multi-surface demo has no driver, no map, and no grid placement.

## Goal

> PM (tcdent, 2026-07-01): "launch into the map and see many different screens running at once...
> scaffold the map and grid placement now, assets drop in when ready."

Finishing: launch the standalone Cathode demo map → N visibly-different live Cathode screens render
simultaneously on a grid; the real screen assets swap in with no code change.

## Final State

- **`ACathodeDemoDirector`** (plugin, demo-scoped) — owns `FCompositionSurfaceOwner`; `Install`s N
  distinct placeholder bundles + the shared `FExampleGameBridge`; at `BeginPlay` spawns N
  `ACathodeExampleDeviceActor` at `tile_center` grid positions, binds each to `GetSurfaceId(i)`; `Pump`s
  per Tick. N + the arrangement are a config (FORK D).
- **The grid** — the convention re-expressed host-agnostic (`GRID_UNIT=60`, `col*60+30`, facing), NOT
  a UPTIME import.
- **The demo map `.umap`** (`Dev_LitBase`-dup + the director) + the placeholder Plane mesh + screen
  material (plugin Content).
- **`Cathode.uplugin`** `CanContainContent=true` + the plugin `/Cathode/` Content mount.

## In Scope (ordered slices)

- **Slice 1 — the C++ scaffold (dev-buildable, headless-proven).** `ACathodeDemoDirector` (owns the
  owner, `Install`s N distinct placeholder bundles + the bridge, `Pump`s) + the grid-spawner (N
  `ACathodeExampleDeviceActor` at `tile_center(col,row)` + facing, bound to `GetSurfaceId(i)`). *Proof
  — placement + wiring, in a TEST WORLD (CreateWorld + RestartWorkerForTesting + Pump; "headless" =
  no-PIE/no-render; NOT re-proving the #27 fan):* the director spawns N actors at the
  CORRECT `tile_center` positions (assert the transforms against the convention), each bound to a
  DISTINCT surface (assert `actor.GetSurfaceId() == the bound GetSurfaceId(i)`, all N distinct), N
  non-null live textures, and N distinct bundles installed. Host-agnostic: the director's module
  `Build.cs` deps ZERO `Uptime*` (grep the raw block); no `grid.py` import. Demo-scoped (a placed
  actor, not a `UWorldSubsystem`).
- **Slice 2 — the map + content (editor; the integrated visual).** The demo map `.umap`
  (`Dev_LitBase`-dup + the director actor placed) + the placeholder screen material/mesh + the plugin
  Content setup (`CanContainContent` + the mount + the material copy). *Proof — the integrated
  PIE/scene-capture:* launch the map → N VISIBLY DIFFERENT screens rendering simultaneously (the
  human's screenshot / a scene-capture binding N distinct rendered contents — NOT a re-run of the #27
  isolation assertion). Authored via the editor/Python-scripting channel (a `.umap` is a binary editor
  asset, not C++).
- **Assets drop-in (later, tcdent).** The real screen assets replace the placeholder mesh/material by
  re-assignment — zero code change (the 031 data-only swap seam).

## Non-Goals

- The real renderable screen assets (tcdent's; the data-only swap later).
- Any `Cathode → host` dep (`grid.py`, a game subsystem, the room-placer) — the inversion's reject.
- A global owner-driver `UWorldSubsystem` — the director is demo-scoped.
- Re-proving the #27 no-bleed isolation, the 031 egress→MID sample, or the grid math — DEMONSTRATE.
- Authoring the real screens' content (the placeholder bundles are distinct-but-minimal; the real
  screens are tcdent's).

## Starting Surface (the dev's audit, confirmed)

- **The #27 fan (BUILT):** `FCompositionSurfaceOwner` `Install({N specs})` → N per-surface documents →
  N textures (`GetSurfaceId(i)`/`GetTexture`); `CathodeMultiSurfaceFanTests` the N-surface precedent.
- **The 031 sampler actor (BUILT):** `ACathodeExampleDeviceActor` — a screen-mesh actor, `BindSurface`
  + Tick samples `GetTexture` → a MID; mesh/material integrator-assigned (the swap seam).
- **The 031 bridge (BUILT):** `FExampleGameBridge` — the shared `game.*` bridge (one-global).
- **`grid-conventions.md`:** `GRID_UNIT=60`, `tile_center(i,j)`, facing 0..3, `Dev_LitBase` to dup.
- **`Dev_LitBase.umap`** (`game/Content/Maps/Dev/`), **`M_CathodeScreen`** (`game/Content/Cathode/`).
- **The gap:** the demo owner-driver (new), the grid-spawner (new), N bundles (new), the `.umap`
  (editor), the placeholder assets (content), the plugin Content setup.

## Method

- **The C++/content split:** S1 is the dev-built C++ scaffold, headless-proven (placement + wiring); S2
  is the editor/content half (the `.umap` + material, via the editor/Python channel + the integrated
  PIE). A `.umap` is a binary editor asset — not authorable in C++ (the 031 content-follow-on shape).
- **Demonstrate, don't re-prove:** the demo INSTANTIATES the fan (N surfaces), the actor (egress→MID),
  and the grid (placement); it does not re-run the #27 isolation, the actor's sample proof, or the
  placement formula.
- **Host-agnostic invariant:** the director's module deps ZERO `Uptime*` (raw-block grep); the grid is
  the convention re-expressed (`col*60+30`), never a `grid.py` import — a UPTIME dep breaks the 031
  inversion.
- **The assets-later seam is data-only:** the director exposes the screen mesh/material as UPROPERTY
  (data, passed to the spawned actors) — NOT `FObjectFinder`-hardcoded; S1 leaves them unset/placeholder
  (placement + wiring, no render); S2 + the real assets set them by re-assignment (no C++ asset path a
  real asset fights — confirm at the gate).

## Acceptance

- **S1 (placement + wiring, in a TEST WORLD):** the 030-joined-test shape (CreateWorld +
  RestartWorkerForTesting + spawn the director + Pump until N textures non-null — "headless" =
  no-PIE/no-render: placement + wiring + texture-liveness, not on-screen pixels). N actors spawned at the
  CORRECT `tile_center` transforms (asserted against the convention), each bound to a DISTINCT surface
  (`GetSurfaceId` match, all N distinct), N non-null live textures, and **N distinct bundles by installed
  CONTENT** (assert the distinctness on the bundle content, not merely N spec slots — catches an
  N-identical-copies bug); **the director's `Build.cs` deps ZERO `Uptime*`** (raw-block grep,
  comment-excluded); demo-scoped (a placed `AActor`, not a `UWorldSubsystem`); no regression.
- **S2 (integrated visual):** launch the demo map → N VISIBLY DIFFERENT screens rendering
  simultaneously — the discriminating assertion is **"N DIFFERENT"** (per-region distinct color/label),
  NOT "N present" (S1 has non-null) and NOT the #27 isolation. A scene-capture automation asserts N
  DISTINCT contents; the human's screenshot is the human's confirmation + the reviewer verifies the
  STRUCTURAL. **The screen material is the PLUGIN's copy (`/Cathode/`), NEVER `/Game/`** — a `.umap`/actor
  referencing the game-side `M_CathodeScreen` is an asset-reference `Cathode → host` arrow (the
  inversion's reject, same class as a code dep); verify the refs resolve to `/Cathode/`. The placeholder
  mesh/material assigned data-only (the swap seam intact — no code path a real asset fights).
- **Host-agnostic intact** (the demo deps no `Uptime*`, the arrow `game → Cathode`); **no regression**
  (the full Cathode suite + the 031 baselines).

## Review Questions (forks)

1. **FORK B — the map/assets home (light PM confirm; ruled host-agnostic in-group).** PLUGIN Content
   (`CanContainContent` + a plugin `/Cathode/` mount + a plugin material copy) — settled by the PM's
   "standalone example inside the Cathode subproject" + the 031 inversion (a UPTIME dep is the reject).
   Confirming the reading, not re-ruling. *(Cost: the `CanContainContent` flip + the mount + a material
   copy.)*
2. **FORK D — the N + the grid arrangement (PM taste).** How many screens + the shape/spacing (e.g. a
   3×3 of 9 on a wall facing PlayerStart, or a row/arc). N is parameterized (the scaffold works for any
   N); the specific N + arrangement is a demo-visual — lead default a modest grid (e.g. 3×3=9) facing
   the spawn, confirm/tune.
3. **Resolved in-group:** FORK A (the demo owner-driver = a plugin demo ACTOR, demo-scoped); FORK C
   (the grid placement = a C++ spawner, the convention); FORK E (the N screens = inline distinct
   placeholders now, authored screens tcdent's later); FORK F (the placeholder = Plane + screen
   material, the data-only swap seam).
