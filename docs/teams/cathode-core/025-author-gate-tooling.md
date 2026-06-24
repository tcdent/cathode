# Sprint 025 — Author Gate Tooling (the build-time authoring-contract gate)

> **CLOSED (2026-06-24) — all six slices accepted + committed. The three-axis authoring contract is a HARD
> build-time gate, Model-A no-drift-by-construction end-to-end: the `.d.ts` (S1) + CSS allow-list (S2) generated
> FROM the engine, the runtime CSS gate re-pointed to the engine-derived set (S3), the author-side DOM/JS (S4) +
> CSS (S5) gates biting BY-MEMBERSHIP to those generated artifacts, wired end-to-end through a real screen (S6).
> S1 `4447a5f` · S2 `474e8e9` · S3 `4c5a28e` · S4 `e90b6d0` · S5 `4480965` · S6 `3f8f98a`. Forks ruled (tcdent):
> FORK-1 Model A, FORK-2 (a) the self-contained Node toolchain, RQ3 (i) keep the 2 `cathode_computed`, real-screen
> CSS (a) a standalone `.css` per screen. See Outcome + Retrospectives at the foot.**

## Scope Boundary

This sprint builds the **author gate**: the three-axis authoring contract (DOM/JS/CSS subset) gets a HARD build-time
gate that fails the author build, with file+line, on anything outside the supported surface (no `fetch`, no
`localStorage`, no `innerHTML`, no unsupported CSS property) — caught when authoring, not as a broken screen at runtime.
Per FORK-1 (Model A), the contract artifacts are **generated from the engine's own interfaces** so they match by
construction — no hand-maintained parallel list to drift.

**Owns:** the `lib.cathode.d.ts` codegen (from FBoundMember); the `css-allow-list.toml` codegen (from
`ParseLitehtmlPropertyNames`); the retirement of `capability-manifest.toml` + the runtime CSS mirror (replacing the
runtime role with the litehtml-enumerated set); the CSS linter (css-tree); the self-contained author toolchain
(tsc-without-`dom` + esbuild + css-tree); the hard build-time gate (an author-side build step); the stale-doc reconcile.

**Does NOT own (named, with the owner):**
- **The runtime backstop + the dynamic-access developer overlay** — a RUNTIME concern (`el[name]`/`globalThis[x]`); the
  gate doc draws the line at build/author-side (`components-authoring-contract-gate.md:37`). A named residual.
- **`game.*` in the `.d.ts`** — deferred, shape pending the sim (`spec-surface.md:41`). The `.d.ts` is DOM/JS-axis +
  standard globals at 025; `game.*` a named residual.
- **A real authored-screen source tree** — none exists; 025 gates the FORMAT + the toolchain, demonstrated against test
  fixtures (the first real screen is later).

## Problem

The authoring contract (the three-axis subset, `spec-surface.md`) has **no build-time gate** — unsupported usage is
discovered as a broken in-world screen, not at author build. The codegen projection the gate doc claims was "built in
the Host Bridge sprint" is **NOT built**: only an in-process binding TABLE (`FBoundMember`, the Model-A source, with TS
`Signature`s) + a TEST-ONLY JSON enumeration (`EnumerateBoundMembersJson`) exist; `lib.cathode.d.ts` +
`css-allow-list.toml` are 001 skeletons; no generator/driver. The CSS axis is built **Model B** (the authored
`capability-manifest.toml [css]` 108-property FLOOR + the runtime mirror + a coherence-assert) — which is exactly the
parallel-list-asserted-==-engine the gate doc names the anti-pattern; FORK-1 (A) retires it. There is **no Node/tsc
toolchain** on the `game/` side (Cathode is pure C++/UE; panels/observe are the forbidden cross-area couple).

## Goal

> tcdent (FORK-1, Model A): generate the `.d.ts` and CSS allow-list from the engine's own interfaces. **"The manifest
> was the mistake — you don't duplicate a reference to a library you already have."** Retire the manifest + the runtime
> CSS mirror; replace the runtime role with the litehtml-enumerated set. **"The gate-doc was right."**
> tcdent (FORK-2, (a)): **"Cathode does not include a TypeScript interpreter. Projects targeting Cathode must be
> compiled to JavaScript beforehand."** The TS gate is an author-side build step; the resulting IIFE is what Cathode loads.

