# Sprint 008 — Cathode DOM scaffold

## Nature

A **no-build / no-execution scaffold** sprint, continuing the per-component house pattern (002–007).
The component is the **DOM** (`docs/browse/components-dom.md`) — the live retained document author
JavaScript manipulates and the source of truth fed to layout. It is the largest component yet and the
first to **compose three built siblings**: 007 `CathodeBinding` (node-class binding + lifetime), 005
`CathodeLayout` (layout via markup), 006 `CathodeSurfaces` (the surface's document). It wraps **no new
vendor**; it is bespoke DOM logic over the sibling seams, and it is quickjs-type-coupled **by design**
through 007's public API (the accepted cost of 007's "expose the spec" ruling — future consumers
inherit that coupling).

This component is where **Model A comes due**: 007 ruled node lifetime = the JS wrapper's refcount,
source of truth = the JS wrapper graph, **no independent C++ root**. The DOM must honor it.

**005 permits Model A; it does not confer it** (the linchpin, independently confirmed against 005's
accepted source). 005's layout seam is markup-serialization — `ILayoutDocumentSource` feeds HTML/CSS
and id-addressable structure reads, and `CathodeLayoutEngine::Render` rebuilds litehtml's tree fresh
from the markup each render into a local document that is discarded; no DOM C++ node ever crosses into
litehtml, which retains only a non-owning borrow of the source. So feeding 005 **does not force** a
retained C++ root. But the seam still requires the DOM to maintain, at relayout, an id-addressable node
structure (`NodeId`→node, parent/child, layout-boundary); whether that backing structure is
**wrapper-governed (Model A)** or an **independent C++ root (Model B)** is a DOM-design choice 005
cannot make. Keeping it wrapper-governed is therefore an **explicit design obligation of this sprint**,
carrying 007 COVERAGE deferral #4 forward — not a property 005 grants.

> "No split. Follow the doc."

## Scope Boundary

**This sprint owns:**
- the **retained node tree** — elements, text nodes, attributes, parent/child structure — held as the
  **JS wrapper graph** (Model A), persisting between ticks, not re-parsed;
- the **bounded DOM API** — element create/insert/remove, attribute/property access, tree reads — and
  nothing more (the firewall);
- the **event system** — the `Event` object, `add`/`removeEventListener`, `dispatchEvent` with
  bubbling;
- **observation reads** — `getBoundingClientRect` / `getComputedStyle`, backed by 005's computed
  geometry/style, exposed as engine reads for the observation layer and **not** advertised on the
  author surface (a scope choice, not a difficulty one);
- **dirty-marking on mutation** — flag the affected region so 005 relayouts only it.

**This sprint does NOT own** (named, with the owner):
- the **cross-heap lifetime mechanism** — 007 `CathodeBinding` owns `gc_mark`/finalizer/`FreeRT`; the
  DOM owns only the **reference-drop points** (`removeChild`, `removeEventListener`);
- **layout** — 005 `CathodeLayout` owns it; the DOM **serializes itself to markup** and drives the
  engine, it does not lay out;
- the **surface** — 006 `CathodeSurfaces`; at integration a surface's `ISurfaceDocument` resolves to a
  DOM;
- the **Authoring Contract & Gate (`.d.ts`)** — a downstream consumer; the DOM's *frozen surface*
  feeds it, but the `.d.ts` is its own concern.

## Problem

No live document exists. Author JS needs a retained, mutable node tree that persists between ticks and
is the input layout consumes; interaction needs events with bubbling; and the node-lifetime handshake
with 007 is **the component's main risk** — listeners capture nodes, forming the cross-heap cycle 007's
Model A is built to handle. None of it exists; 005/006/007 expose the seams the DOM composes.

## Goal

The bounded DOM — retained tree (the JS wrapper graph, Model A), the bounded API, events with
bubbling, observation reads, and dirty-marking — composing 007/005/006, authored complete and tested
against our own bookkeeping, with the GC-timed and real-layout behaviors deferred.

The surface is **deliberately small — that bound is the firewall against accidentally rebuilding a
browser.** Following the doc, the canonical surface is frozen to the bounded set (below); the widening
sugar is deferred.

