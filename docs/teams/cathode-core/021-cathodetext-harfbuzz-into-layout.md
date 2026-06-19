# Sprint 021 — CathodeText: real HarfBuzz shaping proven

Run the **first real `hb_shape` execution** and prove a string **shapes and measures correctly through the
real shaper** — directly on CathodeText's own API, over a real font. The shaping production path's
correctness gate, retiring the tautology-guard deferral ("no shaping runs this sprint... the vendor shaping
is assumed").

> Scope from the PM (tcdent-approved): originally "HarfBuzz shaping wired into Layout... the measurement
> reaches Layout." **Reshaped by browse-design's layering ruling (3):** Text is a server **leaf** — consumers
> dep Text, not the reverse; `ITextMeasurer` is Layout's injection seam, so a CathodeText→CathodeLayout dep
> would invert it. **The 021 proof runs DIRECTLY on Text's API** — the kerning relation is Text's own
> property, no seam, no Layout, no cross-module dep. The production `ITextMeasurer` adapter (composition glue
> depping both Text + Layout) is built at **integration**, not in CathodeText.

## Scope Boundary

**This sprint owns** the first real `hb_shape` execution and a proof that a string shapes + measures
**correctly through the real shaper**, asserted **directly on CathodeText's API** (`FHarfBuzzShaper.Shape` →
`GetMetrics`/`DeriveMetrics` → the measured width) over a real committed font — via the **kerning relation**
(a tautology-free property of real shaping), retiring the "shaping fidelity unasserted" deferral.

**Premise (the 015–020 discipline — dev grounded + reviewer independently confirmed, citations):** the
shaper + measurement *code* is built but **`hb_shape` has NEVER RUN** — `CathodeTextShapingTests` is
hb_*-free by design (scripted opaque glyphs; "Authored, not run this sprint"; it tests
`BuildShapeFeatures`/`AssembleGlyphs` orchestration, never instantiates `FHarfBuzzShaper`/calls `Shape()`).
`FHarfBuzzShaper.cpp:79` calls real `hb_shape` but its **execution is unproven**. So 021 is the **first real
shaping execution** — substantive, not a re-proof.

**Out of scope (named, ruled):**
- **The `ITextMeasurer` adapter + the Layout wiring** ("the measurement reaches Layout") — **integration,
  NOT 021** (browse-design ruling 3). The adapter is composition glue at the wiring layer (deps both Text +
  Layout); it is built at integration. **No CathodeText→CathodeLayout dep** (test-only or not) — Text stays
  a leaf.
- **Glyph rasterization** (`DrawGlyphRun`), **RTL/bidi**, **production font-deploy + bundling** — separate /
  deferred.
- **No re-proof** of `FHarfBuzzShaper`/`AssembleGlyphs`/`DeriveMetrics` orchestration (built; reused — the
  gap is the real `hb_shape` execution + the kerning fidelity).
- **No hand-assertion of HarfBuzz's exact glyph output** (the tautology guard — HarfBuzz owns shaping
  correctness; assert the kerning *relation*, never the vendor's values).

## Problem

The shaper code is built but `hb_shape` has never executed (the tautology-guard deferral). No test runs the
real shaper over a real font and asserts a shaping property; the registry is empty and there is **no usable
font on the build box** (the repo-root fonts are off-box — build box = the UE project only). The dev's audit
grounds: the real font's byte path (`FFontRegistry::RegisterFontFile` → `FFileHelper::LoadFileToArray` →
`GetFontBytes` → `hb_blob_create`, CathodeHarfBuzzShaper.cpp:46-52); and **that Liberation Sans genuinely
kerns the chosen pair** (verified via HarfBuzz before the proof is locked — a non-kerning font makes
`measure("AV") == A+V`, vacuous).

## Goal

Real `hb_shape` runs over a real committed font (Liberation Sans), and a string shapes + measures correctly
through the real shaper — proven **directly on CathodeText's API**, **tautology-free**: the **kerning
relation** `measure("AV") < measure("A") + measure("V")` (a kerning pair shaped as one run shrinks below the
naive sum — only real shaping, applying the font's kern/GPOS, produces the `<`), with a **non-kerning
control** (`measure(pair) == sum`, bites a hardcode-smaller stub) and the **corroborating trio** (non-zero /
longer→wider / larger-font→wider). The font's kerning of the pair is empirically confirmed (the `hb_shape`
result is the ground).

## In Scope (ordered slices — dev-shaped; may be one given the focused gap)