Finishing: an author builds a screen through the Cathode `Authoring/` toolchain; a banned DOM/JS member or unsupported
CSS property **hard-fails the build at file+line**; a conformant screen compiles, lints, and bundles to the IIFE Cathode
loads — and the `.d.ts`/allow-list provably match the engine because they are generated from it.

## Final State

- **`lib.cathode.d.ts` generated from FBoundMember** — a codegen tool + a build/commit driver project the binding table
  (the TS `Signature`s) to the `.d.ts` (DOM/JS axis + standard globals; `game.*` residual). The on-disk skeleton is
  replaced by the generated artifact; a coherence check binds it to the table by construction.
- **`css-allow-list.toml` generated from `ParseLitehtmlPropertyNames`** — the litehtml parse-name FLOOR (string_id.h
  names minus the 7 cited exclusions); no manifest source.
- **`capability-manifest.toml` + the runtime CSS mirror RETIRED** — `GetCssCapabilityMirror`/`CathodeCssCapability`
  removed; the runtime consumers (element.style validation now; getComputedStyle exposed-props later) read the
  litehtml-enumerated set (`ParseLitehtmlPropertyNames` at runtime) instead.
- **The CSS linter** — css-tree parses authored CSS, validates against the generated allow-list, fails file+line.
- **The author toolchain** — a self-contained `game/Plugins/Cathode/Authoring/` Node package: `tsconfig` omitting `dom`
  from `lib` (the `.d.ts` is the only type surface) + esbuild (bundle, ES2022 pin) + css-tree.
- **The hard build-time gate** — an author-side build step (tsc → the linter → esbuild → the IIFE), hard-fail on banned
  usage; runs dev-machine/CI, not the UE/make build.

## In Scope (ordered slices)

The codegen + retire slices (S1–S3) are **C++/build-step, toolchain-independent**; the toolchain + gate slices (S4–S7)
are the Node author-side. S4–S6 share the toolchain stand-up and may compress.

- **S1 — the `.d.ts` codegen (DOM/JS, Model A).** A codegen tool reads `FBoundMember[]` (the binding table + the TS
  `Signature`s) → emits `lib.cathode.d.ts` (DOM/JS + standard globals; `game.*` residual); + the build/commit driver
  (the descriptor is test-only today — a driver invokes the codegen + writes the file). *Proof:* a coherence test binds
  the generated `.d.ts` to the FBoundMember table BY CONSTRUCTION (every emitted member ⟷ a binding; no hand-list) —
  the no-drift guarantee. Mirrors the (now-retiring) CSS coherence-test shape but Model-A (from the bindings, not a
  manifest).
- **S2 — the `css-allow-list.toml` codegen + the runtime set + RETIRE `capability-manifest.toml` (CSS, Model A).** A
  codegen tool reads `ParseLitehtmlPropertyNames` (string_id.h) minus the 7 cited exclusions **PLUS the 2
  `cathode_computed` props (opacity + letter-spacing — Axis-3 IN, Cathode-OWNED, NOT litehtml parse-names; kept per
  RQ3)** → emits BOTH `css-allow-list.toml` (the build/linter artifact) AND a **build-generated compiled-in C++ set**
  (baked in from string_id.h at BUILD time — for the runtime, since string_id.h is not on disk at runtime, so the
  runtime cannot parse it). Move `ParseLitehtmlPropertyNames` + `CssAuditedExclusions` from the test TU to a
  production/codegen home (the litehtml-set source). Retire `capability-manifest.toml`. *Proof:* the generated set ==
  `ParseLitehtmlPropertyNames` minus the 7 exclusions plus the 2 `cathode_computed` — **the retired mirror's EXACT
  accepted set** (behavior-preserved; the raw parse-names would DROP opacity/letter-spacing AND wrongly accept the 7
  exclusions). The **coherence test is regen-vs-committed** (re-run the projection from the engine + assert the
  COMMITTED artifact == the regenerated one — a litehtml/binding add flows through, a stale commit REDs), NOT committed
  == a hand-written expected list (the retired anti-pattern). The compiled-in set is engine-DERIVED (codegen-emitted),
  the 2 `cathode_computed` a named exception (RQ3), NOT a hand-maintained list asserted == the engine.
