// screens/hello/ — a conformant Cathode SCREEN kept as a simple body-mount EXAMPLE (a greeting + a click listener). It
// mounts into document.body — the mount contract (apps mount into document.body; NO #app convention; the
// device seeds documentElement>[head,body] only, so there is no #app root and such a mount would no-op). The gate's REAL
// author-build screen is screens/status-card/; this hello is the reference body-mount example (gate-validated by
// tsc + css-tree, not bundled). It compiles against lib.cathode.d.ts (no `dom` lib) — the supported authoring surface.
const root = document.body;
if (root !== null) {
	const card = document.createElement("div");
	card.setAttribute("class", "greeting");
	card.textContent = "Hello, Cathode";
	root.appendChild(card);

	card.addEventListener("click", (event: Event): void => {
		console.log("clicked", event.type, card.tagName);
	});
}