On node lifetime, the doc's key-concern wording ("the DOM owns the *trigger points* for teardown
`removeChild`/`removeEventListener`; the bridge owns the mechanism") **predates and is superseded by
Model A**: those are **reference-drops**, not explicit-free triggers — `removeChild` drops the
parent's JS ref to the child, `removeEventListener` drops the node's held listener ref, and 007's
`gc_mark` + finalizer reclaim. Recorded in COVERAGE as the doc-premise override (as 007 did).

## Final State

1. **Module `CathodeDom`** — `Build.cs` depends `Core` + `CathodeBinding` + `CathodeLayout` +
   `CathodeSurfaces`; wraps no new vendor (composes the sibling APIs). Quarantine intact: `.uplugin`
   `Modules[]` gains `CathodeDom`; plugin `EnabledByDefault:false`; `Cathode` absent from
   `game/uptime.uproject` `Plugins[]` — disabled-not-compiled.
2. **The DOM node** — a C++ object registered as a quickjs class via 007 (`INativeClassRegistry::
   RegisterClass` → class id; `CreateWrappedObject` attaches the C++ node as the opaque). It registers
   a **`gc_mark`** that marks its held refs (children, listeners, JS-valued attrs) via
   `FJsValue::Mark`, and a **finalizer** (`FinalizeNativeObject<…>`) freeing the native side. Lifetime
   = the JS wrapper's refcount (Model A).
3. **The retained node tree** — the **JS wrapper graph** rooted at the document; parent→child links
   are JS refs (`FJsValue`, `gc_mark`-marked). Persists between ticks; **no independent C++ owning
   root** (any C++ traversal pointer is non-owning/derived).
4. **Bounded structural API** — `createElement`, `createTextNode`; `appendChild`, `insertBefore`,
   `removeChild`; `parentNode`, `childNodes`, `firstChild`, `nextSibling`, `tagName`. `removeChild` is
   a reference-drop (Model A).
5. **Attribute/property access** — `getAttribute`/`setAttribute`/`removeAttribute`, property access;
   values cross the boundary via 007's marshaling (Number/String/Bool). Includes `value` (get) and bulk
   `textContent` (get/set) per the freeze.
6. **The event system** — the frozen `Event` shape + `KeyboardEvent { key, code }`;
   `addEventListener`/`removeEventListener` (listeners held via 007 `FCallableHandle`;
   `removeEventListener` is a reference-drop); `dispatchEvent` with **bubbling** (bubble phase per the
   doc; capture phase deferred).
7. **The layout handshake** — implements 005's `ILayoutDocumentSource` (structure reads +
   **markup serialization** `GetDocumentHtml`/`GetSubtreeHtml`/`GetDocumentCss`) and drives
   `ILayoutEngine` (`SetDocument`/`MarkNodeDirty`/`Relayout`). litehtml rebuilds its C++ tree fresh from
   the markup each render — a **transient layout artifact**, not a retained root. The id-addressable
   structure the DOM maintains to answer the seam (`NodeId`→node, parent/child, layout-boundary) is
   **wrapper-governed** — 005 permits Model A; the DOM keeps it (it is not an independent C++ root).
8. **Dirty-marking on mutation** — each mutating API call marks the affected node/region
   (`MarkNodeDirty`) so 005 relayouts only the affected subtree.
9. **Observation reads** — `getBoundingClientRect` / `getComputedStyle` backed by 005's
   `FLayoutResult` geometry/style; **engine reads for the observation layer**, not the author contract,
   not in the `.d.ts`, no flag-gate.
10. **The frozen canonical surface** (the team's freeze, following the doc's firewall): minimal `Event`
    + `KeyboardEvent{key,code}`; `value` **in**; bulk `textContent` get/set **in**; `checked`,
    `DocumentFragment`, `classList` **deferred**; pointer/mouse `Event` fields **deferred** (with
    pointer input). This surface is the source for the downstream `.d.ts`.
11. **Tests** (006/007-style, recording/bookkeeping) for the bespoke logic: tree-structure invariants
    and reference-drop-frees-by-our-`gc_mark`/finalizer count; attribute/property round-trips through
    007 marshaling; event dispatch + **bubbling order** by our dispatch bookkeeping; dirty-mark on each
    mutation by our `MarkNodeDirty` calls; the **markup serialization** output for a known tree (our
    serialized string). **Zero** assertions on actual GC reclamation, litehtml internals, or real
    layout output.
