// screens/banned/ — a NON-conformant Cathode screen (the negative real-screen path). Its .ts calls `fetch` (a banned web
// API, absent from lib.cathode.d.ts) and its screen.css uses an unsupported property — so the gate MUST hard-fail this
// screen END-TO-END (the production author-build rejects it), proving the gate bites through the real author location,
// not only the in-isolation fixtures. (If you're authoring a real screen and see this fail, the gate is doing its job.)
const root = document.getElementById("app");
if (root !== null) {
	fetch("https://example.com/telemetry"); // BANNED — TS2304: Cannot find name 'fetch' (no network egress in a screen)
	root.textContent = "loaded";
}
