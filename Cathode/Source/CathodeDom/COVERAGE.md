# Cathode DOM — component coverage & accountable deferrals

Durable accountability record for the DOM (sprint 008, no-build/no-run scaffold). It maps every
responsibility in [`docs/browse/components-dom.md`](../../../../../docs/browse/components-dom.md) to its
status: **COVERED** (slice + mechanism + proof test) or **DEFERRED / N-A-by-design / DESIGN-FORK** (with
reason · dependency · unblocker · first-execution risk). Nothing in the component doc is silently missing.

This file is the accountability of record. Every in-tree reference (file / type / test) resolves.

The DOM is the largest component and the first to **compose three built siblings** — 007
[`CathodeBinding`](../CathodeBinding/COVERAGE.md) (node-class binding + lifetime), 005
[`CathodeLayout`](../CathodeLayout/COVERAGE.md) (layout via markup), 006
[`CathodeSurfaces`](../CathodeSurfaces/COVERAGE.md) (the surface's document). It wraps **no new vendor**; it
is quickjs-type-coupled **by design** via 007's public API (the accepted "expose the spec" inheritance).

## Model A — the lifetime model (a recorded human decision + a doc-premise override)

Node lifetime = the **JS wrapper's refcount**; the source of truth is the **JS wrapper graph**, rooted at the
document; there is **no independent C++ owning root** (007's ruling, carried forward as deferral #4 — 005
*permits* Model A but does not *confer* it; keeping the id-addressable structure wrapper-governed was this
sprint's obligation, discharged). Parent→child links and listeners are **JS refs** (`FJsValue`,
`gc_mark`-marked); the `NodeId`→node index is **non-owning**.

**Doc-premise override (recorded, as 007 did):** the component doc's key-concern says "the DOM owns the
*trigger points* for teardown (`removeChild`/`removeEventListener`); the bridge owns the mechanism." Model A
**supersedes** that premise — `removeChild`/`removeEventListener` are **reference-drops** (drop the held
child/listener JS ref); 007's `gc_mark` + finalizer reclaim. There are no explicit-free teardown triggers.

**Model-A invariant refinement (sprint 012 S2, browse-design-ruled — a restatement of the SAME T4 guarantee,
not a change to it):** *no **OWNING** self-reference, but a **non-owning reverse-lookup borrow** is permitted.*
The node-member bindings need an identity-preserving node marshaler (`child.parentNode === parent`, one wrapper
per node), which requires resolving a node id → its existing wrapper — complete even for a **detached** subtree
root, whose wrapper is held only by JS. `FDomNode::WrapperBorrow` is that reverse-lookup: a `JSValue` stored
**without** `JS_DupValue` (non-owning) at the wrapper-mint, so it **adds no refcount** (no self-root / Model-B
leak), is **not `gc_mark`-marked** and **not `FreeRT`'d**. It cannot dangle: the wrapper **owns** the node (the
node is the wrapper's opaque; the finalizer destroys it), so the node never outlives its wrapper, and
`GetWrapperForNode` only reads the borrow for a node still in `NodeIndex` (the finalizer de-registers) — never a
freed wrapper. `GetWrapperForNode` `JS_DupValue`s it to hand JS a **new owning** ref to the **same** object
(identity `===` holds). The 1:1 wrapper↔node invariant holds: the borrow is set exactly once, at each of the two
and only wrapper-mint sites (`CreateElement`, `CreateTextNode`); there is **no re-wrap path**.

## Covered

| Responsibility | Slice | Mechanism | Proof |
|---|---|---|---|
| **Retained node tree** (JS wrapper graph, Model A; persists between ticks) | S2 | `FDomNode` registered as a quickjs class via 007 (`RegisterDomNodeClass`: class id + opaque + `gc_mark` marking held refs + `FinalizeNativeObject` finalizer); parent→child links are `FJsValue` child wrappers; non-owning `NodeIndex`; the document owns the root | `…Node.RegistrationAndTree`, `…Node.FinalizerDeregistersAndCascades`, `…Document.DisposeReclaimsTree`, `…Document.OrphanSurvivorOnTeardown` |
| **Bounded structural API** (create/insert/remove; tree reads) + reference-drops | S2 + S3 | `createElement`/`createTextNode` (owning wrapper), `appendChild`/`insertBefore`/`removeChild` (JS-ref link/drop), `parentNode`/`childNodes`/`firstChild`/`nextSibling`/`tagName` | `…Structure.AppendInsertRemove`, `…Structure.Reparent` |
| **Attribute/property access** (incl. `value`, bulk `textContent`) + 007 marshaling | S4 | `getAttribute`/`setAttribute`/`removeAttribute` over a string `TMap`; `value` (get); `textContent` get/set (set = reference-drop replace); `MarshalAttributeValue` composes 007's `ClassifyJsValue`+`Unmarshal` (scalar vocabulary; rejects exotic) | `…Attribute.SetGetRemove`, `…Attribute.TextContent`, `…Attribute.MarshalValue` |
| **Event system** (`addEventListener`/`removeEventListener`; `dispatchEvent` with bubbling) | S6 | listeners held as `FJsValue` (gc_mark-marked — the node↔listener cycle; identity-matchable); removal = reference-drop; dispatch = AtTarget→bubble, `stopPropagation` halts ascent, `preventDefault` flag | `…Event.ListenerHoldMarkAndDrop`, `…Event.DispatchBubbleOrder`, `…Event.StopPropagationAndPreventDefault`, `…Event.StopPropagationSameNodeRemaining` |
| **Layout handshake** (implements 005 `ILayoutDocumentSource`; drives `ILayoutEngine`) | S5 | structure reads from the non-owning index; **markup serialization** `GetDocumentHtml`/`GetSubtreeHtml` (escaped, sorted-key attrs); `SetDocument` + `Relayout`; the DOM serializes + drives, litehtml rebuilds transient | `…Layout.MarkupSerialization`, `…Layout.StructureReads` |
| **Dirty-mark on mutation** | S3 + S4 | each mutating call → `MarkNodeDirty` (Structure for tree/textContent; Layout for attributes) so 005 relayouts only the affected region | `…Structure.DirtyMarkOnMutation` (+ asserted in the attribute tests) |
| **Observation reads** (`getBoundingClientRect`/`getComputedStyle`, **off the author surface**) | S5 | compose 005's `FLayoutResult.Geometry` by NodeId; not in the `.d.ts`, no flag-gate | `…Observation.ReadsComposeGeometry` |
| **Delegations** (layout→005; lifetime mechanism→007; surface→006) | by design | GENUINELY CONSUMED this sprint: 005 (implements `ILayoutDocumentSource`, drives `ILayoutEngine`) + 007 (node binding/lifetime/marshaling). 006 is FORWARD-DECLARED only — the `CathodeSurfaces` Build.cs dep is contract-intended forward wiring; the DOM does NOT include `CathodeSurfaces.h` or use `ISurfaceDocument` (the surface↔DOM binding is INTEGRATION-DEFERRED). 005/006/007 consumed/depended **not edited** (byte-identical); no new vendor | — |

## Deferred / N-A-by-design / design-forks — with accountability

### 1. Dispose-is-the-runtime-release — an INTEGRATION obligation
- **Reason:** the document owns the root; `Dispose()` drops it (runtime alive) so the tree reclaims by GC. Dropping a document at runtime **without** `Dispose()` abandons its tree until the `JS_FreeRuntime` fallback — document churn (replacement at runtime) accumulates abandoned trees.
- **Dependency / unblocker:** the integration sprint (the surface/owner must `Dispose()` on at-runtime document drop/replacement). The analogue of 007 deferral #4.
- **First-execution risk:** a renderer that swaps documents per surface without `Dispose()` grows memory until shutdown.

### 2. Cyclic node↔listener survivor — provable half DISCHARGED; reclaim = validation-pass
- **Reason:** the orphan mechanism (S2) and the cycle's `gc_mark` VISIBILITY (S6 — `MarkHeldRefs` visits listeners) are proven now. The **actual** cyclic reclaim by a real `JS_RunGC` is GC-timed.
- **Dependency / unblocker:** an execution-sprint build + a running engine (the validation pass).
- **First-execution risk:** none on our side — the cycle is GC-visible; reclamation timing is the engine's, proven at execution.

### 3. `insertBefore` unknown-reference is lenient (appends, not `NotFoundError`)
- **Reason:** a deliberate spec-divergence acceptable for **trusted-only** bundled author code. (`insertBefore(node, null)`→append IS spec-compliant — not the divergence.)
- **Dependency / unblocker:** spec-exact throw needs the JS exception path (future hardening).
- **First-execution risk:** a buggy author passing a non-child reference gets an append instead of a throw — silent, but trusted-only.

### 4. `setAttribute` marks unconditionally (optional skip-if-unchanged)
- **Reason:** `setAttribute` re-marks even on an unchanged value (harmless over-mark); `removeAttribute`'s mark is guarded.
- **Dependency / unblocker:** an optional future refinement (compare-before-mark).
- **First-execution risk:** a redundant relayout request on a no-op set; correctness unaffected.

### 5. `IsLayoutBoundary` root-only → `GetSubtreeHtml` production-unreached for now
- **Reason:** only the root is reported a boundary (conservative — over-claiming would relayout too small a subtree); every dirty therefore walks to the root → full relayout (the safe baseline), so the subtree path is not yet exercised.
- **Dependency / unblocker:** finer boundary detection needs 005's style/dims resolution (explicit dims / replaced elements) — cross-module.
- **First-execution risk:** no incorrectness — only a missed subtree-relayout optimization until boundaries are refined.

### 6. `GetDocumentCss` empty — CSS sourcing (HUMAN-CONFIRMED deferral)
- **Reason:** the DOM has no CSS surface yet; `GetDocumentCss` returns empty. The human resolved this: defer until the CSS surface materializes — verbatim *"if we dont have a css surface yet it will materialize in due time."* Not a pending fork.
- **Dependency / unblocker:** the future CSS surface, at Layout & Style integration; empty-baseline until then.
- **First-execution risk:** documents lay out unstyled until the CSS surface lands.

### 7. `<style>`/`<script>` raw-text escaping trap
- **Reason:** the serializer HTML-escapes all text; raw-text elements (`<style>`/`<script>`) must NOT be escaped (their content is CSS/JS, not HTML text).
- **Dependency / unblocker:** lands with CSS sourcing (#6) — a raw-text exemption in the serializer when those elements become real.
- **First-execution risk:** if a `<style>` ever carried serialized escaped text, the CSS would be corrupted — not reachable until #6.

### 8. Real layout output — validation-pass
- **Reason:** the DOM serializes + drives; actual litehtml layout (boxes/positions) is 005's and runs only with a build.
- **Dependency / unblocker:** the execution/validation pass.
- **First-execution risk:** geometry/observation values are only real once laid out; our composition + serialization are tested, the layout itself is 005's.

### 9. Event-object JS exposure — generate-from-interface deferral
- **Reason:** the dispatch ALGORITHM is built/tested; the `FDomEvent`→JS Event object marshaling (its JS properties + `stopPropagation`/`preventDefault` methods) follows the generate-from-interface deferral, so a JS listener cannot yet receive a real event or call those methods (tests simulate via the recording invoker).
- **Dependency / unblocker:** the Host Bridge's shared generate-from-interface mechanism (#12).
- **First-execution risk:** listeners fire but receive no event object until the exposure lands.

### 10. Capture-phase dispatch — N/A by design (per the freeze)
- **Reason:** the frozen surface is bubble-phase only; capture is deferred.
- **Unblocker:** a future surface widening if real screens need delegation-via-capture.
- **First-execution risk:** none — capture is not advertised.

### 11. Signature refinements as Model-A realizations (recorded, not gaps)
- The S1 sketch's `createElement`→`FDomNode&` became `→FJsValue` (the owning wrapper = the lifetime handle); structural ops + `addEventListener` traffic in `FJsValue` wrappers/functions — because a node must never hold its own wrapper (self-root = Model B) and listeners must be directly gc_mark-markable/identity-matchable (007's `FCallableHandle` hides its function and 007 is frozen). Adjudicated sound; surface unchanged.

### 12. The downstream `.d.ts` (Authoring Contract & Gate)
- **Reason:** the DOM's frozen surface FEEDS the `.d.ts`, generated from the C++ interface by the Host Bridge's shared mechanism — not built here (the human's "no generate-from-interface mechanism" note; 007 Option-B).
- **Dependency / unblocker:** the Host Bridge + the generate-from-interface mechanism.
- **First-execution risk:** authors have no typed surface until the `.d.ts` is generated; the frozen interface is the source.

## The frozen surface (the firewall — the `.d.ts` source; do not widen)

IN: minimal `Event` + `KeyboardEvent{key,code}`; `value` (get); bulk `textContent` (get/set); the bounded
structural API + tree reads; bubble-phase dispatch. DEFERRED (absent): `checked`, `DocumentFragment`,
`classList`; pointer/mouse `Event` fields; capture-phase. Observation reads are off-contract (not in the
`.d.ts`).
