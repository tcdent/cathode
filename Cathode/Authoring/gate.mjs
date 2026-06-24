// Cathode author-side build GATE (025 S4). Runs on the dev machine / CI ONLY — never the UE/make build (the Windows
// build box has no Node; these files sit on its tree unused). Three steps:
//   1. tsc the POSITIVE fixture against lib.cathode.d.ts (no `dom` lib)  -> must PASS  (the supported surface compiles).
//   2. tsc the NEGATIVE fixture (fetch/localStorage/innerHTML)           -> must FAIL  (the load-bearing banned-API bite).
//   3. esbuild the POSITIVE -> an IIFE bundle (what Cathode loads).
// Exit 0 = gate PASS; non-zero = gate FAIL. (S5 adds the css-tree CSS linter; S6 wires this into an author-build target.)
import { execFileSync } from "node:child_process";
import { build } from "esbuild";
import { mkdirSync } from "node:fs";
import { platform } from "node:os";

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

// 3. esbuild the POSITIVE -> IIFE (only if the gate held).
if (!failed) {
	mkdirSync("dist", { recursive: true });
	await build({
		entryPoints: ["fixtures/positive/screen.ts"],
		bundle: true,
		format: "iife",
		target: "es2022",
		outfile: "dist/screen.iife.js",
		logLevel: "silent",
	});
	console.log("[gate] PASS: positive fixture bundled to an IIFE (dist/screen.iife.js)");
}

console.log(failed ? "[gate] RESULT: FAIL" : "[gate] RESULT: PASS");
process.exit(failed ? 1 : 0);