12. **`CathodeDom/COVERAGE.md`** — every responsibility covered-or-deferred with
    reason/dependency/unblocker/first-exec-risk; the **Model A doc-premise override** recorded; the
    frozen surface + the deferred surface items; deferred behaviors (real GC reclamation = validation-
    pass; real layout via litehtml = validation-pass; first-build = execution); the `.d.ts` named as
    downstream.
13. **`TODO-PREBUILD` tags** on any fork-variable seam calls the DOM authors (largely inherited — the
    DOM uses 007's already-tagged binding API and 005's markup seam).

## In Scope (ordered slices)

1. **Surface + Model A node-lifetime contract + sibling-seam audit.** Define `CathodeDom`'s public
   surface shapes and the node-lifetime contract (the JS-ref child link, `gc_mark`/finalizer
   registration via 007). Produce the Starting-Surface inventory. *Method:* compose the sibling APIs;
   honor Model A (no independent C++ root).
2. **Module + the node class + the retained tree.** Establish `CathodeDom` (Core + the three siblings;
   quarantine-safe `.uplugin` edit). Register the node class via 007 (class id + opaque + `gc_mark`
   marking held refs + finalizer). Build the retained tree with **JS-ref** parent/child links.
3. **Bounded structural API + dirty-mark on mutation.** `createElement`/`createTextNode`,
   `appendChild`/`insertBefore`/`removeChild` (reference-drop), tree reads; each mutation marks the
   affected region via 005's `MarkNodeDirty`.
4. **Attribute/property access + marshaling.** `getAttribute`/`setAttribute`/`removeAttribute`,
   property access, `value`, bulk `textContent` — values via 007 marshaling.
5. **Layout handshake + observation reads.** Implement 005's `ILayoutDocumentSource` (structure +
   markup serialization) and drive `ILayoutEngine`; expose `getBoundingClientRect`/`getComputedStyle`
   off 005's geometry/style (off-contract).
6. **Event system + bubbling** (the heaviest). The frozen `Event`/`KeyboardEvent`;
   `addEventListener`/`removeEventListener` (listeners as 007 `FCallableHandle`, removal = reference-
   drop); `dispatchEvent` with bubble-phase propagation + `stopPropagation`/`preventDefault`.
7. **COVERAGE + finalize.** Author `COVERAGE.md`; record the Model A override + the freeze + the
   deferrals; place any `TODO-PREBUILD` tags; confirm the close-state checklist.

## Non-Goals

- Do **not** build the cross-heap lifetime mechanism — 007 owns `gc_mark`/finalizer; the DOM owns the
  reference-drop points only.
- Do **not** build layout — 005 owns it; the DOM serializes to markup and drives the engine.
- Do **not** retain a C++ node tree as an independent root — that is Model B and violates 007's ruling.
- Do **not** widen the author surface beyond the freeze (the firewall); observation reads stay
  off-contract (not in the `.d.ts`, no flag).
- Do **not** build the `.d.ts` / Authoring Contract & Gate — a downstream consumer.
- Do **not** include the deferred surface items (`checked`, `DocumentFragment`, `classList`;
  pointer/mouse `Event` fields; capture-phase dispatch).
- Do **not** build, run, or link. Do **not** delete a fallback just because production does not hit it —
  trace callers and tests first.

## Starting Surface

The framing audit is done (Model A application, the litehtml-markup resolution, the three sibling
seams, the surface inventory). Before editing, the developer produces the inventory in the shape:

```
DOM responsibility -> sibling seam (007 / 005 / 006) or bespoke -> Model A obligation -> proposed action
```

so every authored piece maps to a sibling API or bespoke logic, the Model A obligation it carries, and
the consumer it serves.

## Method

The no-build discipline every slice follows: author the functionality complete per the component doc
even though it cannot run yet (can-not-validate is never a reason to defer). The DOM **composes the
sibling APIs** (007 binding, 005 markup seam, 006 surface document) and wraps no new vendor; it is
quickjs-type-coupled by design (007's inheritance). Tests assert **our** bookkeeping — tree structure,
dispatch/bubble order, dirty-marks, serialized markup, `gc_mark`/finalizer call counts — and **never**
actual GC reclamation, litehtml internals, or real layout output. **Model A is honored**: the JS
wrapper graph is the source of truth, no independent C++ root, reference-drops not explicit-free.
Quarantine stays disabled-not-compiled. The accepted 005/006/007 baselines stay **byte-identical** —
the DOM only consumes their public APIs.

## Acceptance

- `CathodeDom` module exists, composes the three siblings, quarantine intact (`.uplugin` `Modules[]` +=
  `CathodeDom`; `EnabledByDefault:false`; `Cathode` absent from uproject; no Binaries/Intermediate).
- The node class is registered via 007 with a `gc_mark` (marks held refs) and a finalizer; the
  retained tree's parent/child links are **JS refs** — **no independent C++ owning root** (structurally
  verified, grep-clean of raw owning child pointers).
