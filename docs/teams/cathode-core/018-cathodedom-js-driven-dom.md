# Sprint 018 — CathodeDom: JS-Driven DOM

Prove **real panel JavaScript mutates the retained `FDomNode` tree** through the live Binding (017) and
real `Evaluate` (016), verified by a **C++-DIRECT** read of the tree (the source of truth) — not a
selector-service-mediated read-back. `document.createElement`, `parent.appendChild`, and `el.style`
writes from real JS reach and change the C++ tree.

> Scope from the PM (tcdent-approved, run-through cadence): "CathodeDom — JS-driven DOM. Real panel JS
> mutates the FDomNode tree via the live Binding (017) and Evaluate (016). Prove: real JS DOM ops
> (createElement, appendChild, style writes) mutate the retained tree."

## Scope Boundary

**This sprint owns** proving real-JS DOM mutation reaches the retained `FDomNode` tree, verified
**C++-direct**: real `Evaluate` runs panel JS calling the bound DOM ops, and a C++-direct read of the
`FDomNode` (`NodeFromWrapper` / `FindNode` → `GetTagName` / `GetChildNodes` / `GetAttribute`) shows the
mutation.

**Premise (grounded per-op — the 017 lesson; all 3 ops are ALREADY real-JS-driven):**
- **createElement** — real-JS-driven (DocumentTests:99), but only **JS-read-back** verified (`e.tagName`
  JS-side); the C++ `GetTagName` checks exist only on *C++-minted* nodes, never a **JS-minted** one. Gap.
- **appendChild** — real-JS-driven AND its **C++-direct** retained-tree verify **already exists**
  (EventTests:434-436: real-JS `appendChild` then `NativeChildIds(Parent).Contains(Child->GetNodeId())`
  = `FDomNode::GetChildNodes`; insertBefore mirror asserts C++ order). 018 **reuses** this; the new part
  is a **JS-minted** node landing in the C++ parent's child list.
- **el.style write** — a **bound real-JS setter** (the 014 live write-through `CSSStyleDeclaration`
  exotic over the canonical `style` attribute, CathodeDomNode.cpp:1351; real-JS-driven StyleTests:86),
  but only **JS-read-back** verified (`getAttribute('style')` JS-side); the C++ node's stored style after
  a JS write is **unverified C++-side**. Gap.

