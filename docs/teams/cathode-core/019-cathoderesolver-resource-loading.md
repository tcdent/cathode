# Sprint 019 — CathodeResolver: Resource Loading

Build the production **read-mechanism `IBundleSource`** — a UE-asset backend that reads **real file
bytes** via `FFileHelper`, parameterized by a supplied root directory — and prove real asset bytes load
through it and the resolver. The first production backend that resolves a URL to actual file bytes
(today only an in-memory test source exists). The asset-delivery layer of the build-out.

> Scope from the PM (tcdent-approved, run-through cadence): "CathodeResolver — resource loading. Complete
> the production IBundleSource and prove real asset bytes load."

## Scope Boundary

**This sprint owns** the production `IBundleSource` **read mechanism** — a class reading a real file's
bytes via `FFileHelper::LoadFileToArray`, rooted at a **supplied (required) RootDir**, hit/miss per the
`IBundleSource` contract (`bool Read(FString BundlePath, TArray<uint8>& OutBytes) const`, CathodeResolver.h:32)
— and a proof that real asset bytes load through it (a committed fixture → the production backend → its
exact bytes) and through the resolver over it.

**Premise (grounded — the 017/018 lesson):** the resolver is built and fully implemented — `FResourceResolver`
(CathodeResolver.cpp) does the bespoke URL-namespace normalization, **root-agnostic**, over a neutral
`IBundleSource`; `ResolveBytes`/`ResolveText` + the 005 container routing are covered, backed by an
in-memory `FMemoryBundleSource` (CathodeResolverTests.cpp:16). The **genuine 019 gap** (COVERAGE Deferred
#3): the only `IBundleSource` in-tree is the test in-memory one — **no production backend reads real
bytes**. 019 builds it; the resolver is **reused** (`CreateResourceResolver` over the new backend), not
re-proven.

**Out of scope (named, dev+reviewer-grounded):**
- **The production ROOT + its staging (deferred TOGETHER — Deferred #3/#5).** The backend takes a
  **configurable absolute RootDir**; it ships **NO baked production default-root**. The production
  default-root factory (`ProjectContentDir()/Cathode/`) + the project-setting override + the
  `DirectoriesToAlwaysStageAsUFS=/Game/Cathode` staging config + the cooked-pak read validation are ALL
  integration, shipped **together** (don't ship a root the cook can't stage — by shipping NO default
  root, the incoherence cannot arise). This keeps 019 free of a shared `.uproject` change. **Boundary
  check (reviewer, at the slice gate):** the backend has no production default-root baked in — the
  configurable RootDir is the whole contract; the default is the deferred factory.
- **The container wiring** (`SetResourceResolver` into the live litehtml container) — INTEGRATION-DEFERRED
  (the analog of 014/018's layout/render). The container routing is already tested via a stub resolver
  (`Cathode.Layout.Resource.ContainerRouting`); `SetResourceResolver` has no production caller (null =
  the pre-hook clean-miss stub). "Real asset bytes load" = the backend→resolver→bytes **data path**, not
  the end-to-end litehtml consume. No scope fork (dev-grounded).
- **No network I/O, ever**; no resource caching; no JS module loading; no @font-face.

## Problem

The resolver resolves URLs over an `IBundleSource` (hit/miss `url → bytes`), proven only against an
in-memory source. **Nothing reads real bytes in production.** The UE-asset read backend (the
`FFileHelper` read path — the production font loader `CathodeFontRegistry.cpp:43-50` is the precedent:
`FFileHelper::LoadFileToArray(OutBytes, *Path)`, `bool` return, the only "read file bytes" code in
Cathode) is unbuilt. Not `UAssetManager` (COVERAGE:33 — raw `.html/.css/.png` are NOT UObjects → staged
as UFS non-asset files, read via `FFileHelper`; `UTexture2D` drops the encoded bytes on cook).

## Goal

A production `IBundleSource` reads the **real bytes** of an actual fixture file (`FFileHelper`,
supplied RootDir), and those bytes load through the resolver — proven **discriminatingly** (byte-exact
against a distinct non-echoable fixture; a missing file → a clean `Read==false`, empty out, no
crash/garbage).

## In Scope (ordered slices — dev-shaped; collapsible to 1 at the reviewer's call given the small gap)

1. **The production read-mechanism `IBundleSource`** — a class implementing `IBundleSource` via
   `FFileHelper::LoadFileToArray(OutBytes, *(RootDir / BundlePath))`, rooted at a **supplied (required)
   RootDir** (no baked default). *Proof (direct):* the test roots it at the fixtures dir + `Read(<fixture>)`
   → **byte-exact** `OutBytes ==` the committed fixture's distinct content (a stub/no-read → empty/mismatch
   → RED); a **missing** path → `Read==false` + `OutBytes` empty (the clean miss, no crash/garbage).
2. **The resolver over the production backend** (the goal — "real asset bytes load THROUGH the resolver").
   `CreateResourceResolver(productionBackend)` → `ResolveBytes(url, baseUrl)` → the URL-namespace logic
   resolves the URL to the root-relative path → the production `Read` → the real fixture bytes. *Proof:* a
   distinct URL → byte-exact the distinct fixture content **through the production source** (not the
   in-memory); a distinct miss-URL → `ResolveBytes` false.

## Non-Goals

- No network I/O; no caching; no JS module loading; no @font-face.
- No baked production default-root or staging config (deferred together — Deferred #3/#5).
- No container wiring (`SetResourceResolver`) — integration-deferred (escalate if the audit pulls it in).
- No re-proof of the resolver's URL-namespace logic / the in-memory `FMemoryBundleSource` tests (reused).

## Starting Surface (the audit the dev produced)

`IBundleSource Read(path, OutBytes) hit/miss (h:32) → the production FFileHelper read (the FFontRegistry:
43-50 precedent, a supplied RootDir, NO baked default) → the committed distinct TEXT fixture (the
GetBaseDir convention under Source/CathodeResolver/Private/Tests/ — CathodeDomMountProof/Verify/CssCapability
precedents; NOT a plugin Content dir [CanContainContent:false]) → byte-exact + clean-miss → the
resolver-over-production`. COVERAGE.md updated at close (Deferred #3 resolved for the read mechanism;
the root/staging/cooked-validation reaffirmed as the coupled integration item).

## Method

Build the production read backend; prove real bytes with a **real committed fixture file** (distinct,
non-echoable content — e.g. `CATHODE-019-<token>`, NOT derivable from the URL/path) reached via
`IPluginManager...FindPlugin("Cathode")->GetBaseDir()` under `Source/CathodeResolver/Private/Tests/`, read
through the NEW production backend's `FFileHelper` path (a **genuine disk read**, not the in-memory
source). **Byte-exact** `OutBytes ==` the fixture content (not `Contains` — a Contains passes a
partial/over-read). A missing file → a clean `Read==false`. Use a TEXT fixture (no binary-fixture-from-disk
precedent in-tree). Run the FULL CathodeResolver suite (the in-memory logic tests stay green; the new
production-backend test is additive). Coherence (the production TU recompiled + the new test green) + the
authoritative-run discipline on shared builds; the fixture must be on disk at run time.

## Acceptance (reviewer-formulated)

- **Production read backend (genuine disk read):** the UE-asset `IBundleSource` reads a real fixture
  file's bytes via `FFileHelper` (supplied RootDir) — byte-exact `OutBytes ==` the committed fixture's
  distinct content (NOT `Contains`; NOT the in-memory source; not echoable from the URL/path). A
  stub/echo/wrong-file/partial read → RED.
- **Clean miss:** a missing file → `Read==false`, `OutBytes` empty, no crash/garbage/partial.
- **Resolver-over-production:** `CreateResourceResolver(backend)` → `ResolveBytes(url)` → byte-exact the
  fixture bytes through the production source; a miss-URL → false.
- **No baked default-root (the boundary check):** the backend ships no production default-root — the
  RootDir is required/test-supplied (else a shipped-unstaged root re-creates the incoherence). Verified
  at the slice gate.
- **No regression:** the full CathodeResolver suite green — the in-memory `FMemoryBundleSource` /
  `Cathode.Resolver.Resolve.HitMiss` / `Cathode.Layout.Resource.ContainerRouting` tests STAY (additive).
- **Coherence:** the new production-backend test bites a stub backend (no real read → mismatch/miss → RED);
  the production TU recompiled; the fixture on disk at run time; the authoritative run (post-recompile,
  marker green; reject stale/superseded/cross-team; commit the production + test + fixture files).
- **COVERAGE.md updated** at close — Deferred #3 resolved (the read mechanism); the root/staging/cooked
  validation reaffirmed as the coupled integration item.

## Review Questions (resolved at formulation)

1. **Container wiring** — RESOLVED: backend→resolver→bytes in 019; the litehtml container relay
   (`SetResourceResolver`) integration-deferred (no production caller; the 014/018 analog). No scope fork.
2. **The fixture** — RESOLVED: a committed distinct TEXT fixture under `Source/CathodeResolver/Private/Tests/`
   via `GetBaseDir` (the established convention); a genuine `FFileHelper` disk read of the NEW backend.
3. **The production root + staging** — RESOLVED (defer-both-together): no baked default-root in 019; the
   default-root + `DirectoriesToAlwaysStageAsUFS` + the cooked-pak validation ship together at integration
   (Deferred #3/#5). The reviewer's coupling contract honored by shipping neither.
4. **Slice structure** — 2 slices (backend read-mechanism / resolver-over-production); collapsible to 1 at
   the reviewer's call given the small gap.

## Outcome

019 completed the **production `IBundleSource`** (the UE-asset `FFileHelper` backend) and proved **real
asset bytes load through the resolver** over the real file system — the first production backend that
resolves a URL to actual file bytes (previously only the in-memory test source existed).

**Commits (in order):**
- `81cc361` — **production read-mechanism backend (slice 1, production + test + fixture).**
  `FUEFileBundleSource` / `CreateUEFileBundleSource(RootDir)` — a genuine `FFileHelper::LoadFileToArray`
  disk read over a *supplied (required)* RootDir, no baked default-root (the `CathodeFontRegistry`
  `LoadFileToArray` precedent). Proven byte-exact: `Read("probe.txt")` → `OutBytes ==` a hardcoded
  35-byte marker (full-array `==`, non-circular, not Contains); clean miss → `Read==false` + empty.
- `378efb4` — **resolver-over-production end-to-end (slice 2, test-only).** `CreateResourceResolver` over
  the slice-1 backend routes a *non-trivial* URL (`"../probe.txt"` + base `"sub/index.html"`) through
  `ResolveUrlPath` (BaseDir + `..` collapse → `"probe.txt"`) to the production read → byte-exact == the
  marker; a miss-URL → false.
- The close: this doc's Outcome + Postmortem and the updated `CathodeResolver/COVERAGE.md` (Deferred #3
  read-mechanism RESOLVED; the deployment reaffirmed deferred-together; the scaffold-prose coherence note).

**Structural difference (the first production-code sprint):** unlike 016/017/018 (verify-over-real-engine,
test-only commits), 019 *built* production code — slice 1's commit was production + test + fixture + a
test-only-gated `Build.cs` (the Projects dep non-Shipping, so Shipping `CathodeResolver` stays Core-only),
and the discriminator bit a *stub backend* (the production read logic). Slice 2 reverted to test-only
(resolver + backend reused).

**Premise corrected (017/018-style):** the resolver was already built and fully implemented (root-agnostic
URL-namespace logic over a neutral `IBundleSource`, tested via the in-memory `FMemoryBundleSource`); the
genuine gap was only the production backend that reads real bytes.

**Acceptance met:** the production backend reads real bytes (byte-exact, checkout-stable fixture, no baked
default-root), the resolver routes a non-trivial URL through it end-to-end, the deployment (default-root +
staging + cooked validation) is deferred-together; full Cathode gate 113/113; slice-1's `RealBytes` and the
in-memory tests stay green (additive).

## Postmortem

**Lead (uptime.browse-lead).** Two gate moments this sprint were where a weaker process would have passed a
false or stale green, and both held: the **CRLF-on-the-box fixture stability** (slice 1 — the byte-exact
proof rests on the fixture's *checkout* bytes, and the Windows build box with git autocrlf would false-RED
an LF-authored fixture; the fix was a single-line no-newline fixture + a scoped `.gitattributes -text`,
proven on the box at 35 bytes / 0 CR, not assumed), and the **two-same-result-logs sequencing** (slice 2 —
the reviewer distinguishing two 113-green logs by *which build each run postdates*, rejecting the
superseded `/`-rooted run). My job was the staging-config resolution (defer-both-together, settled
docs-consistent so the shared `.uproject` tooling gate never arose), routing the URL sharpening (which the
reviewer grounded from `ResolveUrlPath`), and the per-slice scoped commits in the new production-code
shape. The dev self-superseded the weaker slice-2 green and held for the `"../"` run without being told —
the authoritative-run discipline now internalized in the builder, not just enforced at the gate.

**Dev (uptime.browse-dev, Mundo).**
1. **A byte-exact disk-read proof on a committed fixture is only as stable as the *checkout* bytes — and
   the build box is Windows.** git autocrlf can check out an LF fixture as CRLF → a correct read false-REDs
   (`OutBytes.Num` 36/37 != expected). The fix is checkout-stability: a single-line, no-trailing-newline,
   ASCII fixture (no CRLF/LF/BOM ambiguity) + a scoped `.gitattributes -text` (overriding the repo
   `text=auto eol=lf`), proven on the box (35 bytes, 0 CR). Never weaken to Contains to dodge it (Contains
   masks a partial/over-read — the whole point of byte-exact).
2. **The byte-exact oracle must be a hardcoded constant, not a 2nd read of the same file** (a 2nd read is
   circular — a wrong interpretation matches its own re-read).
3. **019 built production code → the structure differed from 016/017/018's verify-over-real-engine.** The
   commit was production + test + fixture, the fresh-binary marker was the new prod TU recompiled, and the
   discriminator bit a stub backend. Name the build-vs-verify shape so the manifest + coherence are clear.
4. **A new production backend mirrors the existing in-tree precedent + injects its dependency for
   testability.** `FFontRegistry`'s `LoadFileToArray` was the exact template; a configurable injected
   RootDir let the test point at the fixture dir *and* deferred the production default-root.
5. **A deployment config the cook must stage is incoherent to ship half — ship the default-root + staging
   + cooked validation together, or neither.** 019 shipped the read mechanism (configurable RootDir, no
   baked default) and deferred the whole coupling; nothing to stage → the incoherence cannot arise.

**Review (uptime.browse-review).** The **two-same-result-logs sequencing** is the sharpest durable artifact:
when a sharpening crosses a build in flight, two logs can *both* have the marker green and the same
Found-count — the marker NAME is unchanged (the test name), only the source inside strengthened
(`/`-rooted → `"../"`), so "postdates a build + marker green" is vacuous. Gate the run that postdates the
*current* build (191433), not merely *some* build (191134) — the earlier green tested the superseded
source. The **exercise-the-transform** discriminator: a proof that a transform-fn routes a URL *through*
to a backend needs an input that genuinely transforms (URL ≠ resolved path), not an identity or `/`-rooted
near-passthrough (which prove the backend, not the transform). The **CRLF-on-the-box false-RED**: a
byte-exact real-file proof on Windows must defend the fixture's on-disk bytes against autocrlf (a
single-line no-newline fixture + a scoped `.gitattributes -text`, the oracle a hardcoded constant). The
**production-code structural shift**: 019 built production code → the commit was production + test +
fixture + a test-only-gated Build.cs, the discriminator bit a stub backend. Next reviewer inherits: gate
by which build a run postdates (two same-green logs are possible); a transform-routing proof needs a
non-identity input; a byte-exact on-disk proof on Windows needs a CRLF-stable fixture + a hardcoded oracle;
the freshness/commit shape follows build-vs-verify.
