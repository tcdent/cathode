// Cathode author-side build GATE (025 S4-S6). Runs on the dev machine / CI ONLY — never the UE/make build (the Windows
// build box has no Node; these files sit on its tree unused). The in-isolation fixture bites (S4/S5) THEN the REAL screen
// end-to-end (S6). One gate; any failure hard-fails the whole gate:
//   1. tsc the POSITIVE fixture against lib.cathode.d.ts (no `dom` lib)  -> PASS  (the supported DOM/JS surface, S4).
//   2. tsc the NEGATIVE fixture (fetch/localStorage/innerHTML)           -> FAIL  (the DOM/JS banned-API bite, S4).
//   3. css-tree lint the POSITIVE/NEGATIVE .css vs css-allow-list.toml   -> positive PASS, negative FAIL (the CSS bite, S5).
//   4. the REAL screen screens/hello/ (a .ts + its standalone .css, PM ruling (a)) -> tsc + css-tree + esbuild PASS
//      END-TO-END -> the IIFE Cathode loads (the production author-build flow, S6).
//   5. the BANNED real screen screens/banned/ -> HARD-FAILS on the real author-build path (the bite end-to-end, S6).
// Exit 0 = gate PASS; non-zero = gate FAIL. `npm run gate` IS the canonical author-build step (see the Cathode README's
// Authoring gate). The tsc and css-tree checks are INDEPENDENT mechanisms — each its own positive+negative pair.
import { execFileSync } from "node:child_process";
import { build } from "esbuild";
import { mkdirSync } from "node:fs";
import { platform } from "node:os";
import { parseAllowList, lintCssFile } from "./css-lint.mjs";

const TSC = "./node_modules/.bin/tsc" + (platform() === "win32" ? ".cmd" : "");

function tsc(project) {
	try {
		execFileSync(TSC, ["-p", project], { stdio: "pipe", encoding: "utf8" });
		return { ok: true, out: "" };
	} catch (e) {
		return { ok: false, out: (e.stdout || "") + (e.stderr || "") };
	}
}

let failed = false;

// 1. POSITIVE must compile clean.
const pos = tsc("tsconfig.json");
if (pos.ok) {
	console.log("[gate] PASS: positive fixture compiles clean (only the lib.cathode.d.ts surface)");
} else {
	failed = true;
	console.error("[gate] FAIL: the positive (conformant) fixture did NOT compile (the supported surface regressed):\n" + pos.out);
}

// 2. NEGATIVE must FAIL — and specifically on the banned-API bite at file+line.
const neg = tsc("tsconfig.negative.json");
if (!neg.ok) {
	const biteLines = neg.out.split("\n").filter((l) => /error TS(2304|2339)/.test(l));
	const banned = /Cannot find name '(fetch|localStorage)'/.test(neg.out) || /Property 'innerHTML'/.test(neg.out);
	if (banned && biteLines.length > 0) {
		console.log("[gate] PASS: negative fixture HARD-FAILS tsc on banned usage (the load-bearing bite, file+line):");
		for (const l of biteLines) { console.log("        " + l.trim()); }
	} else {
		failed = true;
		console.error("[gate] FAIL: negative fixture failed tsc, but NOT on the expected banned-API bite:\n" + neg.out);
	}
} else {
	failed = true;
	console.error("[gate] FAIL: the negative fixture COMPILED CLEAN — the gate did NOT bite (banned APIs were accepted)!");
}

// 3. CSS LINT (S5): css-tree membership vs css-allow-list.toml (the S2 artifact, parsed at lint time — the bind). A
// property NOT in the allow-list is a violation at property:line (membership-by-absence, no hand denylist). Independent
// of the tsc checks (its own positive+negative pair).
const allow = parseAllowList("css-allow-list.toml");
const posCss = lintCssFile("fixtures/positive/screen.css", allow);
if (posCss.length === 0) {
	console.log(`[gate] PASS: positive CSS fixture lints clean (${allow.size} allow-list props parsed; incl. opacity + letter-spacing — the +2 cathode_computed PASS)`);
} else {
	failed = true;
	console.error("[gate] FAIL: the positive CSS fixture has UNSUPPORTED properties (the allow-list bind or the parse regressed):");
	for (const v of posCss) { console.error(`        ${v.property} (fixtures/positive/screen.css:${v.line})`); }
}
const negCss = lintCssFile("fixtures/negative/banned.css", allow);
if (negCss.length > 0) {
	console.log("[gate] PASS: negative CSS fixture HARD-FAILS the linter on unsupported properties (the load-bearing CSS bite, property:line):");
	for (const v of negCss) { console.log(`        ${v.property} (fixtures/negative/banned.css:${v.line})`); }
} else {
	failed = true;
	console.error("[gate] FAIL: the negative CSS fixture linted CLEAN — the CSS gate did NOT bite (unsupported props accepted)!");
}

// 4. THE REAL SCREEN end-to-end (S6, the production author-build flow): the conformant screen at screens/hello/ (a .ts +
// its standalone screen.css, PM ruling (a)) — its .ts typechecks (no `dom` lib), its .css lints clean, and it bundles to
// the IIFE Cathode loads. This processes the REAL author location end-to-end, not only the in-isolation fixtures above.
const screenTs = tsc("tsconfig.screen.json");
const screenCss = lintCssFile("screens/hello/screen.css", allow);
if (screenTs.ok && screenCss.length === 0) {
	mkdirSync("dist", { recursive: true });
	await build({
		entryPoints: ["screens/hello/screen.ts"],
		bundle: true,
		format: "iife",
		target: "es2022",
		outfile: "dist/hello.iife.js",
		logLevel: "silent",
	});
	console.log("[gate] PASS: REAL screen screens/hello/ passes END-TO-END (tsc + css-tree + esbuild -> dist/hello.iife.js)");
} else {
	failed = true;
	if (!screenTs.ok) { console.error("[gate] FAIL: the real screen's .ts did NOT typecheck (the supported surface regressed):\n" + screenTs.out); }
	if (screenCss.length > 0) {
		console.error("[gate] FAIL: the real screen's .css has UNSUPPORTED properties:");
		for (const v of screenCss) { console.error(`        ${v.property} (screens/hello/screen.css:${v.line})`); }
	}
}

// 5. THE BANNED real screen (screens/banned/) MUST hard-fail on the real author-build path — a banned web API in its .ts
// (tsc) AND an unsupported property in its .css (css-tree). The gate biting through the REAL screen path, not just fixtures.
const bannedTs = tsc("tsconfig.screen-banned.json");
const bannedCss = lintCssFile("screens/banned/screen.css", allow);
if (!bannedTs.ok && bannedCss.length > 0) {
	console.log("[gate] PASS: BANNED real screen screens/banned/ HARD-FAILS end-to-end (the real-screen-path bite):");
	for (const l of bannedTs.out.split("\n").filter((l) => /error TS/.test(l))) { console.log("        .ts  " + l.trim()); }
	for (const v of bannedCss) { console.log(`        .css ${v.property} (screens/banned/screen.css:${v.line})`); }
} else {
	failed = true;
	console.error(`[gate] FAIL: the banned real screen did NOT hard-fail on both axes (ts-ok=${bannedTs.ok}, css-violations=${bannedCss.length}) — the gate did not bite the real-screen path!`);
}

console.log(failed ? "[gate] RESULT: FAIL" : "[gate] RESULT: PASS");
process.exit(failed ? 1 : 0);