- The bounded structural API, attribute/property access (incl. `value`, `textContent`), and the event
  system with **bubbling** are authored per Final State; `removeChild`/`removeEventListener` are
  reference-drops.
- 005's `ILayoutDocumentSource` is implemented (structure + markup serialization) and `ILayoutEngine`
  is driven; dirty-marking marks the affected region per mutation; observation reads compose 005's
  geometry/style and are off-contract.
- The frozen surface matches the freeze exactly (deferred items absent).
- Tests assert our bookkeeping only — **zero** GC-reclamation / litehtml-internal / real-layout
  assertions; bubbling order and reference-drop-frees are exercised.
- `COVERAGE.md` complete; references resolve; the Model A override recorded; deferrals carry
  reason/dep/unblocker/first-exec-risk (real GC reclamation + real layout = validation-pass; first-build
  = execution).
- The 005/006/007 baselines are byte-identical (consumed, not edited).
- No build / run / link occurred and none is required to verify the above.

## Review Questions (open forks)

1. **Node child-link representation** — the lifetime-governing parent→child link MUST be a JS ref
   (`FJsValue`, `gc_mark`-marked); any C++ traversal pointer is non-owning/derived. The exact
   representation is the node-class slice's (S2) call; the **principle is the Model A inheritance** and
   is non-negotiable.
2. **Markup serialization fidelity** — the DOM→HTML/CSS serialization for layout; escaping/attribute
   encoding bounded by litehtml's `createFromString` expectations; settle in S5. (Real layout output is
   005's and a validation-pass concern, not asserted here.)
3. **Dispatch phases** — the doc names *bubbling*; capture phase is deferred (following the doc's
   bounded surface). Confirm bubble-only in S6.
4. **Future fork (flagged, not this sprint):** 005's seam comment notes the dirty-DOM-fork contingency
   *may* later add a mutable-tree accessor. The current seam is markup + id-reads (Model-A-compatible).
   **If** that contingency ever hands the engine a *retained mutable C++ tree*, it introduces Model-B
   pressure and needs its own Model-A review when it lands — route it then; it must not land unreviewed
   under this sprint's premise.

---

# Postmortem (sprint close)

Sprint 008 closed with all seven slices accepted. It is the largest component yet and the first
**composer** — bespoke DOM logic over three built siblings (007 lifetime, 005 layout via markup, 006
forward-wired), quickjs-type-coupled by design through 007. It is where **Model A came due**, and its
defining moment was the **teardown**: a hand-rolled `~FDomDocument` cascade that the reviewer twice and
correctly rejected, which the human's "fire GC on the DOM" reframe then *deleted* rather than fixed.

## Lead — uptime.browse-lead

