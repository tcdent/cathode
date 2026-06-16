# Cathode — Runtime Verification (detail)

A **built-in Cathode capability** that, at runtime, discovers the surface the engine *actually*
exposes and verifies the real primitives *actually* work — emitting a structured, machine-readable
self-report. It **folds in the surface-discovery prior art** (above all our own `observe/observer.js`)
as a first-class module artifact, superseding the earlier throwaway one-off discovery pass.

What/why at design-of-record altitude — not the internal *how*, which is the team's.

> **Ratified — settled design-of-record** (tcdent, 2026-06-14). This artifact is folded into Cathode
> as a built-in capability; the `observe/` harness is **transferred to browse-team ownership,
> refactored, and integrated** as its in-engine home; the build is scoped **next-sprint**. The
> rulings are reflected throughout and in *Resolved* below — this doc is no longer a proposal.

## Why it exists

The build gate is **static**: it proves author code stays *inside the declared surface* (the `.d.ts`
generated from the interface). But "declared" ≠ "actually exposed and actually working at runtime."
Two gaps the static gate structurally cannot see:

1. **Surface drift — does the *live* exposed surface match the declared contract?** The generated
   `.d.ts` says what *should* be there; only runtime introspection says what *is*. If the
   implementation under- or over-exposes a member vs. `spec-surface.md` / `components-dom-surface.md`,
   nothing static catches it.
2. **Unverified-by-construction integrations — a member can be present and still not function.**
   This is not hypothetical: the COVERAGE ledger systematically books a **"validation-pass"** class of
   deferral — correct-by-construction, unproven until a real run. Confirmed in the team's own records:
   `CathodeDom/COVERAGE.md` #8 *"Real layout output — validation-pass"* (geometry/observation values
   are only real once laid out) and #2 *cyclic node↔listener reclaim* (proven GC-*visible*, but the
   actual reclaim is a real-`JS_RunGC` validation-pass); plus the rasterizer/surfaces first-execution
   risks (real glyph output, 4K AA, pixels-reach-texture). **Declared-correct can still be
   runtime-broken.** browse-pm's 010 bring-up named four such integrations (real litehtml output,
   decode-for-size, real observation geometry, cyclic GC) — these are the **first concrete targets**,
   not the definition.
3. **The AI-dev-loop reason.** Machine developers need an automated, accurate way to *see* what their
   code actually exposed and whether it works — without a human as the oracle (the perception goal in
   `observation.md` / `testing.md`). A built-in self-report that *names what is exposed and what works*
   is that perception surface.

## Prior art it folds in (grounding)

Surveyed across the read-only references; two recurring patterns, both inspiration here:

- **`observe/observer.js` (ours) — the keystone.** Walks the live DOM, reads the *actually-exposed*
  members (`childNodes`/`nodeType`/`nodeName`/`attributes`/`nodeValue` + `getBoundingClientRect` +
  `getComputedStyle`), and `console.log`s one structured JSON snapshot — run identically in two
  engines. The *introspect-the-real-surface-and-emit-a-structured-report* mechanism. **Ratified
  (tcdent, 2026-06-14) to transfer to browse-team ownership, be refactored, and integrated here as its
  in-engine home** — its README already anticipated rolling upstream once the loop was validated.
- **obs-browser** — `exposedFunctions` enumerated and dynamically registered + `IsValidFunction()`
  gate (`browser-app.cpp`): the exposed surface *as data* you can enumerate and check. And
  `obs_cef_all_drm_formats_supported()` (`drm-format.cpp`): a runtime *capability probe* of the
  backend before relying on it.
- **UE-Uranium** — `GetKeys()` reflective enumeration of live CEF objects (`CefV8Utils.cpp`) and
  `UProperty` reflection (`CefJsObjectSerializer.cpp`): reflect over the *live* exposed object surface.
  `IsApplicable()` + `CurrentRHI()` → `CreateSharedTexture()` (`SharedTextureInterface.*`): a
  multi-factor runtime capability probe that *selects the path that actually works*.
- **cef-spout** — `create_device()` feature-level fallback (`d3d11.cpp`): graceful capability
  negotiation — try, detect what's supported, fall back.
- **UCefViewPlayGroud** — query-string late-binding dispatch (`CefViewQuery.h`): runtime handler
  discovery (minor).

## Two faculties

The artifact unifies the two patterns above:

