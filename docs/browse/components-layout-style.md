# Cathode — Layout & Style (detail)

Detail for the **Layout & Style** component (see `components.md`). What/why, not how. litehtml is
grounding.

This component turns the DOM tree plus CSS into a laid-out set of positioned boxes and the paint
calls that draw them. It defines the CSS coverage authors actually get.

## Responsibilities

**Lay out the document.** Feed the DOM tree into litehtml and produce positioned geometry. *Why:*
something has to turn DOM + CSS into boxes with coordinates.

**Parse and cascade CSS.** Via litehtml (CSS 2/3, flexbox; no grid; partial CSS3). *Why:* styling
is half of "standard web authoring," and this is where the documented CSS constraints become
concrete.

**Drive relayout on dirty.** When the DOM marks itself changed, relayout the affected document.
*Why:* litehtml is parse-then-render, not a live-mutation engine, so this component owns the
adaptation — keeping Cathode's retained tree as the source of truth and re-laying-out efficiently.

**Route paint.** Translate litehtml's draw operations through its `document_container` callbacks to
the rasterizer and text components. *Why:* this is the seam connecting layout to pixels.

**Surface computed geometry and style (for observation).** Expose each element's computed box
geometry and resolved computed style — the data the observation layer reads back via
`getBoundingClientRect` / `getComputedStyle` (see `observation.md`). *Why:* litehtml already
computes both to lay out and paint, so **geometry is a near-free read** of data that already
exists; **resolved style is a bounded mapping** of litehtml's cascaded values, scoped to the
contract's CSS properties (not the full CSSOM). These back the observer, not the author contract —
no special debug-gating, just a read of what layout already holds.

## Key concern — the mutation/relayout adaptation

litehtml's model is "parse a document, render it," not "mutate a live tree." Bridging our retained,
JS-mutated DOM to litehtml efficiently is the component's hard part and a known de-risk item —
likely a small litehtml fork. *Ground:* litehtml#69.

## Boundaries

Owns the litehtml integration, relayout, and paint routing. Delegates text measurement and shaping
to Text & Fonts, and actual rasterization to the Rasterizer. Consumes the DOM tree.

(Element-at-point **hit-testing** is litehtml-provided but **not in scope** — it existed only to
serve pointer input, which is deferred; see `components-input-routing.md`. It returns if pointing
lands on the roadmap. Note this is distinct from the element→box **geometry read** above, which
*is* in scope for observation: hit-testing is point→element, the geometry read is element→box.)

## Open / deferred (team's call)

- The mutation strategy (litehtml fork vs re-feed) — gated by the de-risk spike.
- Which CSS gaps (e.g., grid) to fork in, paid for only as real screens need them.