- **The teardown arc is the lesson I'm keeping.** When the reviewer rejected the dev's hand-rolled
  cascade twice (untested orphan path; a false symmetric-safety claim), my first instinct was to route
  Opt1/Opt2 (a hard ordering requirement vs a guard). The human cut through it — *"don't we just fire
  GC on the DOM?"* — and the right move was to **ground it, not adopt it**: I routed it to the dev to
  verify against quickjs-ng's GC semantics before letting it reshape the slice. It dissolved the harder
  blocker outright (the destructor became passive/context-free → no UAF in any order) and trimmed the
  other. Lesson: when a hand-rolled mechanism breeds blockers, the answer may be to *delete* it (lean on
  the engine, 007's pattern), not patch it — and a human reframe still gets grounded before it lands.
- **Gate the foundational premise pre-contract.** I had the reviewer confirm the litehtml/Model-A
  linchpin *before* the contract set, and carried its sharpening ("005 *permits* Model A, doesn't
  *confer* it — wrapper-governed is the DOM's obligation") into the contract phrasing. That one
  distinction is what kept the whole sprint honest about what it had to prove.
- **Route undefined-design forks, defer the rest.** The reviewer surfaced the CSS-sourcing fork (the
  source was never specified); I routed it to the human, who deferred it ("the CSS surface will
  materialize in due time"), and it landed in COVERAGE as a confirmed decision with the verbatim quote —
  not a silent contract-fix.
- **Ground contradictions in the tree.** The dev's stale "CathodeRuntime M" note vs the reviewer's
  "it's clean" — I read the tree (007 committed, CathodeRuntime untouched) rather than average them.

## Developer — uptime.browse-dev

- **A hand-rolled mechanism that breeds blockers is the smell you're rebuilding what the engine does.**
  The cascade's untested orphan path + false-safety claim were the tell; the fire-GC reframe deleted the
  cascade for a passive context-free destructor + `Dispose` (runtime release) + `JS_FreeRuntime`
  fallback. Abandon-not-free is the *necessary consequence* of a context-free destructor (the accepted
  leak-until-fallback, recorded as the Dispose integration contract).
- **The lifetime model dictates the signatures.** Every mutator refined from raw `FDomNode&` to
  `FJsValue` wrappers because Model A says the wrapper *is* the lifetime handle (a node must never hold
  its own wrapper = self-root = Model B; listeners must be gc_mark-markable + identity-matchable). The
  principle was fixed; the representation was the realization.
- **The linchpin a reviewer confirms at framing must be *built* to be true** — implementing
  `GetDocumentHtml`/`GetSubtreeHtml` (escaped, sorted-key, deterministic) is what makes "the DOM is the
  source of truth, litehtml's tree is disposable" real.
- **When half a seam is deferred, isolate the INPUT, not the code** — the injectable invoker drives the
  *production* dispatch algorithm; the deferred Event-object exposure is honestly out, not faked.
- **Prove behaviors where they're exercised, not by source-trace** (the reparent + same-node-stopProp
  tests), and **claim only what the code does this sprint** (forward-declared ≠ consumed — the row-8 fix).

## Reviewer — uptime.browse-review

- **'Permits' is not 'confers' — gate the foundational premise pre-contract** and pin the residual it
  doesn't discharge as the implementing sprint's explicit obligation (the wrapper-governed id-structure;
  007 deferral #4 carried forward).
- **For a destructor, trace every order** (disposed/not × runtime-alive/dead), prove the *crux* not the
  easy half, and **reject a false safety claim even when the contracted path is green**. The resubmit's
  orphan test had to drive the production `~FDomDocument`, not a parallel path.
- **Adjudicate build-don't-defer by verifying the blocking dependency is real** — `IsLayoutBoundary`
  root-only needs 005's resolved CSS dims (verified, not accepted as a phrase) = legit cross-module;
  `GetDocumentCss` empty is an *undefined* design = route the fork to the human, don't let "empty
  forever" settle.
- **An injected seam is sound only if it isolates an INPUT while the code under test stays the
  production path** — confirm the production entry delegates to the same function and the simulated input
  exercises a real production response.
- **Separate the frozen JS/.d.ts surface from the C++ realization** — a signature change that keeps the
  JS surface and doesn't edit a frozen sibling is a realization, not a drift (confirm *both* every time).
- **Close on your own completeness map; "covered" must name a real consumed mechanism** (caught
  `006 consumed` when it's forward-declared-but-unused).

## Inheritance for the next sprint / integration

- **Dispose-contract (integration):** at-runtime document drop/replacement MUST `Dispose`, or the tree
  leaks to the `JS_FreeRuntime` fallback (document churn accumulates) — the analogue of 007 deferral #4.
- **The CSS surface (human-confirmed deferral):** `GetDocumentCss` is empty until the CSS surface
  materializes at Layout & Style integration; whoever lands it also owes the `<style>`/`<script>`
  raw-text escaping exemption (the serializer escapes all text today).
- **Finer layout boundaries (cross-module):** `IsLayoutBoundary` is root-only (safe full-relayout
  baseline); the subtree-relayout optimization needs 005's dims/style resolution (`GetSubtreeHtml` is
  production-unreached until then).
- **The Event-object JS exposure + the whole DOM `.d.ts`:** generated from the C++ interface by the Host
  Bridge's shared generate-from-interface mechanism (the firewall = the deliberately-scoped interface).
- **Validation-pass:** real layout output + the actual cyclic node↔listener *reclaim* (a real
  `JS_RunGC`). The provable halves (markup/structure/observation composition; orphan + gc_mark cycle
  *visibility*) are discharged now.
