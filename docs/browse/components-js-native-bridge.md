# Cathode — JS↔Native Bridge (detail)

Detail for the **JS↔Native Bridge** component (see `components.md`). What/why, not how. quickjs-ng
APIs are grounding.

This is the machinery that lets C++ objects and functions appear to JavaScript as real JS values,
and that keeps both heaps' lifetimes correct. Every native thing author code touches — DOM nodes,
the `game.*` API — crosses this boundary, so it is also where the system's single biggest
correctness hazard lives.

## Responsibilities

**Expose C++ to JS.** Present C++ objects as JS objects and C++ functions as callable JS functions.
*Why:* the DOM and the host API have to look and behave like ordinary JS to author code.

**Own cross-heap lifetime.** Tie a C++ object's lifetime to its JS wrapper (and keep native
objects alive while JS references them). *Why:* the dangerous shape is a node that holds a listener
closure that captures the node — a reference cycle spanning both heaps that quickjs-ng's collector
cannot see through. Mishandled, it leaks or, worse, frees a C++ object while JS still points at it.
This is the #1 area to get right.

**Provide the binding mechanism.** One reusable way to register native classes, attach opaque
pointers, and run finalizers — rather than ad-hoc binding per component. *Why:* consistency and a
single place to enforce lifetime discipline. *Ground:* `JS_NewClassID` / `JS_NewClass` /
`JS_SetOpaque` + finalizers; header-only RAII wrappers (quickjscpp/quickjspp-style) over the
refcount; `WeakRef`/`FinalizationRegistry` for native-side cleanup when wrappers die.

**Marshal values.** Convert across the boundary within a small, fixed vocabulary (numbers,
strings, bools, arrays, plain records) and reject anything exotic. *Why:* keeping marshaling
bounded keeps it predictable and cheap.

**RAII over JS refcounts.** Guard `JS_FreeValue` discipline so held values don't leak. *Why:*
manual refcounting is the classic embedding leak; RAII makes it safe.

## Key concern — the teardown policy

The bridge defines *when* native objects are released: explicit teardown on node and listener
removal, plus finalization-driven cleanup as a backstop. This policy is the heart of the component
and the thing to design carefully; the DOM provides the teardown trigger points.

## Boundaries

Owns the binding/lifetime mechanism and value marshaling. Does **not** define the DOM surface or
the host API — those are consumers. Used by DOM, Host Bridge, and Runtime Core (for any JS values
they hold).

## Open / deferred (team's call)

- Adopt an existing C++ wrapper (quickjscpp/quickjspp) vs a thin hand-rolled layer over the native
  API, given our small, specific surface.
- The exact marshaling vocabulary.