1. **Commit the real font + run the first `hb_shape` + prove the kerning relation (direct on Text's API).**
   Commit Liberation Sans 2.x (croscore — SIL OFL 1.1) under `CathodeText/Private/Tests/Fixtures/` (GetBaseDir, test-wired)
   with the full license + `PROVENANCE.md` (source/version/sha256/license) and the scoped `*.ttf binary`
   `.gitattributes`; register it (`RegisterFontFile` → bytes → `hb_font`); run the first real `hb_shape`.
   *Proof (direct on CathodeText's API):* `measure("AV") < measure("A") + measure("V")` (the kerning
   relation — real `hb_shape` applied the kern/GPOS) AND a non-kerning control `measure(pair) == sum`; the
   corroborating trio (non-zero / longer→wider / larger-font→wider). **First** verify Liberation Sans kerns
   'AV' via HarfBuzz (else switch the pair, or DejaVu Sans the fallback, and flag). Win64-gated (HarfBuzz
   links Win64-only, CathodeText.Build.cs:30).

## Non-Goals

- No `ITextMeasurer` adapter, no Layout wiring, no CathodeText→CathodeLayout dep (integration; Text stays a
  leaf).
- No glyph rasterization, RTL/bidi, production font-deploy/bundling.
- No re-proof of the scripted orchestration; no hand-assertion of HarfBuzz's exact advances.

## Starting Surface (the audit the dev produces)

`FHarfBuzzShaper.Shape (real hb_shape, CathodeHarfBuzzShaper.cpp:79) → GetMetrics/DeriveMetrics → the
measured width → the kerning relation, direct on CathodeText's API`. Audit targets: `CathodeHarfBuzzShaper.cpp`
(real `hb_shape`; the font byte path :46-52), `CathodeTextMetrics`/`GetMetrics`, `FFontRegistry::RegisterFontFile`,
the kern feature toggle (CathodeShapeFeatures), the committed Liberation Sans fixture (kerns 'AV' — verified),
COVERAGE.md (the "shaping fidelity unasserted" + the measurement-caller deferrals updated; the adapter/Layout
wiring recorded as integration). NO CathodeLayout / `ITextMeasurer` (out of scope).

## Method

Commit the real font (Liberation Sans, binary-stable: the scoped `*.ttf binary` `.gitattributes` — a .ttf
autocrlf-mangle corrupts it; verify it reads intact on the Windows box); register it → `hb_font`; run real
`hb_shape`. Prove **tautology-free, direct on CathodeText's API**: the kerning relation (`<` only from real
shaping) + the non-kerning control (`== sum`) + the corroborating trio; **first** empirically confirm the
font kerns the pair (the `hb_shape` result is the ground — switch pair/font + flag if not). Do NOT
hand-assert HarfBuzz's advances. Run the FULL CathodeText suite green; the existing orchestration tests
stay. Win64-gate the shaping assertions. Coherence + the authoritative-run discipline (the shared-build /
cross-team-red-module case if it applies).

## Acceptance (reviewer-formulated, direct-on-Text)

- **First real `hb_shape`:** real `hb_shape` runs over the real committed Liberation Sans (registered →
  `hb_font`), not the scripted orchestration, not a mock font.
- **The kerning relation (primary, tautology-free):** `measure("AV") < measure("A") + measure("V")` on
  CathodeText's API — only real shaping (kern/GPOS) yields the `<`; a naive/proportional/fixed measurer
  gives `==`/0. **The font's kerning of the pair is empirically confirmed** (else vacuous → switch/flag).
- **The non-kerning control:** `measure(pair) == sum` for a rarely-kerned pair — bites a hardcode-smaller
  stub (the two-pair set: kerning `<` sum AND non-kerning `==` sum).
- **Corroborating:** non-zero / longer→wider / larger-font→wider (a per-glyph-sum measurer passes these but
  fails the kerning `<` — they are corroborating only).
- **No regression:** the full CathodeText suite green; the orchestration tests stay. Win64-gated.
- **Coherence:** the proof's `<` comes from real `hb_shape` (a non-shaping path gives `==` → RED); the
  touched TU recompiled; the authoritative run.
- **Layering:** no CathodeText→CathodeLayout dep (grep-clean); the adapter/Layout wiring is integration.
- **COVERAGE updated** at close — the shaping-fidelity + measurement-caller deferrals (the real shaper ran);
  the adapter/Layout wiring recorded as integration.

## Review Questions (resolved)

1. **The discriminator** — RESOLVED: the kerning relation (primary, tautology-free) + the non-kerning
   control + the corroborating trio; the font must genuinely kern the pair (empirically confirmed).
2. **The font** — RESOLVED (browse-design): Liberation Sans 2.x (croscore — SIL OFL 1.1), committed unmodified + full
   license + `PROVENANCE.md`; verify it kerns 'AV' first; DejaVu Sans the fallback.
3. **`.gitattributes`** — RESOLVED (browse-design): scoped `*.ttf binary` under Fixtures (extend to
   `*.otf`/`*.woff` if added).
4. **The layering** — RESOLVED (browse-design): no CathodeText→CathodeLayout dep; the proof is direct on
   Text's API; the `ITextMeasurer` adapter is integration composition-glue.

## Outcome

