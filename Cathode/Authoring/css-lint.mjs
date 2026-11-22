// The Cathode author-side CSS linter. css-tree parses a stylesheet, walks its Declarations, and flags any
// property NOT in css-allow-list.toml (the generated allow-list). This is a MEMBERSHIP test, NOT a hand denylist —
// a property fails by ABSENCE from the generated list (grid/gap/animation aren't litehtml parse-names, so they're not
// in the .toml). No drift: a litehtml add/remove flows through the .toml, so the gate tracks the engine with no
// hand maintenance. Reports each violation as { property, line } (file:line), mirroring the tsc bite shape.
import * as csstree from "css-tree";
import { readFileSync } from "node:fs";

// Hand-parse the GENERATED css-allow-list.toml — a flat `[properties] allow = [ "x", ... ]` block of quoted strings.
// No toml dependency: the file's shape is fixed + generated (if it ever grows tables, revisit a real toml parser). A
// broken parse is self-evident — the positive fixture's supported props would then read as "not in allow" and fail.
export function parseAllowList(tomlPath) {
	const text = readFileSync(tomlPath, "utf8");
	const allow = new Set();
	let inAllow = false;
	for (const raw of text.split("\n")) {
		const line = raw.trim();
		if (line.startsWith("allow")) { inAllow = true; continue; } // the `allow = [` line
		if (inAllow && line.startsWith("]")) { break; }             // end of the array
		if (inAllow) {
			const m = line.match(/^"([^"]+)"\s*,?$/);
			if (m) { allow.add(m[1].toLowerCase()); }
		}
	}
	return allow;
}

// Lint a .css file against the allow Set. Returns [{ property, line }] for every UNSUPPORTED property declaration.
// Custom properties (--x) are author theming (not litehtml props) and are accepted — consistent with the runtime
// element.style gate (IsAcceptedStyleProperty accepts `--`).
export function lintCssFile(cssPath, allow) {
	const src = readFileSync(cssPath, "utf8");
	const ast = csstree.parse(src, { positions: true });
	const violations = [];
	csstree.walk(ast, (node) => {
		if (node.type === "Declaration") {
			// CSS property names are CASE-INSENSITIVE per the CSS standard, so we lowercase before the membership test
			// (the allow Set is lowercased too, in parseAllowList). This is correct + DISTINCT-BY-DOMAIN from the
			// JS-path IsDeclaredCssProperty, which is CaseSensitive (JS identifiers are case-sensitive) — not a divergence.
			const prop = node.property.toLowerCase();
			if (prop.startsWith("--")) { return; }      // custom property — accepted
			if (!allow.has(prop)) {
				violations.push({ property: node.property, line: node.loc?.start?.line ?? 0 });
			}
		}
	});
	return violations;
}
