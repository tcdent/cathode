# Vendored Preact (test fixture) — provenance

- **Artifact:** `preact.umd.js` — GENUINE upstream Preact, UMD distribution (the real diff/commit/render path; its
  UMD wrapper self-registers `globalThis.preact` in the bare quickjs runtime).
- **Version:** `10.29.2` — PINNED to panels' Preact target (the version number is the pin reference).
- **Source:** UPSTREAM npm — `https://unpkg.com/preact@10.29.2/dist/preact.umd.js`. Acquired from UPSTREAM, **NOT**
  copied from `panels/node_modules/` — `panels/` is UNTOUCHED (it stays unreachable on the UE-project-only build
  box, and the don't-touch-panels non-goal holds); Cathode is self-contained + build-box-reachable here.
- **sha256:** `08b3e370787c8206405e960a0aad5d605e46f5255346f8e715fbe65b4b8639e3` (byte-unmodified; identical to the
  upstream 10.29.2 distribution).
- **License:** **MIT** — Copyright (c) 2015-present Jason Miller. Full text:
  [`LICENSE-Preact.txt`](LICENSE-Preact.txt), fetched from the same upstream channel as the artifact
  (`https://unpkg.com/preact@10.29.2/LICENSE`). The MIT license permits bundling this test fixture.
  Aggregated in the repo-root [`THIRD_PARTY_NOTICES`](../../../../../../THIRD_PARTY_NOTICES).

## Use (test-only boundary)

Referenced ONLY by `Cathode.Dom.Mount.FirstMountProof` (the first-mount proof). This is TEST-ONLY, DISTINCT from
the production author-JS path: production Preact-loading is the forward LoadAuthorScript / surface-load flow (a
separate pre-bundled IIFE — see `docs/browse/surface-load-document-wiring.md`). This fixture shares the LOAD
mechanism (eval into the runtime, with the bare-runtime env shims), NOT the source, and does NOT pre-decide the
production author-bundling model.
