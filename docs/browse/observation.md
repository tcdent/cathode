# Cathode — Observation (rendering parity by snapshot)

A companion to `testing.md`. Where the functional gates are small, self-asserting capability
checks, **observation** validates real, *uninstrumented* apps — the `panels/sandbox` baseline and
the shipping panels — by watching their rendered output and comparing Cathode to Chromium. It
answers **"how close did we get,"** per element, automatically, with no human as the visual oracle.

## The principle: observe, don't instrument

Apps are pure inputs. No assertion code, no test IDs, no harness imports in the app — it stays a
clean, normally-authored app. The same observer validates any panel, so the sandbox is a swappable,
nice-to-have corpus, not a coupling point. The coupling lives in the **snapshot schema** (= the
supported surface you already maintain), which is the right place for it.

## One JS extractor, run in both engines

The observer is **a single JavaScript module, run identically in Chromium and Cathode.** Same code
on both sides, so the snapshot schema cannot drift — one implementation, not two emitters to keep
in lockstep. It walks the rendered DOM and `console.log(JSON.stringify(snapshot))`; the driver
captures it in Chromium, Runtime Core's console routing captures it in Cathode. A bonus: the
extractor *running at all* in Cathode is a live probe of the DOM + JS path — if it can't run,
that's a finding before anything is diffed.

**Author the extractor against Cathode's surface, not Chromium's.** Cathode is the subset; Chromium
is the superset, so a script that runs in Cathode runs in Chromium unchanged. Author it against
Chromium's full API by habit and it silently uses something Cathode lacks, breaking the symmetry.
Cathode is the binding constraint.

## The snapshot is the contract

Per rendered node: tag, attributes, computed box geometry, the contract's computed CSS properties,
text. It is app-agnostic — anything that renders a DOM can be observed against it — and it is the
**runtime twin** of the static contract (`lib.cathode.d.ts` + Cathode's own CSS declaration): the same
supported surface, observed instead of declared.

## The reads it needs are just part of the engine

The observer reads geometry and computed style. These are **ordinary engine features, not a special
debug surface** — litehtml already computes the box geometry and the cascade for every element (it
must, to lay out and paint), so the data already exists:

- **`getBoundingClientRect` is a thin read of what litehtml already has** — cheap; a plain engine
  feature. (It was left out of the *authoring contract* only because the panels don't use it — a
  scope choice, not a difficulty one. It is not gated behind anything.)
- **`getComputedStyle` is more involved** — it maps litehtml's resolved values into a JS-readable
  form — but bounded: the observer needs only the contract's CSS properties, not the full CSSOM, so
  scope it to those.

So the observer uses ordinary DOM reads that both engines support; nothing is hidden behind a
validation flag.

## The diff: closeness, and gap discovery

The intermediary parses both snapshots and produces a **closeness report**, not pass/fail: per-node
geometry deltas, missing/extra nodes, style mismatches. For anything Chromium rendered that
Cathode's subset doesn't support, it emits a **gap entry** rather than a failure. One layer, double
duty:

- **conformance** — in-subset content must match the oracle;
- **gap discovery** — out-of-subset effects (the sandbox deliberately exceeds the subset:
  `text-shadow`, `@keyframes`, etc.) become a precise inventory of *what the engine would need to
  fork litehtml for*. That is what the sandbox is for: a discovery baseline, distinct from the
  in-subset functional-gate corpus in `testing.md`.

## Boundaries

- Observation covers structure, geometry, and computed-style parity — not visual fidelity (the
  human taste call) and not the libraries' internal correctness.
- Apps are observed, never modified.

## Pending

- **The shared observer module** (authored to Cathode's surface) and the **intermediary differ**
  that consumes both console dumps into a closeness/gap report.
- **`getComputedStyle`** scoped to the contract's CSS properties on the engine side.
  (`getBoundingClientRect` is a plain engine read; no special work.)
