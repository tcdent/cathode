# Sprint 027 — Live/Dynamic Essentials (close 026's deferred essentials)

> **GREEN-LIT (full). Slice 3 (rebind-on-realloc) committed `ee1ff32`. RQ1 ruled (tcdent): (i) the JS-DOM-build,
> mount into `document.body` (the mount contract locked). RQ2/RQ3 OUT (PM). Slices 1→2 building on RQ1 (i).**

## Scope Boundary

This sprint closes 026's deferred essentials — what makes Cathode a **live, dynamic** system (tcdent: "not
nice-to-haves"). Three essentials: **(1)** real DOM content (a real mutable `FDomDocument` as the device's source,
replacing 026's inline stub); **(2)** a live content-change (the mutate→re-render→re-upload→re-display two-frame
v1→v2, visible on the device); **(3)** the rebind-on-realloc guard exercised under a real texture realloc.

**Owns:** the device displaying a **real** `FDomDocument` (not the 026 inline `FCathodeDeviceDocument` stub); the
end-to-end live update (a DOM mutation visible on the device's screen); the rebind exercised on a real egress
texture realloc.

**Does NOT own (named, with the owner):**
- **An arbitrary HTML-string parser / ingestion** — RQ1: the design-of-record DOM surface is a *chosen subset*
  behind a firewall (no `innerHTML`/`DOMParser`; content enters as JS, not markup — `components-dom.md:15-22`,
  `components-dom-surface.md`). A real parser would *widen* that firewall; out unless tcdent rules RQ1 (ii).
- **Per-device dims / the egress resize-realloc** — (3)'s rebind is proven by `Release+re-drive` (a re-register, no
  dims change); a dims change pulls the unbuilt egress *resize-realloc producer* (a 026 residual). RQ2: candidate.
- **The A2-staged production font** — orthogonal to (1)/(2)/(3); the committed OFL fixture suffices for any text in
  the proof. RQ3: candidate (a production-polish residual, recommend out).

## Problem

026 displayed a **static** surface from an **inline** `FCathodeDeviceDocument` stub (a hardcoded `GetDocumentHtml`
string) — NOT a real mutable `FDomDocument`, so nothing can be mutated and no real DOM content is authored. The DOM
*mutation* (018), the litehtml re-render (020), the dirty-rect upload (024), and the per-frame DriveAll + display
(026) are each proven — but the **live chain** (mutate → re-render → re-upload → re-display, visible) was never
joined end-to-end, and the rebind-on-realloc guard (`CathodeDeviceActor.cpp:60`) is source-confirmed but never
exercised (no realloc under the static floor).

## Goal

> Cathode is a live, dynamic system: the device displays a real DOM, a content change is visible, and the
> texture-rebind survives a realloc.

## Final State

- **A real `FDomDocument` as the device's source (RQ1 ruled).** The JS stack (`ICathodeJsRuntime`/
  `INativeClassRegistry`/`ILayoutEngine`) stands up; a JS script (Preact-style) issues `createElement`/`appendChild`
  → the real retained tree → the device's `ILayoutDocumentSource` (replacing the inline stub). The `FDomDocument`
  serializes to HTML for litehtml (the existing adapter — `RelayoutDocument` → `Render(GetDocumentHtml())` →
  `createFromString`). No firewall violation; content enters as JS.
- **The live two-frame.** A DOM mutation (018 `SetAttribute`/`AppendChild`) on the live `FDomDocument` →
  `MarkNodeDirty` → Relayout re-feeds the new serialized tree → the 024 dirty-rect upload → the device shows v2,
  NOT v1 (the 026 scene-capture).
- **The rebind exercised.** `egress.Release(Surface)` + re-drive → a NEW `UTexture2D` ptr → the device's
  `Texture != BoundTexture` guard rebinds → the device shows the post-realloc content (not the stale texture).

## In Scope (ordered slices)

