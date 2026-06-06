// lib.cathode.d.ts — the Cathode authoring type surface
//
// 001 SKELETON: section shape only. The full member surface is authored in the
// authoring-contract sprint (003) and is GENERATED from capability-manifest.toml —
// never hand-maintained in parallel with it.
//
// Author apps (TypeScript, or plain JS type-checked against these same definitions)
// compile against THIS file and explicitly NOT the standard DOM lib — the project's
// tsconfig omits "dom" from "lib". Any unsupported web API (fetch, localStorage,
// Intl, ...) is therefore a type error with a file and line, and authors get
// autocomplete + docs for the supported surface for free. This is the build-time
// gate for the DOM and JS API axes of the authoring contract.

export {}; // module scope; the real globals are declared below at (003)

// --- Scheduler -------------------------------------------------------------
// Promise is core quickjs-ng and is not declared here. Cathode provides the timers
// and the frame callback, ticked against the worker clock.
//
// declare function setTimeout(handler: () => void, timeout?: number): number;      // (003)
// declare function clearTimeout(id: number): void;                                 // (003)
// declare function requestAnimationFrame(cb: (t: number) => void): number;         // (003)

// --- Console ---------------------------------------------------------------
// declare const console: { log(...args: unknown[]): void; /* ... */ };             // (003)

// --- DOM surface -----------------------------------------------------------
// The bounded DOM: element create/insert/remove, attribute & property access, a
// minimal Event (bubbling + hit-test), tree reads, and a document/mount root.
//
// interface CathodeEvent { /* type, target, bubbling, preventDefault, ... */ }     // (003)
// interface CathodeElement { /* children, attributes, addEventListener, ... */ }   // (003)
// declare const document: { /* createElement, mount root, ... */ };                // (003)

// --- Host API (game.*) -----------------------------------------------------
// INTENTIONALLY UNSPECIFIED (architecture doc). The `game` global's shape is
// generated from Unreal reflection once the simulation's shape is known — it is not
// authored even in 003 until then.
//
// declare const game: { /* curated, reflection-generated host surface */ };        // (deferred)