021 proved the **first real HarfBuzz `hb_shape` execution**, direct on CathodeText's API — retiring the
shaping-fidelity tautology-guard deferral (the shaper code was built but had never run; the existing tests
use scripted opaque glyphs, orchestration not execution).

**Reshaped by browse-design's layering ruling:** Text is a server **leaf** — no CathodeText→CathodeLayout
dep; the proof is the kerning relation on Text's own API; the `ITextMeasurer` adapter + the "measurement
reaches Layout" wiring are integration composition-glue, deferred. This narrowed 021 from a 2-slice
adapter+Layout plan to a single direct-on-Text slice.

**Commit:** `6b796da` — **the first real `hb_shape` (slice 1 = the sprint).** Real `hb_shape` over a
committed OFL Liberation Sans v2.1.5 fixture genuinely kerns 'AV' (`measure("AV")=60.469 < A+V=64.031`,
kern-delta +3.562) — the tautology-free kerning relation (only real shaping's GPOS/kern yields the `<`;
never a hand-assert of HarfBuzz's exact advances) + an exact non-kerning control (`II == I+I`) + the
corroborating trio. The close: this doc's Outcome + Postmortem and the CathodeText COVERAGE retire.

**Acceptance met:** real `hb_shape` over the sha256-verified OFL bytes (76d04c18, not the superseded GPL
1.07.4), Win64-executed-non-vacuous, leaf-clean (no CathodeText→CathodeLayout dep), the OFL license matching
the bytes, full CathodeText suite 115/115 (Cathode.Text 8→9) with the clean-link + green-tree on the
agency-C2248-red shared target. The `ITextMeasurer` adapter + Layout wiring stay integration.

## Postmortem

**Lead (uptime.browse-lead).** This sprint ran the most threads of the epic — and the disciplines held on
each. The premise audit pinned that `hb_shape` had never run (the first execution, not a re-proof); the
layering ruling (which the reviewer surfaced as a flag and browse-design ratified) *improved* the scope by
keeping Text a leaf and pushing the adapter to integration; the font gate (a content + tooling decision)
went to the design authority and resolved without tcdent; and a **license deviation** — the dev's GPLv2+
font-exception 1.07.4 against browse-design's OFL ruling, which my own relay had inadvertently hedged — was
caught, owned, and re-sourced to OFL v2.1.5 *before* the proof locked, so the work was never built on the
wrong font. At the commit, the scoped-commit verification caught the agency team's WIP already staged in the
index and unstaged it — committing only the six CathodeText files. The lesson I carry: verify the staged
list every time; a shared tree can have another team's work staged under you, and the discipline is the only
thing between that and committing it.

**Dev (uptime.browse-dev, Mundo).**
1. **For a vendor-owned-correctness mechanism, prove it with a relational property, never by hand-asserting
   the vendor's values** — the kerning relation (`measure("AV") < A+V`, only real shaping yields the `<`) +
   an exact non-kerning control; the trio (non-zero/longer/larger) only corroborates (a proportional stub
   fakes it).
2. **When a swap changes identity but not the observable output, the byte-hash proves it — the output
   can't.** The OFL v2.1.5 swap produced identical advances (the Liberation family preserved the metrics);
   the sha256 (76d04c18, byte-different from 1.07.4's f8ace1f8) is what proves the new bytes ran.
3. **A platform-gated assertion must make "skipped" distinguishable from "passed" — AddError the non-gated
   path, never `#if` out to a vacuous green** (the non-Win64 `#else` fails; the [WIN64-EXECUTED] marker +
   real advances prove the gated path ran).
4. **A general note doesn't override a specific design ruling; and a license swap must sweep the source
   comments, not just the license/data files** (the GPL deviation re-sourced to OFL; the stale GPL source
   comment swept at close).
5. **The premise audit (first execution, not a re-proof) + the leaf-layering kept 021 focused.**

**Review (uptime.browse-review).** The two sharpest gates were the **sha256 font-identity** and the
**Win64-executed-non-vacuous**. The sha-identity is the durable shape: when a swap *preserves the
observable* (the OFL v2.1.5 and GPL 1.07.4 have identical kern metrics), the observable cannot prove the
swap — the byte-hash does (computed committed sha == v2.1.5, byte-different from 1.07.4), and "the box ran
it" is corroborated-inference (the binary attribute + the dev's box-sha + the post-swap timing), not a
remote file read. The Win64 gate: a platform/conditionally-gated proof must AddError on the non-gated path,
else a `#if`'d-out vacuous green. The kerning relation is the tautology-free discriminator for vendor-owned
correctness (a relational property + an exact control, never a hand-assert). And the stale-comment-after-a-
license-swap: a license swap needs the source-comment grep, not just the LICENSE file. Next reviewer
inherits: prove artifact-identity by byte-hash when a swap preserves the observable (+ flag box-ran-it as
corroborated-inference); a conditionally-gated proof must AddError the non-gated path; a vendor-owned-
correctness mechanism is proven by a relation + an exact control; a license swap needs the source-comment
grep.
