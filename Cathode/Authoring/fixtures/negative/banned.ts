// NEGATIVE fixture (025 S4) — the LOAD-BEARING bite. Each line uses a web API that is ABSENT from lib.cathode.d.ts and
// outside the ES2022 lib (no `dom`), so tsc MUST error (a build error at file+line). The gate asserts this run FAILS;
// a clean-only gate proves nothing. (If any of these ever becomes a SUPPORTED, bound API, move it to the positive fixture.)
fetch("https://example.com/data");                 // TS2304: Cannot find name 'fetch'  (no network egress in a Cathode screen)
const saved = localStorage.getItem("session");      // TS2304: Cannot find name 'localStorage'  (no web storage)
const root = document.getElementById("app");
if (root !== null) {
	(root as HTMLElement).innerHTML = "<b>unsafe</b>"; // TS2339: Property 'innerHTML' does not exist on HTMLElement (no raw-HTML injection)
}
