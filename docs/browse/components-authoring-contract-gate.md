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

**Hold one source of truth.** A capability manifest that generates the type definitions and the CSS
allow-list, asserted at engine build against the actually-registered bindings and litehtml config.
*Why:* the gate must provably match the engine; a contract that drifts from reality is worse than
none.

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
- The capability-manifest format and the generation pipeline.
- Developer-overlay scope.
