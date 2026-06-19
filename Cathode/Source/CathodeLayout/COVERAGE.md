# Cathode Layout & Style — component coverage & accountable deferrals

Durable accountability record for Layout & Style (sprint 005, no-build/no-run scaffold). It maps every
responsibility in [`docs/browse/components-layout-style.md`](../../../../../docs/browse/components-layout-style.md)
to its status: **COVERED** (slice + mechanism) or **DEFERRED** (reason · dependency · what unblocks it ·
first-execution risk). Nothing in the component doc is silently missing.

This file is the accountability of record. Every in-code `TODO`/cross-ref (e.g. "003 #3", "validation
pass") resolves to an entry here.

## Covered

| Responsibility | Slice | Mechanism |
|---|---|---|
| **Lay out the document** (DOM + CSS → positioned geometry) | S2 + S3 | `FLiteHtmlRelayoutBackend::Render` feeds markup through `litehtml::document::createFromString` → `render(width)` (litehtml confined to `CathodeLayoutEngine.cpp`); the result is read out as neutral geometry (below). litehtml's computed boxes are read via `QueryNodeBox` (`get_placement`) — **discharged + proven** in sprint 011 [c] (relational invariants vs litehtml's own boxes). |
| **Parse and cascade CSS** (CSS 2/3, flexbox; no grid; partial CSS3) | S2 + S3 | Same `createFromString(html, container, master_css, user_css)` path — litehtml owns the cascade; we feed the document + author CSS. Coverage scope = litehtml's (grid is a doc-deferred gap). |
| **Drive relayout on dirty** — BOTH complete + intelligent dirty-DOM rerender **BUILT** | S3 + S4 | `FDirtyTracker` (mark/flush, layout-vs-paint), `ComputeRelayoutPlan` (affected-subtree selection: walk up to the nearest `IsLayoutBoundary`, root-reaching ⇒ full, paint-only ⇒ no relayout — pure graph, litehtml-free), `FRelayoutController` driving `IRelayoutBackend` down **both** paths (`RelayoutDocument` / `RelayoutSubtree`). Dirty-DOM rerender is **built, not deferred**; only the offset *value* + efficient==full *proof* are validation-pass. |
| **Route paint** (document_container → neutral paint-op stream) | S2 + S4 | `FCathodeDocumentContainer` callbacks → `Emit*` helpers → `TArray<FLayoutPaintOp>`. **Full border routing**: `DecomposeBorder` (solid+square → 4 `FillBox` edges; any styled side / corner radius → one neutral `StrokeBorder` op). Clip via `EmitClipPush`/`EmitClipPop`. The splice transform `OffsetPaintOps` is built + tested. |
| **Surface computed geometry + resolved style (observation)** | S5 | `WalkGeometry` (pre-order DFS over `ILayoutDocumentSource`, one `FLayoutNodeGeometry{NodeId, Box, Style}` per node — walk/assembly are ours) + `MapResolvedStyle` bounded **by construction** to `ContractStyleProperties()` (box-model + visual subset, not the full CSSOM). Backs `getBoundingClientRect` / `getComputedStyle`; the vendor BOX read (`QueryNodeBox`) is **discharged** (011 [c] — real `get_placement` through the observation read path), while the per-property computed-VALUE read (`QueryComputedValue`) stays deferred (see #2). |
| **Delegate text measurement/shaping to Text & Fonts** | by design | The neutral `ITextMeasurer` input seam (`ResolveFont`/`MeasureWidth`/`GetFontMetrics`); the container's `create_font`/`text_width` route through `FLayoutFontCache` to it. **No `hb_*` / no `CathodeText` coupling** (grep-clean). Produces 004's measurement-*consumer* side. |
| **Delegate rasterization to the Rasterizer** | by design | Output is OUR neutral `FLayoutPaintOp` stream (incl. `StrokeBorder`, `ClipPush`/`ClipPop`) — mapped to 003's `FPaintOp` at integration. **No `BL*` / no `CathodeRasterizer` coupling** (grep-clean). Produces 003's draw-call source + the `StrokeBorder` the Rasterizer's 003 `COVERAGE.md` #3 stroke op will consume. |
| **Consume the DOM tree** | S2 + S3 | The neutral retained-DOM seam `ILayoutDocumentSource` (tree structure for selection + markup for the re-feed). **The source impl is BUILT (sprint 020-confirmed): `FDomDocument` IS the `ILayoutDocumentSource` (CathodeDom.h:253), self-wired via `Layout.SetDocument(*this)` (CathodeDomDocument.cpp:27); `Relayout` drives it (`Render(Source.GetDocumentHtml()/GetSubtreeHtml(), …)` + the structure-walk for dirty selection).** See the sprint-020 status below. |
| **Mutation strategy (fork vs re-feed) — doc-open** | S3 | DECIDED as **strategy-agnostic re-feed** behind `IRelayoutBackend` (complete + subtree both built); the litehtml-fork remains a contingency the seam can adopt without breaking callers. Knobs fixed, no tuning surface. |
| **Knobs (finalized, minimal — no tuning surface)** | S2–S5 | Viewport/DPI/default-font defaults (`1920×1080`, `96` dpi, `sans-serif`/`16`) fixed in `FLiteHtmlRelayoutBackend`; litehtml `master_css` as the UA sheet; the font-cache dedup key; the `ContractStyleProperties()` set (widening it is a deliberate contract change). None are runtime-tunable. |

## litehtml on the real Dom + Resolver — status (sprint 020)

litehtml runs on the **real `FDomDocument`** (the adapter is `FDomDocument` itself, self-wired) and the **real
Resolver** is test-bound. The DOM-to-CSS round-trip is proven on real data:

- **Real-Dom adapter — BUILT + Relayout-driven.** `FDomDocument : public ILayoutDocumentSource` (CathodeDom.h:253)
  self-registers in its ctor (`Layout.SetDocument(*this)`, CathodeDomDocument.cpp:27). `Relayout` feeds litehtml via
  the adapter's markup-refeed (`Source.GetDocumentHtml()` → `createFromString`, CathodeLayoutEngine.cpp:390/500) +
  the structure-walk (`GetRootNode`/`IsLayoutBoundary`, CathodeRelayout.cpp) for dirty-region selection. The
  re-serialization IS the adapter contract (litehtml is `createFromString`-based by design), NOT a bypass.
- **getComputedStyle real values + inline re-cascade — ALREADY PROVEN over the real `FDomDocument` + real litehtml**
  (sprints 013/014; only the text measurer is stubbed, the cascade is real). `Cathode.Dom.Style.*` / `…Opacity` /
  letter-spacing / `…ReCascade` assert cascade-COMPUTED values (50%→100px, 0.1em→1.6px, an inline write → re-cascade →
  the new value). 020 did **not** re-prove these — the premise correction (the stated proof was already done).
- **Linked-CSS through the REAL Resolver — PROVEN (020-A1), test-wired.** `SetResourceResolver(CreateResourceResolver(
  CreateUEFileBundleSource(<test fixtures dir>)))` on a real `FDomDocument`'s litehtml engine; a `<style>@import
  url('linked.css')` → litehtml's OWN parse fires `import_css` → `RouteLinkedCss` → the real `FUEFileBundleSource`
  `FFileHelper` read → the cascade → `getComputedStyle(#target).color == 'rgb(255,0,255)'` (the linked value). Proof:
  `Cathode.Dom.Resolver.LinkedCssRoundTrip` (a NULL-resolver control pair: null → the default, distinct → the linked
  value provably came THROUGH the real resolver). The `@import`-in-`<style>` → `import_css` trigger is
  EMPIRICALLY confirmed (the vendored litehtml is prebuilt; grounded vs upstream v0.7 — the green pair IS the
  confirmation, not a hand-call [that's the existing RouteLinkedCss routing coverage], not `GetDocumentCss` [empty]).
- **A2 — production wiring DEFERRED (the next integration sprint, a tcdent tooling gate).** 020-A1 is **test-wired** (a
  test RootDir). NO production `SetResourceResolver` caller; NO production default-root; the 019 deploy-coupling
  (the `/Game/Cathode/` default-root + `DirectoriesToAlwaysStageAsUFS` staging + the cooked-pak validation) stays
  deferred-together. A2 ships the production wiring + the deploy-coupling together.

## Deferred — with accountability

### 1. Boundary-offset VALUE + efficient==full EQUIVALENCE
- **Reason:** `GetBoundaryOffset` (the subtree boundary's prior-layout position, used to splice subtree paint ops into document coords) returns `ZeroVector` — the real value is litehtml's placed position, which needs a render. And proving the intelligent path produces the *same* result as a full re-feed needs both to actually run.
- **Dependency:** an execution-sprint build + run.
- **Unblocks:** the validation pass (execution-approval sprint).
- **First-execution risk:** subtree paint ops splice at a zero offset (visually wrong placement) until the boundary-offset query is bound; the efficiency win is unproven until the equivalence holds. The transform that consumes the offset (`OffsetPaintOps`) and the path selection are built + tested.

### 2. Computed-STYLE-string read (`QueryComputedValue`) — style fidelity
- **DISCHARGED in sprint 011 [c] — the BOX read (`QueryNodeBox`):** now returns litehtml's real `get_placement()`
  via the retained `litehtml::document` + the `data-node-id` NodeId↔element map. Proven by
  `Cathode.Layout.LiteHtml.ComputedBoxes` (the engine) and `Cathode.Dom.Observation.RealLayoutGeometry` (the
  observation read path), both on relational invariants against litehtml's OWN boxes. No longer deferred.
- **Still deferred — `QueryComputedValue` (the per-property computed-STYLE-string read):** litehtml exposes
  computed style as TYPED properties (`css().get_display()` etc.), not name-keyed strings, so a real read needs a
  litehtml-typed→string mapping over the bounded `ContractStyleProperties()` set. It was never one of the four
  [c] geometry/decode/GC targets (the [c] oracles are boxes) — it is a style-fidelity follow-on, not a [c] gap.
- **Dependency / re-tie:** the DORMANT CSS axis — Cathode's own litehtml-grounded CSS declaration (Authoring/,
  roadmap). The declared CSS contract and the computed-style read are one style-fidelity track; resolve them
  together. **Unblocker:** the litehtml-typed→string mapping.
- **First-execution risk:** `getComputedStyle` returns empty property values until the typed→string read is
  bound; the BOX read (`getBoundingClientRect`) is unaffected (discharged). The walk + bounded mapping are tested.

### 3. Styled / rounded-border DRAW
- **Reason:** Layout *produces* the neutral `StrokeBorder` op for any styled side or corner radius; the op that actually draws it is the Rasterizer's stroke / rounded-rect / path op.
- **Dependency:** the Rasterizer stroke op — [`CathodeRasterizer/COVERAGE.md`](../CathodeRasterizer/COVERAGE.md) **#3 (Styled / rounded borders)**.
- **Unblocks:** when that op lands + integration maps `StrokeBorder` → it.
- **First-execution risk:** styled/rounded borders are emitted but render square or missing until the Rasterizer draw op exists (solid square borders already decompose to `FillBox` edges and render).

### 4. Image byte LOADING + size-decode
- **Reason:** the container *routes* images (`draw_background`/`draw_list_marker` → `EmitDrawImage` with a neutral source ref) and exposes the info seam (`get_image_size`/`load_image`), but resolving the source to bytes + real dimensions is downstream. **The Resource Resolver is now BUILT (sprint 019) and the linked-CSS resolve-path through it is proven (020-A1: `import_css` → `RouteLinkedCss` → the real `FUEFileBundleSource`)**; the IMAGE path (`load_image`/`get_image_size`) is the same router shape but still stubbed (size `0×0`, load no-op), and the size needs a 003/Blend2D image-info decode over the resolver's bytes.
- **Dependency:** the production resolver wiring (A2) for `load_image`; a 003 decode-for-size seam for `get_image_size`.
- **Unblocks:** A2 (image bytes) + the rasterizer decode-for-size (dimensions).
- **First-execution risk:** images route by ref but draw at zero size / with unresolved bytes until A2 wires the resolver + the decode-for-size lands; layout that depends on intrinsic image size is wrong until then.

### 5. Cross-module WIRING — source impl DONE (sprint 020); measurer/paint-op + production resolver wiring deferred
- **DONE (sprint 020):** the `ILayoutDocumentSource` impl is `FDomDocument` (self-wired, Relayout-driven — see the
  sprint-020 status above); the Resolver bind (`SetResourceResolver`) is proven **test-wired** over the real
  `FUEFileBundleSource` (`Cathode.Dom.Resolver.LinkedCssRoundTrip`).
- **Still deferred:** (a) the **production** `SetResourceResolver` caller + the 019 deploy-coupling (the default-root +
  `DirectoriesToAlwaysStageAsUFS` + the cooked-pak validation) = **A2**, the next integration sprint (a tcdent tooling
  gate); (b) 004 backs the measurer (today a stub measurer); (c) 003 consumes the paint ops (`FLayoutPaintOp` →
  `FPaintOp`, `FLayoutFontRef` → `FFontHandle`) — the rasterization/egress sprint.
- **First-execution risk:** the inline + linked-CSS round-trips run (test-wired); production resource resolution +
  cross-module paint/measure are future. Layout stays litehtml-confined + `BL*`/`hb_*`-free.

### 6. Hit-testing (point → element) — OUT of scope
- **Reason:** litehtml provides it, but it existed only to serve pointer input, which is deferred; the component doc explicitly scopes it **out** (distinct from the element→box geometry read, which *is* covered above).
- **Dependency:** pointer input (`components-input-routing.md`).
- **Unblocks:** if pointer input returns to the roadmap.
- **First-execution risk:** none now — no feature depends on point→element until pointer input exists.

### 7. CSS gaps (grid, partial CSS3) — doc-deferred
- **Reason:** the baseline is litehtml's CSS coverage (CSS 2/3, flexbox; no grid); which gaps to fork in is paid for only as real screens need them (the doc's open item).
- **Dependency:** a litehtml fork + real screens that need the missing CSS.
- **Unblocks:** when an authored screen needs a gap (e.g. grid).
- **First-execution risk:** a screen using an unsupported feature (e.g. `display:grid`) lays out per litehtml's fallback, not the intended layout, until forked in.
