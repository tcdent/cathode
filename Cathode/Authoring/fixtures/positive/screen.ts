// POSITIVE fixture (025 S4) — a conformant Cathode screen. Uses ONLY the lib.cathode.d.ts surface (no `dom` lib): the
// bound DOM (document/Element/Node), console, and events. Must compile CLEAN and bundle to an IIFE. If this ever fails,
// the gate is over-tight (the supported surface regressed) — the inverse failure mode to the negative fixture.
const root = document.body;
if (root !== null) {
	const greeting = document.createElement("div");
	greeting.setAttribute("class", "greeting");
	greeting.textContent = "Hello, Cathode";
	root.appendChild(greeting);

	const input = document.createElement("input");
	greeting.appendChild(input);

	greeting.addEventListener("click", (event: Event): void => {
		console.log("clicked", event.type, greeting.tagName);
		const rect = greeting.getBoundingClientRect();
		console.log("at", rect.x, rect.y, rect.width, rect.height);
	});
}
