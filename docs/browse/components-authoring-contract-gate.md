# Cathode — Authoring Contract & Gate (detail)

Detail for the **Authoring Contract & Gate** component (see `components.md`). What/why, not how.

This is the build/author-side component that defines the subset screens may use and makes the build
**hard-fail** on anything outside it. It is what keeps authored screens inside what the engine
actually supports — and what makes them debuggable.

## Responsibilities

**Define the three-axis subset.** Authoritatively state what's allowed across DOM, CSS, and the JS
environment. *Why:* authors need a known target, and the engine needs a contract it can enforce.

**Gate the build.** Fail the build on unsupported usage, with file and line — not at runtime.
*Why:* unsupported features should be caught when authoring, not discovered as a broken screen in
the world. *Ground:* the TypeScript compiler is the DOM/JS gate (compile against `lib.cathode.d.ts`
without the standard DOM lib); a small linter checks CSS against a manifest; the bundler target
pins the JS syntax level.

**Generate the contract from the engine, not a parallel list.** The DOM/JS `.d.ts` is **generated
from the engine's own interfaces** — the authored DOM interface plus the `game.*` host bridge — so
it matches the engine by construction; there is no hand-maintained string list or separate
registration to drift, and nothing to "assert manifest == bindings" because the interface *is* the
bindings. The CSS allow-list is **Cathode's own declaration, authored from litehtml's actual coverage** (in
`Authoring/`) — not panels' `css-subset.json` (the panels team's separate guard; Cathode authors its
own conformant equivalent, never couples to another dev's area). *Why:*
the gate must provably match the engine, and the cheapest guarantee is to *be* the engine's
interface rather than a copy asserted to equal it.

**Provide the runtime backstop.** Throw a clear, sourced error on dynamic access the static gate
can't see (`el[name]`, `globalThis[x]`), surfaced to a developer overlay. *Why:* static checking
can't catch dynamic access; the backstop + overlay are what make screens debuggable without
DevTools.

## Boundaries

Build/author-side, distinct from the runtime (except the backstop + overlay). Consumes the surfaces
defined by DOM and Host Bridge (to emit the `.d.ts`) and litehtml's CSS coverage (the manifest).
Not in the runtime hot path.

## Open / deferred (team's call)

- TypeScript-first vs plain-JS authoring (the type gate is free either way; TS gives it directly).
- Developer-overlay scope.

*(Settled 2026-06-14 — no longer open: the `.d.ts` is one output of the **single
generate-from-interface projection** — one extraction over the C++ binding-interface → quickjs
bindings + `.d.ts` + the runtime-verification descriptor. Built in the **Host Bridge /
generate-from-interface sprint**; not an open design question. See `components-host-bridge.md`.)*