- **S3 — RETIRE the runtime CSS mirror + re-point to the build-generated set.** Remove `GetCssCapabilityMirror`/
  `CathodeCssCapability`; re-point the **one production consumer** (`IsDeclaredCssProperty` in the element.style
  accept-and-ignore gate, `CathodeDomNode.cpp:1441`; getComputedStyle exposed-props later) to the **S2
  build-generated compiled-in set** (the S2 set = litehtml −7 +2 `cathode_computed`; NOT `ParseLitehtmlPropertyNames` at
  runtime — it parses a build-time header absent at runtime). The other 3 mirror callers are the coherence TESTS,
  retired WITH the mirror. *Proof:* the runtime property-validation behavior is preserved EXACTLY (same accept/reject —
  the build-generated set IS the mirror's set, so `element.style.opacity` stays accepted and the 7 exclusions stay
  rejected; the raw 115-name parse set would drift both ways), sourced from the build-generated set (no mirror); the
  **two-direction retire check** — no live doc/code/RUNTIME reference to the retired manifest/mirror.
- **S4 — the author toolchain + the DOM/JS gate (FORK-2 (a)).** A self-contained `game/Plugins/Cathode/Authoring/`
  Node package: `package.json` + `tsconfig` (omit `dom` from `lib`; `lib.cathode.d.ts` the type surface) + esbuild +
  css-tree; AND the DOM/JS gate exercised through it (tsc against `lib.cathode.d.ts` without `dom` → a banned member is
  a TYPE ERROR). *Proof — the NEGATIVE half is load-bearing (the toolchain's reason to exist is the bite, not just a
  stand-up):* a NEGATIVE fixture (a screen using `fetch`/`localStorage`/`innerHTML`) hard-fails tsc at **file+line** AND
  a POSITIVE fixture (a real conformant screen) compiles clean + bundles to an IIFE; runs dev-machine/CI (no UE/make
  dependency).
- **S5 — the CSS linter + gate.** css-tree parses authored CSS, validates against `css-allow-list.toml`. *Proof:* a
  NEGATIVE fixture (an unsupported property — grid/`gap`/animation) hard-fails at file+line AND a POSITIVE (conformant
  CSS that **includes opacity + letter-spacing** — the 2 `cathode_computed`) passes clean — so the gate proves the 2
  Cathode-owned contract props still PASS (a positive that were all-litehtml-108 would silently pass while opacity is
  dropped). (css-tree is a distinct mechanism from tsc — its own negative+positive pair.)
- **S6 — the gate wiring + the stale-doc reconcile.** The author-build hook (tsc → linter → esbuild → IIFE) as a
  Cathode-Authoring script/CI target; the end-to-end gate. Reconcile the stale docs: the gate-doc's "Built in Host
  Bridge" claim (false → 025 builds it), `spec-surface.md:69` + `css-epic-contract.md` "stub" (the `[css]` was authored
  013/014), the `capability-manifest.toml` 001-header over-claim (now retired); AND mark the gate-doc's A-principle
  ("the interface IS the bindings") as now **REALIZED/built** — the forward half (not merely deleting the manifest
  refs; the A-principle is now the built state). *Proof:* the end-to-end author-build (a conformant screen →
  compiles+lints+bundles to the IIFE; a banned screen → hard-fails); the docs reconciled to Model A.

## Non-Goals

- The runtime backstop + the dynamic-access overlay (a runtime concern — gate-doc `:37`).
- `game.*` in the `.d.ts` (deferred, pending the sim).
- A real authored-screen source tree (the gate proves the FORMAT against fixtures; the first real screen is later).
- **Do not delete a fallback just because production does not hit it** — the manifest/mirror retirement (S2/S3) is a
  *ruled* teardown (FORK-1 A); trace the runtime consumers (S3) before removing the mirror.

## Starting Surface (audit before editing)

Produce a `piece -> proven/gap -> file:line -> proposed action` inventory before editing. Grounded:
- **PROVEN (Model-A sources):** `FBoundMember` table + TS `Signature`s (`CathodeBoundMember.h:26-36`), DOM tables
  complete (`CathodeDomNode.cpp:1676-1733`, `CathodeDomDocument.cpp:512-523`); `EnumerateBoundMembersJson`
  (`CathodeBoundMember.cpp:61-80`, TEST-ONLY); `ParseLitehtmlPropertyNames` (`CathodeDomCssCapabilityTests.cpp:68-69`,
  the litehtml parse-name enumeration); the 7 cited exclusions (`capability-manifest.toml:49-55`).
