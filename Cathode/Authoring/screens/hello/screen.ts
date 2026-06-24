// screens/hello/ — a conformant Cathode SCREEN (the REAL author-build location; PM ruling (a): a screen `.ts` + its own
// standalone `screen.css`). This is the production author-build INPUT (distinct from the in-isolation gate fixtures): it
// compiles against lib.cathode.d.ts (no `dom` lib), its CSS lints against css-allow-list.toml, and it bundles to the IIFE
// Cathode loads. `npm run gate` processes this screen end-to-end (tsc -> css-tree -> esbuild).
const root = document.getElementById("app");
if (root !== null) {
	const card = document.createElement("div");
	card.setAttribute("class", "greeting");
	card.textContent = "Hello, Cathode";
	root.appendChild(card);

	card.addEventListener("click", (event: Event): void => {
		console.log("clicked", event.type, card.tagName);
	});
}