- **Slice 1 — real DOM content (gated on RQ1).** Stand up the JS stack + a JS script builds a real `FDomDocument`
  via the JS DOM API → wire it as the device's `ILayoutDocumentSource` (replace the inline stub). The JS-stack
  ownership/lifetime is named (subsystem-owned, one runtime for the proof; multi-device N-runtimes is the scale
  residual, out). *Proof — real-DOM-NOT-the-inline-stub (the display alone is insufficient — the 026 inline stub
  ALSO shows a known color box):* (a) the JS-authored content is **observably DISTINCT from 026's inline red box**
  (a JS-built BLUE div) → the scene-capture tracks BLUE (a still-wired inline stub renders RED, the wrong content
  → RED — a same-color build can't distinguish the two sources); AND (b) the displayed color is **read from the JS-built retained tree**
  (`GetAttribute`/the node's style reflects the JS `createElement`/`setAttribute` — the 026 read-from-layout
  pattern; the inline stub's `GetChildNodes` is empty → a stub FAILS the read-from-the-real-tree); AND (c) the
  device's `ILayoutDocumentSource` **IS-A `FDomDocument`**, not `FCathodeDeviceDocument`. Reuses the proven JS-DOM
  build (`Cathode.Dom.JsDriven.*`) + `RuntimeCore.Eval.RealEvaluate`; the NEW edge is the real-`FDomDocument`-as-
  device-content (not new primitives).
- **Slice 2 — the live content-change (depends on slice 1).** Mutate the live `FDomDocument` (018 API) +
  re-DriveAll. *Proof — the two-frame, two-sided (capture BOTH frames):* v1 (the pre-mutation scene-capture, a
  known color e.g. RED) → a **visible** mutation (018 `SetAttribute`/`AppendChild` that changes the rendered
  bitmap — a different bg color e.g. BLUE / a new visible element, NOT a non-rendered/getComputedStyle-only
  attribute) → re-DriveAll → v2 (the post-mutation scene-capture). Assert **v2 == the mutated content (BLUE) AND
  v2 != v1 (RED gone)** — the two-sided bites a no-re-render (still v1 → RED), a stale overlay / no-clear (both
  shown), and a smear (the 023 two-color discipline). The 024 dirty-rect is the cost; the visible v1→v2 is the
  proof. NOT a getComputedStyle re-proof of the 018 mutation — the *displayed* bitmap must change.
- **Slice 3 — the rebind-on-realloc (independent; may run parallel/first).** `egress.Release(Surface)` + re-drive
  → a new `UTexture2D` ptr → the device rebinds. *Proof — PTR-IDENTITY is the primary discriminator (the display
  alone can't catch a stale texture if the content is unchanged — the freed old texture and a fresh new one look
  identical, and a no-rebind samples the GC-freed old texture → garbage/crash, not a clean "shows v1"):* capture
  the OLD `BoundTexture` ptr → `Release(Surface)` (drops + GCs the old) + re-drive (`CreateTransient` a fresh
  `UTexture2D`) → assert **NEW != OLD (the realloc genuinely changed the ptr — not a vacuous no-realloc; assert,
  don't assume)** AND the device **REBOUND (`BoundTexture == GetTexture() == NEW`, the `:60` guard fired)**; the
  scene-capture (valid post-realloc content, no crash/garbage) corroborates. The guard exercised end-to-end (vs
  026's source-confirmed-only).

## Non-Goals

- An HTML-string parser / ingestion (RQ1 — the firewall; unless tcdent rules (ii) → a separate parser slice).
- Per-device dims + the egress resize-realloc producer (RQ2 — `Release+re-drive` proves (3) without dims).
- The A2-staged production font (RQ3 — the OFL fixture suffices).
- Re-proving the 018 mutation, the 020 re-render, the 024 dirty-rect, or the 026 display in isolation.
- **Do not delete a fallback just because production does not hit it — trace callers and tests first.**

## Starting Surface (audit before editing)

- The JS-DOM build (PROVEN): `createElement`/`appendChild`/`setAttribute`/`textContent` (`CathodeDom.h:127-154`),
  C++- and JS-callable (thunks `CathodeDomDocument.cpp:489`, `CathodeDomNode.cpp:1700`); node-by-node build
  (`Cathode.Dom.JsDriven.CreateAppendRetainedTree`); `CreateDomDocument` needs the full JS stack
  (`CathodeDomDocument.cpp:340`).
- The DOM→litehtml adapter (PROVEN): `FDomDocument` IS-A `ILayoutDocumentSource` (`CathodeDom.h:253`); each relayout
  re-feeds `Render(GetDocumentHtml())` → `createFromString` (`CathodeLayoutEngine.cpp:392,500`). Serialization is
  the adapter contract, NOT a bypass.
- The mutate→dirty→relayout chain (PROVEN): mutation → `MarkNodeDirty` (`CathodeLayoutEngine.cpp:575`) → relayout
  re-feeds the new tree; the 018 mutation tests; the 024 dirty-rect; the 026 DriveAll + display.
- The realloc path (PROVEN exists): the egress caches per-surface (`CreateTransient` if `!IsValid()`,
  `CathodeEgress.cpp:155`); `Release(Surface)` drops it (`:139`); the next drive re-creates a new ptr; the device's
  `Texture != BoundTexture` rebind (`CathodeDeviceActor.cpp:60`). (The dims-change path is validity-only-not-size —
  a separate egress resize-realloc residual, out per RQ2.)

## Method

- **Reuse the proven pieces; prove the new JOIN.** (1) reuses the JS-DOM build + the adapter — the NEW is the
  real-`FDomDocument`-as-device-content. (2) reuses the 018 mutation + the 024 dirty-rect + the 026 display — the
  NEW is the visible v1→v2. (3) reuses the device + the egress — the NEW is the rebind exercised on a real realloc.
  Reject any slice that re-proves a piece in isolation (a getComputedStyle after a mutation is 018, not (2)).
- **The display proof is the 026 hard-gate scene-capture** (created-world `SceneCapture`, R-dominant tolerant
  ratio, non-circular, test-only shader-force) — the *displayed* content, not a texture re-read.
- **Freshness / dep-confinement carry from 026** (the engines confined to UptimeCathodeDevice; Uptime Cathode-free;
  relink + additive marker; the box session for the GPU readback/capture).

## Acceptance

- **(1) real DOM content (real-not-inline bound):** the device displays a JS-built content **observably distinct
  from 026's inline red box** (a BLUE div → the capture tracks BLUE; a still-wired stub renders RED → RED) AND the
  color is **read from the JS-built retained tree** (the empty-child inline stub fails it) AND the source **IS-A
  `FDomDocument`** (not the stub) — the display alone (a same-color render) can't distinguish the two sources.
- **(2) the live two-frame (two-sided):** capture BOTH frames — v1 (RED) → a **visible** mutation → re-DriveAll →
  v2; assert **v2 == the mutated content (BLUE) AND v2 != v1 (RED gone)** (bites a no-re-render, a stale overlay,
  a smear). The dirty-rect the cost, the visible change the proof; NOT a getComputedStyle re-proof of 018.
- **(3) the rebind exercised (ptr-identity primary):** capture the OLD ptr → `Release+re-drive` → assert **NEW !=
  OLD (genuine realloc) AND `BoundTexture == NEW` (rebound)**; the scene-capture (valid post-realloc, no crash)
  corroborates. The display alone can't catch a stale texture on unchanged content.
- **Dep-confinement intact** (Uptime Cathode-free); **no regression** (the full Cathode suite + the 023/024/026
  baselines exact).

## Review Questions

1. **RQ1 — the HTML→DOM scope/firewall fork — RESOLVED (tcdent, 2026-06-22): (i) the JS-DOM-build.** The real
   `FDomDocument` is built via JS **mounting into `document.body`** (NOT an HTML-string parser — the firewall held;
   content enters as JS, not markup). **The mount contract (locked, `surface-load-document-wiring.md`):** Cathode
   seeds `documentElement` + `head` + `body` only (NO root div); apps mount into `document.body`; NO `#app`
   convention; NO config. Slices 1/2 build on this.
2. **RQ2 — per-device dims — RESOLVED (PM): OUT.** `Release+re-drive` proves (3)'s rebind without it; a dims
   change pulls the unbuilt egress *resize-realloc producer* (a 026 residual). Per-device *content* stays IN
   (natural with (1)/(2)).
3. **RQ3 — the A2-staged production font — RESOLVED (PM): OUT.** Orthogonal to (1)/(2)/(3); the OFL fixture
   suffices for any proof text. A production-polish residual, deferred.