- **TO RETIRE (FORK-1 A):** `capability-manifest.toml` (the `[css]` 108-property FLOOR — its DATA is the exclusion list
  to carry into the S2 codegen); the runtime mirror `GetCssCapabilityMirror`/`CathodeCssCapability.cpp:13-133` + its
  runtime consumers (trace before removing).
- **GAP (greenfield):** the codegen tools + the build/commit driver (no generator in-tree); `lib.cathode.d.ts` +
  `css-allow-list.toml` (001 skeletons); the CSS linter; the author toolchain (no `package.json`/`tsconfig`/`node_modules`
  under `game/`); the gate wiring.

## Method

- **Generate from the engine; assert nothing (Model A).** Each artifact is projected from the engine's own interface
  (the bindings / litehtml's parse-names) so it matches by construction — no hand-maintained list, no `manifest ==
  bindings` assert (the retired anti-pattern). The coherence checks are **regen-vs-committed** (re-run the projection
  from the engine + assert the COMMITTED artifact == the regenerated one — a litehtml/binding add flows through, a
  stale commit REDs), NOT committed == a hand-expected list (which re-introduces the parallel list Model A retired). The
  one non-engine-derived exception is the 2 `cathode_computed` props (RQ3) — declared Cathode-owned, named, not a
  hand-list-asserted-==-engine.
- **The negative fixture is the load-bearing half of the gate.** Every gate slice (S5/S6) proves the build HARD-FAILS at
  file+line on banned usage (a negative fixture) AND passes a real conformant screen (a positive fixture). A
  positive-only gate doesn't prove the bite.
- **The retire is UPDATE-not-revert with a two-direction check (S2/S3).** Retiring the manifest/mirror preserves the
  runtime behavior (re-sourced from litehtml) and leaves no live doc/code/runtime reference (grep the retired names).
- **The toolchain is self-contained + author-side.** No UE/make coupling, no panels couple; runs dev-machine/CI (the
  Windows UE box has no Node).

## Acceptance

- **S1:** `lib.cathode.d.ts` generated from FBoundMember (DOM/JS + globals; `game.*` residual); the coherence test binds
  it to the table by construction; the build/commit driver writes the file.
- **S2:** `css-allow-list.toml` generated from `ParseLitehtmlPropertyNames` minus the exclusions; the manifest retired;
  the allow-list == the litehtml-enumerated FLOOR.
- **S3:** the runtime mirror retired; the consumers re-sourced from litehtml (behavior preserved); two-direction retire
  check clean.
- **S4:** the `Authoring/` toolchain stands up; a conformant screen compiles + bundles to an IIFE (dev-machine/CI).
- **S5:** the DOM/JS gate — a banned member hard-fails tsc at file+line (negative) AND a conformant screen compiles
  (positive).
- **S6:** the CSS linter — an unsupported property hard-fails at file+line (negative) AND conformant CSS passes
  (positive).
- **S7:** the end-to-end author-build gate (conformant → IIFE; banned → hard-fail); the stale docs reconciled to Model A.
- **No regression** (the retired CSS runtime behavior preserved; the full Cathode suite green); the `.d.ts`/allow-list
  provably engine-derived (by construction, not asserted).

## Review Questions

1. **FORK-1 — the source-of-truth model — RESOLVED (tcdent): MODEL A.** Generate from the engine (FBoundMember /
   `ParseLitehtmlPropertyNames`); no manifest; retire `capability-manifest.toml` + the runtime mirror, replacing the
   runtime role with the litehtml-enumerated set. The built Model B was the gate-doc's named anti-pattern.
2. **FORK-2 — the toolchain — RESOLVED (tcdent): (a).** A self-contained Cathode `Authoring/` Node toolchain
   (tsc-without-`dom` + esbuild + css-tree); an author-side build step (dev-machine/CI), not the UE/make build.
3. **RQ3 — the 2 `cathode_computed` props (opacity + letter-spacing) — for tcdent (a Model-A sub-fork his ruling did not
   weigh).** These 2 are Axis-3 IN + Cathode-OWNED — litehtml does NOT parse them (confirmed absent from string_id.h) —
   so they CANNOT be "generated from the engine" like the litehtml set. Does Model A **(i)** KEEP them as a NAMED
   2-property exception (the allow-list + runtime set = generated(litehtml −7) + the 2 declared Cathode-owned props —
   NOT a hand-list-asserted-==-engine, just the genuine 2 props beyond litehtml's coverage) **OR (ii)** DROP them (a
   deliberate behavior change — opacity/letter-spacing leave the authoring contract)? **Lean (i)** — preserves the
   contract; the 2 are declared Cathode-owned, not the retired anti-pattern. The draft proceeds with (i) (the
   behavior-preserving default; the fix is a 2-property keep, non-blocking); a (ii) ruling drops the 2.
4. **Team-resolved (no fork):** the slice compression (S4+S5 folded — the toolchain stand-up + the DOM/JS gate in one
   slice, so the bite is in-slice, not a positive-only stand-up); S3 its own slice (1 production consumer +
   `IsDeclaredCssProperty` + the runtime-set mechanism); the `.d.ts` standard-globals shape (console/setTimeout/raf,
   `spec-surface.md:40-41`).

## Outcome

025 delivers the author gate: an author builds a screen through the Cathode `Authoring/` toolchain (`npm run gate`),
and a banned DOM/JS member (`fetch`/`localStorage`/`innerHTML`) or an unsupported CSS property HARD-FAILS the build at
file/property:line — caught at author-build time, not as a broken in-world screen. The contract artifacts are generated
FROM the engine, so they match by construction (Model A — no hand-maintained parallel list to drift).

- **S1 `4447a5f`** — `lib.cathode.d.ts` generated from the FBoundMember binding table (members + TS signatures from the
  table; 3 named + engine-guarded hand-sections for the table-less surface — globals/extends/aux types). The globals
  guard, bound to the install set, caught a real drift (`cancelAnimationFrame`, omitted by the old skeleton); the
  resolve guard caught a dangling `Text`.
- **S2 `474e8e9`** — `css-allow-list.toml` + a baked runtime set generated from litehtml's `string_id.h` (parse-names
  − 7 audited exclusions + 2 `cathode_computed` = 110); `capability-manifest.toml` (the Model-B source) retired. The +2
  and the −7 each carry their OWN engine-subtraction guard (the aggregate coherence can't catch a wrong curation).
- **S3 `4c5a28e`** — the runtime CSS mirror retired; `IsDeclaredCssProperty` re-pointed to the baked engine-derived set
  (case-sensitive scan preserved), behavior proven byte-identical property-by-property (the +2 accepted, the −7
  rejected). The Model-A runtime endpoint: the runtime gate IS litehtml's enumeration.
- **S4 `e90b6d0`** — the self-contained `Authoring/` Node toolchain (tsc-no-dom + esbuild + css-tree) + the DOM/JS gate;
  banned web APIs are tsc errors BY ABSENCE from the generated `.d.ts`. Folded a latent S1 fix (the `.d.ts` `export {};`
  made it a module → the gate inert; the first real consumer caught it).
- **S5 `4480965`** — the CSS linter (css-tree) biting by-membership against `css-allow-list.toml`; the positive fixture
  carries the +2 (proving the `cathode_computed` pass, not just the litehtml 108).
- **S6 `3f8f98a`** — the gate wired end-to-end through a real screen (`screens/hello` passes + IIFEs; `screens/banned`
  hard-fails on both axes); the document-only author-build hook; the stale-doc reconcile.

**Residuals carried forward** (named, not dropped): `game.*` in the `.d.ts` (deferred, shape pending the sim); a CI
workflow (none exists repo-wide — the gate is `npm run gate`, document-only; a human/repo-wide call); the Cathode
README's broader de-scaffolding (the dated-but-true `:100`/`:25` "scaffold" framing — a separate doc pass); a real
authored-screen source tree (025 gates the format + the toolchain against `screens/` examples); the runtime backstop /
dynamic-access overlay (a runtime concern, the gate doc's named out).

## Retrospectives

### Lead retrospective — uptime.browse-lead

**What I tried.** Formed the sprint as a team (the dev grounding the build surface, the reviewer re-deriving the premise
from source), then routed one narrow slice at a time, arbitrating each green against a discriminating gate — never
advancing on a coherence-green that didn't bind the real engine. In-group forks (the fixture shape, the test
disposition, the doc location) I ruled; the genuine future-shaped forks (Model A vs B, the toolchain, RQ3, the
real-screen CSS source) I escalated to the human via the PM, each with a recommended lean and a parameterized,
non-blocking path so the slice kept moving while the answer landed.

**What evidence changed the plan.** The `export {};` blocking-fork (S4) was the first real consumer catching a latent S1
bug its own coherence never ran — I ruled it FOLDED into S4 (the fix with its proof), not escalated as a re-proof.
Cross-team doc entanglement repeatedly forced selective-scoping: the repo-root README + `components-css.md` carried
pre-existing cross-effort WIP, so the scoped commits excluded them (the Cathode-plugin README was the real target), and
the two-direction retire stayed clean in the committed tree without them.

**What I held the line on.** Scope, when the README kept surfacing stale "scaffold" refs — fix the KNOWN-FALSE (the
Quarantine, `:30`), defer the dated-but-true (`:100`/`:25`), so the README reconcile stayed a gate-reconcile not a
multi-sprint overhaul. FORK-2's doc location — the project's no-proactive-docs / prefer-editing-existing directive over
the dev's (reasonable) co-location lean. And I conceded cleanly where wrong: the commandlet driver (CathodeDom is
non-UObject → the cvar-gated test driver).

**What failed (mine, owned).** I told the dev to "pick it up fresh" on a long S2 tail — anthropomorphizing a
context-length signal as human rest, which just left the dev idle and stalled the work. The human flagged it; I
corrected (re-engage; the answer to a long/degrading context is compact-and-continue, never idle — an agent doesn't rest
and return).

**What the next sprint should inherit.** Resolve in-group forks yourself; escalate the genuine convention forks
non-blocking, with a lean. A coherence-green is not acceptance — bind every green to the real engine and prove the USE
through the first real consumer. Hold the commit scope on a shared tree (selective-stage; exclude another team's
uncommitted WIP). The residuals above are named, not dropped.

### Dev retrospective — uptime.browse-dev (Mundo)

**What I tried.** Built the 6 slices as a single Model-A spine: each gate artifact PROJECTED from the engine's own
interfaces (the `.d.ts` from FBoundMember; the css-allow-list from litehtml's `string_id.h`), never hand-authored, with
the gate biting by **absence** from the generated artifact (no hand ban-list). Verification ran split-channel: the
C++/codegen side through env on the Windows box (the coherence + guards in the automation suite), the Node toolchain
side on the dev machine (`npm run gate` — tsc + css-tree + esbuild). The keystone discipline from earlier epics carried:
prove the bite, not just the stand-up; a positive-only gate is half a gate.

**What evidence changed the plan.**
- **An actual tsc run reshaped S4.** The S1 `.d.ts` carried `export {};`, which makes it a *module* — so its globals are
  module-scoped and invisible to author code (with `export {};` tsc rejects even console/document; without it, only the
  banned `fetch` errors). A "regen == committed" coherence proof does **not** prove CONSUMABILITY — S1's coherence/guards
  never ran tsc against the artifact, so the defect survived to S4 (the first real consumer). The fix folded into S4,
  landing WITH its proof.
- **env has no Node** — so the author-gate half verifies dev-side, not on the box. The verification channel follows the
  toolchain, not the sprint.
- **The coherence can't catch a wrong exclusion.** An 8th silently-added CSS exclusion, once regenerated AND committed,
  passes committed==regen. So the −7 (and the +2 cathode_computed) each needed their OWN engine-subtraction guard, bound
  to ParseLitehtmlPropertyNames — not riding the whole-artifact coherence.

**What failed / cost a cycle.** The `export {};` latent bug (coherence-green but unusable). And my first S3 proof rode
purely on `TestEqual`/`TestTrue` assertions, which UE suppresses on PASS — zero `[025-S3]` log lines (env caught it); I
added `AddInfo` evidence lines to match the S1/S2 per-point-in-the-log standard. The proof must be READABLE in the log
on a green, not only inferable from the suite count.

**What the next sprint should inherit.** No CI exists (`.github` absent, repo-wide) — the gate is `npm run gate`
(document-only); automatic CI is a flagged residual. The real-screen CSS source is ruled (a) — a standalone `.css` per
screen; future screens follow it (if `<style>`-in-HTML or CSS-in-`.ts` becomes a shape, the linter needs an extraction
front, the css-tree core unchanged). The README's broader de-scaffolding is deferred. `game.*` is still unspecified
(deferred until the sim's shape is known).

**One line for the team.** A generated artifact that's *coherent* is not yet *correct* — correctness is the first real
consumer using it. Prove the use (the bite, end-to-end through the real path), and guard each curated piece, not just the
aggregate.

### Review retrospective — uptime.browse-review

**What I tried (and would re-use).**
- **Premise re-derivation that stopped the framing cold.** The sprint opened framed as "the `.d.ts` emitter is a small
  add over an existing projection." Grounding the actual tree showed otherwise: the `.d.ts` + `capability-manifest.toml`
  were 001 SKELETONS, no generator existed, and the manifest was the BUILT Model B (a parallel list asserted == the
  engine — the exact anti-pattern the gate doc names). Surfacing that A-vs-B faithfulness contradiction as a design fork
  (→ tcdent → Model A) was worth more than any per-slice check.
- **The circular-guard catch — the load-bearing pattern (S1 + S2).** A "guard" that asserts the codegen's OWN output
  (S1 globals: a hand-list `Dts.Contains(...)`; a +2 guard that just `contains opacity`) is BLIND to engine drift. Bind
  the guard to the ENGINE source instead (the globals to the install set; the +2 to `(generated − ParseLitehtmlPropertyNames)
  == {opacity,letter-spacing}`). Both bindings IMMEDIATELY caught real pre-existing drift the circular form was blind to
  (`cancelAnimationFrame`; a dangling `Text`). No-drift must be BY CONSTRUCTION, never a self-assert.
- **The independent re-run as the verification path for a dev-machine Node gate (S4/S5/S6).** I re-ran `npm run gate` on
  my OWN Node channel — re-deriving the negative bite (TS2304/TS2339; the css property:line; the real-screen end-to-end on
  both axes) and the positive+IIFE empirically, not from relayed output. For a gate whose evidence lives off the box,
  re-running it yourself is the strongest independence available.

**What evidence changed the plan.** The `export {};` blocking bug (the dev's empirical tsc run). The cathode_computed
(opacity/letter-spacing) — Axis-3-IN but NOT litehtml parse-names; a raw-litehtml re-point would silently DROP the +2 AND
re-accept the 7 exclusions — caught pre-build at the draft review, became the +2 guard, the −7's own guard, and the
property-by-property byte-identical (S3). The recurring "where Model A leaks" edge (the Cathode-owned surface beyond the
engine table) — each a NAMED, engine-subtraction-GUARDED exception.

**What failed (mine, owned).** I twice concluded from a high-level signal instead of the actual source (S2 premise): (i)
called `capability-manifest.toml [css]` a "stub" from the file's `001 SKELETON` header, when the `[css]` section was
FULLY AUTHORED; (ii) claimed "CSS can't be Model-A-generated", when `ParseLitehtmlPropertyNames` already enumerated
litehtml's parse-names. Both contradicting signals were IN source. Read the actual SECTION/FUNCTION, not a
skeleton/stale-doc header or an "X isn't possible" assumption. (Both corrected before a verdict; the A-vs-B fork stood.)

**What the next sprint's reviewer should inherit.** A no-drift guard binds to the engine, never to its own codegen
output. Each place the generated surface "leaks" past the engine is a NAMED, individually-engine-guarded exception — and
a second hand-list (the −7) needs its OWN guard. A behavior-preserving re-point proves SET-equality property-by-property,
not a count. For a dev-machine/Node gate, re-run it on your own channel. Case-sensitivity is by-domain (CSS-standard
case-insensitive vs the JS-path CaseSensitive). On a reconcile/retire, fix the KNOWN-FALSE, defer the dated-but-true
(scoped), state the EXACT state re-derived against source, with the two-direction (retired-name) grep its own pass.