- **(A) Surface introspection.** Enumerate the surface the engine *actually exposes* at runtime — the
  DOM members and their returns, the provided globals, the CSS properties litehtml actually honors —
  and reconcile it against the **declared contract** (`spec-surface.md` / the generated `.d.ts`).
  Answers *"is what's exposed what we promised?"* (Lineage: `observer.js` walk + obs-browser/Uranium
  enumerate-and-reflect.)
- **(B) Capability verification.** Exercise the real primitives and confirm they *function*, not just
  exist: geometry returns real (non-stub) boxes, layout produces real output from real input, decode
  yields real dimensions, the node↔listener cycle is actually reclaimed by a real GC. Answers *"does
  the real thing actually work?"* (Lineage: obs-browser/Uranium/cef-spout capability probes.) The
  COVERAGE "validation-pass" deferrals are exactly its work-list.

**Output:** a structured, machine-readable **self-report** (the `observer.js` JSON-snapshot lineage,
extended with a capability section) — so the team and the AI dev loop read what's actually
exposed/working without a human eyeballing it.

## Boundaries

- **Owns:** the in-engine introspection + capability-probe faculties and the structured self-report.
- **Shares the introspection *mechanism* with `observation.md`** — the same surface walk — but the
  *purposes differ and must not be conflated:* `observation.md` is **external rendering parity**
  (Cathode vs. the Chromium oracle); runtime-verification is **internal self-report** (Cathode vs. its
  own contract, and do its primitives work). Don't duplicate the walker; this built-in faculty is the
  one `observation.md`'s observer can also use.
- **Complements `testing.md`** (functional gates assert *specific* behaviors; this reports the *whole
  real* surface + capability state) and **the contract/gate** (static: author code ⊆ declared surface;
  this is the runtime counterpart: declared surface == actually-exposed-and-working surface).
- **Delegates** the underlying reads — geometry/cascade to Layout & Style, node/GC to the JS↔Native
  Bridge — it *composes and reports*, it does not reimplement.

## Resolved (ratified 2026-06-14)

- **Build timing — next-sprint.** Not 010 (bring-up/observe-don't-build); this artifact is a build and
  it builds *on* the 010 inventory, which tells it what to verify.
- **`observe/` — folded in, browse-owned.** The standalone harness is **transferred to browse-team
  ownership, refactored, and integrated** as this artifact's in-engine home — an ownership transfer +
  refactor, not merely a roll-in. (`observe/README.md` and `observation.md`'s external-observer
  references reconcile to this when the refactor lands.)
- **Faculty-(B) first targets.** The four 010 unverified-by-construction integrations — real litehtml
  output, decode-for-size, real observation geometry, cyclic GC — stand as the initial
  capability-verification work-list. The faculty's purpose stays general; these are first, not the
  definition.

## To settle during the build (the team's *how*)

- The exact **self-report schema** (the `observer.js` snapshot lineage + a capability section).
- The internal **mechanism** of introspection and capability probes.

**Faculty-A's declared baselines (2026-06-14):**
- **DOM + globals** reconcile against the **generate-from-interface descriptor** — the third output of
  the single projection (alongside the quickjs bindings and the `.d.ts`), built in the **Host Bridge /
  generate-from-interface sprint** (`components-host-bridge.md`). Dormant until that descriptor lands.
  No hardcoded list, no manifest (superseded), no separate faculty-A extraction.
- **CSS** reconciles against **Cathode's own CSS capability declaration** — authored in
  `Cathode/Authoring/`, grounded in **litehtml's actual coverage** (the true source of what the engine
  supports, more correct than any consumer-side approximation). That declaration is **not yet authored**
  (the `Authoring/css-allow-list.toml` 001 skeleton is a stub), so the CSS axis is **dormant until
  Cathode authors it** — parallel to DOM/globals being dormant until the 012 descriptor. Panels'
  `panels/css-subset.json` is the **panels team's own separate guard** — Cathode neither uses, moves,
  nor depends on it (the *internalize* principle: author our own conformant declaration from intention,
  never couple to another dev's area).

## Cross-refs

`observation.md` · `testing.md` · `components-dom-surface.md` · `spec-surface.md` ·
`components-authoring-contract-gate.md`. Prior art: `observe/observer.js`;
`vendor/lib/explore/{UE-Uranium,obs-browser,cef-spout}`; `vendor/UCefViewPlayGroud`.
Grounded 2026-06-14.
