# Sprint 012 — Cathode Host Bridge: the JS-facing engine surface & the single-source projection

## Nature

The **surface-build sprint** that pairs with 011. 011 built the verification *faculty*; 012 builds the
*surface* the faculty verifies — the JS-facing engine bindings (DOM members + standard globals) — via one
**generate-from-interface projection** so the runtime bindings, the author types, and faculty-A's
descriptor cannot drift. It runs **immediate-next-after-011**; faculty-A's 011 DOM/globals reconcile is
framework-ready-but-dormant and **completes on this sprint's descriptor, in lockstep**.

> "One annotated C++ interface is the single source; a single generate-from-interface projection produces
> the quickjs bindings, the author `lib.cathode.d.ts`, and the runtime-verification descriptor — one
> extraction, three outputs, locked in step by construction." (`components-host-bridge.md`, ratified
> 2026-06-14 with tcdent.)

## Scope Boundary

**This sprint owns:**
- **[C] the single-source projection** — one C++ binding-definition per member that BOTH installs the
  quickjs binding AND is enumerable → the three outputs (bindings + `.d.ts` + faculty-A descriptor),
  reading the 007 non-UObject binding-interface, **no UHT/UObject, no header-parser required**;
- **[A] the DOM JS member bindings** — the C++-functions→JS half (007-Deferred-#1) over the existing 008
  `FDomNode` methods, each member per `components-dom-surface.md`, marshaled via 007's vocabulary;
- **[B] the standard globals** — wire the unwired `OnInstall*` seams in `Worker::Run` (console,
  `setTimeout`/`clearTimeout`, `requestAnimationFrame`) backed by the existing runtime tick + `ICathodeClock`;
- the **game.* registration seam** only (the plug-in point).

**This sprint does NOT own:**
- **game.* content / organization** — sim-deferred; the `UObject→JS` selection over existing UE subsystems
  (where UHT is natural) is its own later concern. Registration seam only.
- the **reactivity / change-signal model** — deferred (team's call).
- **faculty-A itself** — 011 built it; 012 emits the descriptor faculty-A consumes (lockstep), it does not
  build the faculty.
- **re-architecting the 007 binding layer** — extend it in the same hand-rolled non-UObject style, do not
  rebuild it.
- the lead running builds or implementing — the developer drives via `uptime.env`, the reviewer verifies.

## Problem

The JS-facing engine surface is **declared but not installed**:
- `RegisterDomNodeClass` registers the DOM quickjs class with **zero members** — 007 built the
  C++-objects→JS half (class + opaque + finalizer + gc_mark = wrapper/lifetime); the C++-functions→JS half
  (007-Deferred-#1) is unbuilt, so `el.appendChild` / `el.childNodes` etc. are not bound. The C++ methods
  themselves **already exist** (008 `FDomNode`).
- the `OnInstall*` global seams are **unwired in production** `Worker::Run` (set only in the assembly-order
  test), so `console` / timers / `rAF` are not installed.
- there is **no program-readable declared interface** for faculty-A: the `.d.ts` is an unbuilt skeleton and
  `capability-manifest.toml` is superseded — so 011's faculty-A DOM/globals reconcile has no descriptor to
  land on.

## Goal

The JS-facing engine surface (DOM members + standard globals) is **bound and reachable** from author JS,
produced by **one** generate-from-interface projection (the single C++ binding-definition per member →
bindings + `.d.ts` + faculty-A descriptor, locked in step), with game.* a registration seam only. 011's
faculty-A DOM/globals reconcile **completes on the emitted descriptor**.

> "The DOM and the standard globals are bound by Cathode's own non-UObject mechanism with no Unreal
> dependency; UHT/UObject reflection, if used at all, lives only [in `game.*`]." (`components-host-bridge.md`.)

## Final State

1. Each bindable DOM member (per `components-dom-surface.md`) is **defined once** in a C++ binding-definition
   (JS name + the C++ `FDomNode` method + arg/return marshaling via 007's vocabulary) that **both** installs
   the quickjs member binding **and** is enumerable. In JS, `el.appendChild(x)` → `FDomNode::AppendChild`,
   `el.childNodes` → the getter, etc.
2. The standard globals (`console`, `setTimeout`/`clearTimeout`, `requestAnimationFrame`) are installed via the
   now-wired `OnInstall*` seams in `Worker::Run`, backed by the runtime tick + `ICathodeClock`.
3. The single projection emits **three outputs from the same table**: (a) the quickjs runtime bindings
   [install], (b) faculty-A's runtime descriptor [enumerate], (c) the author `lib.cathode.d.ts` [a dev/build
   dump]. Install + enumerate from one source → **no drift** ("the interface IS the bindings").
4. **game.*** is a registration **seam** only — the plug-in point; no game.* content, no `UObject→JS`
   selection (sim-deferred).
5. 011's faculty-A DOM/globals reconcile (framework-ready-dormant) **completes on the descriptor**: the runtime
   now exposes the members/globals, the descriptor provides the declared baseline, and the reconcile shows the
   real residual drift. **Lockstep with 011.**

## In Scope (ordered slices)

1. **[C] the single binding-definition mechanism** — the projection foundation. Establish the FORM (a
   registration table / a `CATHODE_BIND_METHOD(name, &FDomNode::Foo, …)` macro / a small codegen — the dev's
   how; recommend the leanest table/macro over a header-parser, matching 007) that yields **install +
   enumerate from one declaration**. Prove it with a first member (or small set): the binding installs AND the
   descriptor enumerates, both from the one definition.
2. **[A] the DOM member bindings** — bind each `FDomNode` method/accessor (per `components-dom-surface.md`) as a
   JS member through the single-definition mechanism; the full DOM member axis, 007-marshaled.
3. **[B] the standard globals** — wire the `OnInstall*` seams in `Worker::Run` (console, `setTimeout`/
   `clearTimeout`, `rAF`) backed by tick + `ICathodeClock`; the globals ride the same projection/descriptor.
4. **the descriptor emit + faculty-A lockstep** — emit faculty-A's runtime descriptor from the binding table;
   confirm 011's faculty-A DOM/globals reconcile completes on it (the dormant reconcile activates; drift shrinks
   to the real residual).
5. **the game.* registration seam** (minimal) — the plug-in point; no content.

## Non-Goals

- Do **not** build game.* content / organization — registration seam only (sim-deferred).
- Do **not** use UHT/UObject reflection in the DOM or globals — it lives only in game.* (sim-deferred).
- Do **not** re-architect the 007 binding layer — extend it in the same non-UObject style.
- Do **not** introduce a heavy header-parsing codegen — the leaner table/macro is the recommendation (the
  dev's how, but no heavyweight tooling).
- Do **not** reintroduce a hand-maintained declared list — the interface-as-source contract eliminated it;
  install + enumerate from the **one** table.
- Do **not** regress the accepted baselines — the S1 unblock, the link-time build-id, and 011's faculty stay
  intact.

## Starting Surface

Audit first:
- the **007 binding layer** (`RegisterDomNodeClass`, `FNativeClassDef`, the marshaling vocabulary) — what to
  extend;
- the **008 `FDomNode` methods** — the member set to bind;
- **`components-dom-surface.md`** — the declared member contract (the reconciled DOM surface);
- the **`OnInstall*` seams** in `Worker::Run` + the assembly-order test — how the globals install;
- the **tick + `ICathodeClock`** — the globals' backing;
- **011's faculty-A descriptor-consumption point** — the shape the descriptor must take for faculty-A's
  reconcile to land on it (lockstep).

Inventory shape: `member → C++ method (008) → marshaling (007) → {install + enumerate} binding-definition → the three outputs`.

## Method

- **One definition per member** — install + enumerate both derive from it; never a parallel list (no drift,
  interface-IS-source).
- **Drive the real primitive** — the real quickjs class, the real `FDomNode` methods, the real 007 marshaling;
  test real behavior (`el.appendChild(x)` actually appends and JS sees the result; a timer actually fires on
  the tick), never config/string presence.
- **Lockstep with 011** — the descriptor shape matches what 011's faculty-A reconcile consumes.
- **Coherence per-result-run on the link-time build-id**; build/run via `uptime.env`.
- **Extend 007, don't rebuild; no UHT.**

## Acceptance

- Each DOM member (per `components-dom-surface.md`) is **callable from author JS** and dispatches to its
  `FDomNode` method with correct marshaling — **driven + asserted live**.
- The standard globals are **installed + functional** (console logs; a timer fires on the tick; `rAF`
  schedules) — driven + asserted live.
- The single binding-definition emits **all three outputs from one source**: the runtime install (members
  work), the enumerable descriptor (faculty-A's declared baseline), the `.d.ts` dump — install + enumerate
  provably from the **same** table (no parallel list).
- 011's faculty-A DOM/globals reconcile **completes on the descriptor** (the dormant reconcile activates; the
  runtime exposes the members/globals; the reconcile shows the real residual drift). Lockstep verified.
- game.* registration seam exists (no content).
- Coherence per-result-run on the build-id; the accepted 011 + S1 baselines do not regress.

## Review Questions (open forks)

1. **The single-definition FORM** (table / macro / small codegen) — the dev's how; recommend the leanest
   (table/macro over a header-parser). Settled in slice 1.
2. **The DOM member SET** — `components-dom-surface.md` is the declared contract; confirm the reconciled member
   list (browse-design grounded it as faculty-A's baseline).
3. **The descriptor SHAPE for faculty-A lockstep** — the shape 011's faculty-A reconcile consumes; coordinate
   with the 011 faculty-A descriptor-consumption point so the dormant reconcile lands cleanly.
4. **The game.* registration seam extent** — how minimal (the plug-in point only; no content).

## Outcome

Delivered: the full JS-facing engine surface via the single generate-from-interface projection, the
standard globals, the document subsystem + mount lookup, the first-mount marquee (a real upstream
Preact panel renders end-to-end on Cathode), and the full DOM-completion member surface (events,
selectors, snapshot-returns, forms, style-with-CSS-stubs, focus/body/namespaceURI). Node table 35
members + document table 9 + getComputedStyle global, all install+enumerate from the one table with
the per-class descriptor lockstep; review confirmed no documented member missing. Deferred: the
explicitly-stubbed CSS interactions (the CSS epic, building to `docs/browse/css-epic-contract.md`),
and the contingent `getElementsBy*`/live-HTMLCollection (browse-design's breadth call if/when —
parked as backlog).

Commits (Host Bridge → first-mount → DOM-completion): mechanism `c0a219b` → node-core `875d223` →
reflected-props `c8a24c9` → globals `3d38067` → document `ec53276` → first-mount-proof `fde3c09` →
events `4ea160a` → selectors-floor `458b182` → selectors-trio `04920b4` → snapshot-returns
`888418b` → forms `811b71d` → style+CSS-stubs `9426775` → focus/body/namespaceURI `e583d31`.

## Postmortem

These are durable lessons, appended at close; they do not rewrite the contract above.

### Lead (uptime.browse-lead)

- **Audit-first, slice-by-slice, evidence against the contract.** Every slice opened with the dev's
  doc-grounded audit (grep the design-of-record, map built-vs-stubbed, propose the immediate slice),
  not a pre-framed plan. This caught undocumented members on selectors, snapshot-returns, and
  focus (the companions/trio shape) before any code, and surfaced the real forks early. Reviewer
  acceptance gated at slice boundaries; mid-slice phases (e.g. the gc_mark fold-in) cycled inside the
  dev's build loop.
- **Route every fork to its lane owner; do not design.** Design/coverage → browse-design (the
  single-definition form, the #8 object-return recipe, the litehtml-confined matcher option (iii),
  the CSSStyleDeclaration construction, the Attr-item depth, the companions breadth); sequencing/
  decomposition → PM; genuine model/intent → tcdent (the per-surface document lifetime). The lead
  arbitrates evidence against the designer-set gate — it does not set the gate. Saying "I adjudicate
  the gate" is the same overstep the PM made; both were corrected by tcdent (012-S5).
- **A grounded coverage/breadth ruling is the designer's, not a default tcdent-relay.** Deference
  creep (relaying browse-design's grounded coverage as a "tcdent confirm") was flagged twice (the
  selectors trio, getElementsBy*); reserve tcdent for taste/his-intent/irreversible.
- **The lean cadence (tcdent, mid-sprint).** Stop inter-slice PM status/SHA/milestone/ack traffic +
  per-slice green-light round-trips; the lead OWNS routine slice commits (reviewer ACCEPT + explicit-
  path scope-verify + the PM's pre-stated checklist), reserving PM contact for decisions + sprint
  completion. This cut the noise materially.
- **The gc_mark thread is the model case for booked deferrals.** Slice 5 removed document.gc_mark as
  redundant but BOOKED a carry-not-rediscover re-check trigger (binding the event API). When events
  fired it, the dev re-evaluated rather than blindly re-adding — and the answer INVERTED the naive
  prediction (re-add is unsafe/UAF under the C++-external-RootWrapper/null-finalizer model), settled
  by two independent convergent GC re-derivations (review blind + dev) → browse-design. Book the
  trigger AND its condition; re-evaluate at the trigger, don't assume.
- **013 inherits:** the `css-epic-contract.md` stub boundary as the consumer-driven contract the CSS
  epic builds to (the DOM defines the OPERATIONS, the epic owns the AUTHORITY — cascade authority /
  supported-set source / computed-value granularity / `<style>`-ingress shape, all deferred); the
  design-fork watch at the CSS boundary (a silent CSS-contract fix surfaces lead → human); the
  staged CSS order (capability declaration → var() feasibility → GetDocumentCss/cascade →
  getComputedStyle values → element.style re-cascade); the downstream joints already wired (1-line-
  real on QueryComputedValue, the input router on FocusedNodeId).

### Dev (uptime.browse-dev / Mundo)

- **The loop that worked: audit-first + doc-grounding + evidence-from-the-settled-logs.** Grep
  `components-dom-surface.md` first — it caught a scope-delta on nearly every slice; build the
  documented set, FLAG undocumented members UP (never silently add/drop), let browse-design/PM rule.
  Route forks by kind (design → browse-design, decomposition → PM, mechanical → my how); never
  resolve a design fork downstream. Verify from the SETTLED logs myself — the build log (0 errors +
  the relink-set) AND the run log (per-test {Success} + the coherence aggregate advancing the touched
  module); negative controls PROVEN by the recompile-set (a litehtml change not relinking CathodeDom),
  not asserted.
- **Evidence beat the prediction.** gc_mark INVERTED at events (re-add is unsafe/UAF, not "returns").
  The selector matcher had to be a live-FDomNode-tree structural walk over a neutral interface —
  litehtml's matcher is bound to a per-layout THROWAWAY tree with no bridge to the live tree (the
  post-mutation discriminator proves it). A directly-registered exotic class needs a fresh class id
  per runtime (env isolation runs located the real fault; a full-suite stack misled).
- **Honest misses, caught by compile/env/review (no quiet workaround).** The GetValue "no callers"
  claim came from a HEAD-truncated grep where a same-named `FJsValue::GetValue` hid the real caller →
  C2660 (trace callers EXHAUSTIVELY; same-name-on-another-type is the trap). The `JS_NewClassID`
  idempotency AV took 3 cycles (per-context globals + reset id=0 for a fresh per-runtime id). Account
  every miss UP to the lead.
- **013 inherits:** the stub boundary (`css-epic-contract.md`) the epic builds TO (the DOM did not
  gate on CSS); the downstream joints as 1-line rebinds (getComputedStyle→FResolvedStyle,
  element.style→the canonical `style` attribute SoT, ACCEPT-ANY validation until the declaration);
  the litehtml-confined neutral seam (no litehtml type in CathodeDom, by-the-negative); the come-due
  discipline (a deferral NAMES its consumer; when it arrives, the come-due IS the missing primitive —
  build the storage+read and verify set-site completeness via a single mint funnel).

### Review (uptime.browse-review)

- **The per-target discriminating oracle (breaking-geometry).** Every proof gated on a check that
  FAILS without the property, never "it's green / it returned something": the cross-path SoT (a
  parallel store passes same-path, desyncs cross-path); the four-step dirty-value (always-live or
  always-attribute each fails specific steps); the static-snapshot (capture → mutate →
  captured-unchanged); the firing-order EXACT sequence; leaks-before/reclaimed-after GC; litehtml
  RELATIONAL invariants (never a hand-typed pixel). Re-derive from SOURCE + the settled post-build
  run, NEVER the report (caught a loose coherence framing, a mis-attributed relink, the GetValue-
  caller miss). By-the-negative layering (a litehtml-side change relinks ONLY CathodeLayout, CathodeDom
  0). Would-it-pass-if-test-only / reachable-through-the-real-address (production install + the shared
  assembly + globalThis.document; the env-only shim-audit). Coherence-first on the cross-module
  AGGREGATE buildId (a repeated aggregate on a should-be-fresh run = reject-incoherent).
- **Re-derivations that changed a verdict.** The gc_mark survival-test-is-vacuous (a BLIND
  re-derivation, convergent with the dev → browse-design ruled stays-removed/re-add-unsafe); the
  multi-runtime class-id AV root cause + the fresh-zero/per-context fix sound (Subsystem + T4 green);
  the exhaustive GetValue-caller re-derivation (the dev's truncated grep + the name-collision hid a
  caller — the load-bearing catch).
- **Standing reject bars (durable):** a desyncing parallel store; a stub hollowing a proof (provably
  out-of-scope of every come-due proof, structurally incapable of routing through it, never a vacuous
  half); a forked install (drive the SHARED assembly, share-don't-fork); the four-step dirty-value;
  set-site completeness (single-funnel or exhaustive enumeration); METHOD over counters (reject
  incoherent / stale-binary / wrong-path / forked-install even at ZERO mismatch).
- **013 inherits — the GATE FLIPS:** 012 stubbed the cascade/empty values BY DESIGN; 013 BUILDS
  them, so the bar flips from "stub-coherent" to "values REAL against the primitive's own output" (an
  empty value correct in 012 is a DEFECT in 013) — gate each operation DISCHARGING its 012 stub
  (QueryComputedValue → real values, the cascade, the capability declaration authored from
  css_properties.h NEVER fabricated [replacing accept-any], the `<style>`-ingress). The design-fork
  watch holds (a silent CSS-contract fix surfaces lead → human). The FORK-1 firewall must HOLD through
  the epic (CathodeDom→litehtml stays absent). Standing anchors: the aggregate buildId coherence,
  re-derive-from-source, the per-target discriminating oracle, by-the-negative layering.
