// screens/status-card/ — the 029 #30 end-to-end authoring-pipeline DEMO app. The authored JS builds a single styled box
// and mounts it to document.body — the LOCKED 027 contract (apps mount into document.body; NO #app). Rendered ONCE (the
// 028 worker idles after compose — no rAF/timer self-wake). The gate bundles this .ts to an IIFE; the worker evals it ->
// THIS authored JS builds the DOM -> the box renders on the device (the pipeline 025-gate -> IIFE -> 028-device).
//
// SHAPE — a SINGLE absolutely-positioned GREEN box (the PROVEN s4-author-bundle containment shape: a single sized,
// childless, TEXT-FREE absolute div renders CONTAINED in the device's litehtml). GREEN (#00ff00 — the egress/scene-capture's
// detected channel) at a DISTINCT layout box (left:32,top:28,64x40) so the proof binds THIS app's content at its OWN
// computed box with a blank margin around it.
//
// WHY NO LABEL: text content inside an absolute div spills the bg to FULL-SURFACE on the device's litehtml — a SEPARATE
// litehtml-layer finding (tracked by the PM), out of #30's de-risk scope. The de-risk's claim is the gate's authored
// output rendering AT its position, which the bare box meets (96.6% in-box green on the box). THE PROOF is the SWAP: this
// GATE-PRODUCED IIFE vs the hand-written s4 bundle, wired via GAuthorBundlePath -> status-card.iife.js (the bare box being
// s4's SHAPE is fine — the swap + the in-box render at the distinct position is the proof, the wiring is the source).
const card = document.createElement("div");
card.setAttribute("class", "status-card");
card.setAttribute("style", "position:absolute;left:32px;top:28px;width:64px;height:40px;background-color:#00ff00;");

// document.body is Element | null (the .d.ts surface) — null-check it (the correct body-mount pattern; on the device the
// FDomDocument seeds documentElement>[head,body], so body is non-null at runtime).
const body = document.body;
if (body !== null) {
	body.appendChild(card);
}