**The genuine gap:** the **C++-DIRECT** verification of (i) the JS-MINTED node's identity (`NodeFromWrapper
(JS-created)->GetTagName`) + its parenting, and (ii) the JS-WRITTEN style (`->GetAttribute("style")`).

**The Model-A direct-vs-indirect precision (reviewer).** In Model A, `FDomNode.Children` ARE the owning
JS-wrapper refs (CathodeDom.h:227) — there is **no separate JS shadow**, and `querySelector` reads the
**same** C++ tree *through the selector service*. So a `querySelector('#g')===g` read-back is **not
vacuous** (a no-op fails it too) — but it is **INDIRECT** (the selector service is a confound: a selector
bug could mask, a stale cache could false-positive). The source-of-truth proof reads the `FDomNode`
**DIRECTLY**. The retained-tree claim's **PRIMARY reject** = a `querySelector` / JS-side-identity-only
assertion (indirect), not the C++-direct read.

**Out of scope (named):**
- **Layout/render of the mutation** — the Dom→Layout→render path is integration-deferred (the 014
  finding); 018 proves the *tree mutation*, not re-layout/render.
- **GC reclamation re-proof** (CathodeBinding's tracked follow-on); **no new DOM surface** beyond
  create/append/style (DocumentFragment/classList stay COVERAGE-deferred).

## Goal

Real panel JS (via `Evaluate` + the bound DOM ops) mutates the retained `FDomNode` tree — a JS-minted
element with the right tag, parented in order, and a JS-written style — each verified **C++-direct** on
the `FDomNode`, proven discriminatingly (a unique tag / exact order / distinct style binds the JS op,
biting a no-op/wrong-tag/wrong-order/wrong-store).

## In Scope (ordered slices — dev-shaped to the gap)

1. **create + append → the C++ retained tree (C++-direct).** Real JS
   `globalThis.__n = document.createElement('cathode-probe-XYZ'); __root.appendChild(__n)` (a **UNIQUE**
   tag, not a generic `div` that could pre-exist) → C++ recovers `__n` via the global channel
   (`JS_GetPropertyStr`, 017's transit) → `NodeFromWrapper(__n)->GetTagName == "cathode-probe-XYZ"` (the
   JS-minted node's C++ identity, NEW) AND the parent `FDomNode`'s `GetChildNodes` includes `__n`'s
   NodeId **IN ORDER** (append two distinct children → the C++ child-id list is `[a,b]`, not `[b,a]` or
   missing — order bites a link-but-mis-order). This reuses EventTests:436's C++-direct read **helper**
   (`GetChildNodes`), with a STRONGER assertion — the **exact ordered sequence** (not the existing
   `Contains` membership) on a **JS-minted** node (the existing 436 is `Contains` on a *C++-minted*
   node) — so "reuse" must not collapse to the membership check.
2. **el.style write → the C++ retained node (C++-direct).** Real JS `__el.style.color = 'red'` (a
   DISTINCT value) → `NodeFromWrapper(__el)->GetAttribute("style")` (C++-side, the canonical attribute)
   CONTAINS `'red'` (the C++-stored inline style after a real-JS write, NEW) — not the JS `getAttribute`
   read-back. (Audit confirms the 014 write-through stores on the `FDomNode`'s `style` attribute.)

## Non-Goals

- No layout/render of the mutation; no GC reclamation re-proof; no new DOM surface.
- No re-proof of appendChild's existing C++-direct verify (EventTests:436) — reuse it; the new part is
  the JS-minted node.
- No `querySelector`/JS-side-identity-only assertion for the retained-tree claim (indirect — the reject).
- Do not delete the existing DOM tests (C++-driven node / selector / Preact mount-proof / forms — the
  additive guards stay green).

## Starting Surface (the audit the dev produced)

`op → real-JS-driven? → C++-DIRECT tree-verified, or only querySelector/JS-read-back? → the gap → proof`
(grounded: create + style = gap; append's C++-direct verify exists, reused). The C++-direct read surface:
`Doc->NodeFromWrapper(FJsValue)` (CathodeDom.h:281), `Doc->FindNode(NodeId)` (:280),
`FDomNode::GetTagName/GetChildNodes/GetParentNode/GetAttribute`. The wrapper→node bridge for a JS-created
node = `NodeFromWrapper(the recovered global)`. COVERAGE.md updated at close (the JS-driven-DOM status).

## Method

Real-engine tests over `CreateJsRuntime()` + `Evaluate` (016) + the bound DOM classes (017). Verify the
mutation on the **C++-DIRECT** `FDomNode` read (the source of truth — never only `querySelector`). The
discriminator is the exact tree-structure-change the JS op set, read C++-direct: a UNIQUE tag (not a
pre-existing generic), the right parent + ORDER, a DISTINCT style value — not echoable, biting a no-op.
The 017 multi-input discipline ([[feedback_multi_factor_proof_vary_every_input]]) applies if a value is
computed. Run the FULL CathodeDom suite (the existing guards stay green). Coherence + the
authoritative-run discipline on shared builds ([[reference_authoritative_run_shared_build]]).

## Acceptance (reviewer-formulated)

- **create + append (C++-direct):** real JS mints a UNIQUE-tagged element + appends; `NodeFromWrapper
  (__n)->GetTagName` == the unique tag AND the parent's `GetChildNodes` includes `__n`'s NodeId **in
  order** (two distinct children → ordered list). PRIMARY reject: a `querySelector`/JS-identity-only
  assertion (indirect); a generic/pre-existing tag (echoable); an append without the ORDER check.
- **style write (C++-direct):** real JS `el.style.<prop>=val` (distinct) → the C++ `FDomNode`'s
  `GetAttribute("style")` (C++-side, not JS `getAttribute`) CONTAINS it. Reject a JS-side read-back.
- **Discriminating:** the C++-direct tree state binds the JS op (unique tag / exact order / distinct
  value); a no-op/wrong-tag/wrong-parent/wrong-order/wrong-store FAILS; not echoable.
- **No regression:** the full CathodeDom suite green — the C++-driven node tests + the selector
  (querySelector) tests + the Preact mount-proof + forms STAY (additive). Gate BOTH.
- **Coherence:** the new real-JS-tree-mutation test bites a broken binding/op (stub → no C++ tree change
  → RED; additive marker); the AUTHORITATIVE run (post-recompile, marker-green; reject stale/superseded/
  cross-team-shared); commit only the browse files.
- **COVERAGE.md updated** at close — the JS-driven-DOM status (C++-direct verified for create/append/
  style); the layout/render boundary reaffirmed.

## Review Questions (open forks for the team + PM)

1. **The genuine gap per op** — RESOLVED: create + style need the C++-direct verify (the gap); appendChild's
   C++-direct verify exists (EventTests:436, reused; the new part is the JS-minted node). The slices prove
   the gap, not a re-proof.
2. **C++-direct vs indirect (Model-A precision)** — RESOLVED: assert C++-direct on the `FDomNode` (source
   of truth), not `querySelector` (selector-service-mediated, indirect — the primary reject). There is no
   JS shadow in Model A; the gap is direct-vs-indirect, not tree-vs-shadow.
3. **el.style exposure** — RESOLVED: a bound real-JS write-through to the canonical `style` attribute
   (014); the gap is the C++-direct `GetAttribute("style")` read (audit confirms it stores on the
   `FDomNode` attribute).
4. **Scope vs layout** — the mutation is proven; the re-layout/render is integration-deferred (no render
   in 018).

## Outcome

018 proved **real panel JavaScript mutates the retained `FDomNode` tree** — `createElement`,
`appendChild`, and `el.style` writes from real `Evaluate` (016) through the live Binding (017) reach and
change the C++ tree, each verified **C++-direct** (the source of truth, not a selector-service-mediated
read-back).

**Commits (in order):**
- `c0f8d7b` — **create + append (slice 1).** Real JS mints two unique-tagged elements
  (`document.createElement` in the eval source) and appends them; C++ recovers them (017's transit) and
  asserts C++-direct: `NodeFromWrapper(__a)->GetTagName == "cathode-probe-a"` (the JS-minted node's C++
  identity) and the fresh-empty parent's `GetChildNodes == [__a, __b]` (exact order, stronger than the
  existing Contains-on-a-C++-minted-node).
- `abde6bf` — **el.style write (slice 2).** Real JS writes `el.style.color='red'` + `width='42px'` (the
  014 write-through setter, in the eval source); C++ asserts C++-direct: `NodeFromWrapper(__el)->
  GetAttribute("style")` Contains the bare value tokens (`"42px"`, `"red"` — the compact non-normalized
  inline form, not the rgb/getComputedStyle read-path).
- The close: this doc's Outcome + Postmortem and the updated `CathodeDom/COVERAGE.md` (the JS-driven-DOM
  engine-verified status; the layout/render boundary reaffirmed as deferral #8).

**Premise corrected (017-style, dev-grounded per-op):** all three ops were *already* real-JS-driven, and
appendChild's C++-direct retained-tree verify *already existed* (EventTests:436). The genuine 018 gap was
narrower — the C++-direct verify of the **JS-minted node** (create) and the **JS-written style**
(el.style) — not a re-proof.

**Acceptance met:** real JS mutates the retained tree, verified C++-direct, discriminatingly (unique tag /
exact order on a fresh parent / distinct style tokens); full Cathode gate 111/111; the existing
C++-driven node / selector / Preact mount-proof / forms tests stay green (additive). The
authoritative-run discipline held on env's shared cross-team builds (the player TUs a separate module).

## Postmortem

**Lead (uptime.browse-lead).** This sprint ran the cleanest of the epic, and the reason is that the
disciplines built earlier had become the team's defaults. My first-cut carried the right instinct (the
017 premise lesson — flag that most of the work may already be done) but two framings the team
corrected: "C++ tree vs JS shadow" (the reviewer's Model-A precision — there *is* no shadow; the gap is
direct-vs-indirect, querySelector being a selector-service confound), and the implicit assumption that
the proof was novel (the dev's per-op audit found appendChild already C++-verified). My job was to route
the grounding (premise→dev, acceptance→reviewer), fold the precisions (the masquerade guard, the fresh
parent, the serialization token), and hold the authoritative-run + single-file-commit discipline on the
shared builds — not to be right about the proof shape. The dev satisfied the reviewer's precisions
*in-flight* twice (the masquerade guard, the serialization grounding) before they were flagged: the sign
the loop's evidence discipline is now load-bearing in the builders, not just the gate.

**Dev (uptime.browse-dev, Mundo).**
1. **The per-op premise correction narrowed 018 to the genuine 2-op gap** — a 3-way per-op audit
   (real-JS-driven? / C++-direct-verified?) found all three already real-JS-driven and appendChild's
   C++-direct verify already existing (EventTests:436); the gap was only the JS-minted node (create) +
   the JS-written style. Ground the state, build the gap.
2. **C++-direct vs indirect is the source-of-truth read** — assert the FDomNode directly
   (`NodeFromWrapper->GetTagName/GetChildNodes/GetAttribute`), never querySelector/getComputedStyle (in
   Model A there's no shadow; querySelector reads the same tree but indirectly — a confound, the reject).
3. **The masquerade guard — prove the op is JS-driven by putting it IN the Evaluate source**; a test-side
   C++ `Doc->CreateElement`/`SetAttribute` is the other direction (the existing coverage), not the gap.
4. **An exact-order whole-list assertion needs a fresh parent** (`GetChildNodes == [a,b]` is genuine only
   if the children are exactly the appends).
5. **Ground the exact stored form before choosing the substring** — the inline `style` attr is compact +
   non-normalized (`color:red`, not `rgb(...)`); Contains a stable value token (`"42px"` zero-normalization
   safest), never the rgb/computed form (false-RED) nor a space-separated `prop: value`.

**Review (uptime.browse-review).** The Model-A direct-vs-indirect precision is the durable artifact: for a
"retained C++ structure mutated" claim, assert *directly* off the source-of-truth `FDomNode`, never
through a mediating service. It corrected "C++ tree vs JS shadow" — in Model A `FDomNode.Children` ARE the
owning wrapper refs (no shadow), so querySelector reads the *same* tree but indirectly (selector-service
-mediated, a confound that can mask a tree bug or false-positive off a stale cache); the indirect read is
the primary reject. The masquerade guard draws the line between the gap and a re-proof — *where* the op
runs (eval source = the gap; host-side C++ = the existing direction). Two grounding precisions kept the
assertions honest: an exact-order whole-list `==` needs a fresh parent, and Contains needs the grounded
stored serialization (compact-inline vs normalized-computed). The authoritative-run log-discipline held a
third sprint running. Next reviewer inherits: assert direct off the source-of-truth object; the
masquerade guard is *where* the op runs; a structural-equality assertion needs a fresh container; and
ground the stored serialization before choosing the Contains token.
