// screens/status-card/ — the end-to-end authoring-pipeline DEMO app. The authored JS builds a single styled box
// and mounts it to document.body — the mount contract (apps mount into document.body; NO #app). Rendered ONCE (the
// worker idles after compose — no rAF/timer self-wake). The gate bundles this .ts to an IIFE; the worker evals it ->
// THIS authored JS builds the DOM -> the box renders on the device (the pipeline gate -> IIFE -> device).
//
// SHAPE — a SINGLE absolutely-positioned GREEN box (a single sized,
// childless, TEXT-FREE absolute div renders CONTAINED in the device's litehtml). GREEN (#00ff00 — the egress/scene-capture's
// detected channel) at a DISTINCT layout box (left:32,top:28,64x40) so a render proof binds THIS app's content at its OWN
// computed box with a blank margin around it.
//
// WHY NO LABEL: text content inside an absolute div spills the bg to FULL-SURFACE on the device's litehtml — a SEPARATE
// litehtml-layer limitation. What this screen demonstrates is the gate's authored
// output rendering AT its position, which the bare box meets: the gate-produced IIFE
// (dist/status-card.iife.js) is what the device loads, and the in-box render at the distinct position shows
// the authored bundle is the source of the pixels.
const card = document.createElement("div");
card.setAttribute("class", "status-card");
card.setAttribute("style", "position:absolute;left:32px;top:28px;width:64px;height:40px;background-color:#00ff00;");

// document.body is Element | null (the .d.ts surface) — null-check it (the correct body-mount pattern; on the device the
// FDomDocument seeds documentElement>[head,body], so body is non-null at runtime).
const body = document.body;
if (body !== null) {
	body.appendChild(card);
}
