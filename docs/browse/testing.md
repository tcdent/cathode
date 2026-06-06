# Cathode — Testing & Functional Gates

How Cathode's supported subset is verified automatically. The purpose is to give **machine
developers an accurate, structured way to see whether the basic functionality works** — so they
can iterate to a solution on their own, without a human acting as the visual oracle. The human is
only ever the judge of genuine taste (does the text *look* good); never of "is it 10px off" or
"did it draw."

This doc covers **functional gates** — small, self-asserting capability checks. For validating
real, *uninstrumented* apps (the sandbox, the shipping panels) by observing their rendered output,
see `observation.md`.

The enumerated cases come from the supported contracts (the JavaScript contract is being authored
separately; CSS and font cases follow from the supported subset).

## What we are — and are not — testing

- **We trust the libraries.** quickjs-ng, litehtml, HarfBuzz, and Blend2D are assumed correct. A
  bug inside them surfaces visually eventually; it is not what these gates are for.
- **We test bare functionality and presence.** Can a supported JS function be called? Do CSS
  selectors apply? Does a font load and produce a glyph? After the page's script runs, are the
  expected elements actually there? These gates catch **functionality regressions** — something
  that worked and stopped — and **missing expected output**.
- **We do not chase fidelity.** This is not pixel parity and not precise-geometry parity with any
  browser. Whether a glyph is beautiful, or a box is positioned to taste, is a separate human
  call. These gates answer "does the machinery work," not "does it look right."

We test only the **supported subset** of HTML/CSS/JS. A gate failing on something outside the
subset is not a bug — it marks the edge of what we support.

## The common surface: structured JSON over the console

Every test case is a small app/script that runs in **both Chromium and Cathode** and reports its
results as **JSON printed to the console**. A runner captures the console output from each engine,
parses it, and gates on it.

JSON-over-console is the deliberate choice because it is the one surface both engines share for
free — no per-app bindings, no second comparison engine. A test emits something like:

```json
{ "case": "css-selector-applies", "checks": [
  { "id": ".alert color", "ok": true, "value": "rgb(255,0,0)" },
  { "id": ".alert count", "ok": true, "value": 3 }
]}
```

Same script, same JSON shape, two engines, one comparison. That common surface is the whole
integration point.

## Oracle vs. self-contained checks

- **Oracle checks** are ones where Chromium can give a meaningful shared answer — JS behavior, CSS
  application, DOM structure after script runs. Here the gate compares Cathode's JSON to Chromium's
  JSON; Chromium provides ground truth automatically, so the agent never needs to know the right
  answer in advance or ask a human for it.
- **Self-contained checks** are ones where Chromium is not a meaningful oracle because we render
  our own way — e.g. "did glyph A render non-empty." These assert a simple property (a boolean, a
  count, a non-empty result) on their own, no comparison engine needed.

Both kinds report through the same JSON surface.

## Categories of gates

Kept simple — our JS surface is only about a dozen functions, so the corpus stays small.

- **JavaScript capability.** Can each supported function be called and do what it should
  (return/effect)? Can the available built-in primitives be enumerated (is the expected JS
  environment present)? *(Specific cases come from the JavaScript contract.)*
- **CSS application & enumeration.** Do selectors match the right elements and apply the expected
  computed effect? Can the supported selectors/properties be enumerated, so an agent can see what
  it has to work with?
- **DOM presence.** After the page's script runs (including in-flight mutations and events), are
  the expected elements present and structured as expected? This is the core "the page got the
  elements we expect" check.
- **Font & render sanity.** Does a font load? Does a chosen glyph (e.g. the letter "A") render to a
  non-empty result? Presence/sanity only — not a pixel comparison.

These map directly to the questions the gates exist to answer: *can I call a function, enumerate
the primitives, apply a selector, see what selectors I have, load a font, confirm a glyph
renders.*

## Shape of a test case

A case is the smallest app that exercises one capability and prints its findings as JSON. It runs
through the **real** Cathode pipeline — the actual Runtime Core, DOM, layout, raster — never a
parallel reimplementation, so a green gate means production works, not a stand-in. The same source
runs unmodified in Chromium for the oracle checks.

## What a gate reports

Per-check, machine-readable, and specific enough to act on without a human:

- a function that can't be called → named, with the failure
- a selector that didn't apply → which selector, expected vs actual computed value
- a missing element → which one, where it was expected in the tree
- a font that didn't load, or a glyph that came back empty → named

Red/green plus targeted detail, so an agent sees exactly where it's succeeding and failing and can
re-run as many times as it needs, closing on a solution on its own.

## Boundaries

- Gates cover functionality and presence — not visual fidelity (the rare human taste call) and not
  the libraries' internal correctness.
- The corpus is scoped to the supported subset; it doubles as the executable examples of what that
  subset is, alongside the authoring contract.

## Pending

- **JavaScript contract** — the ~dozen supported functions, authored separately; the JS capability
  cases derive from it.
- The CSS subset cases and the font/glyph cases follow from the supported CSS coverage and the
  shipped fonts.
