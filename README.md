# Cathode

> **PLACEHOLDER — Sprint 033 Slice 1.** This file is a stub establishing the repository
> pattern. A proper README is authored in a later polish sprint; the repository is not yet
> populated and **nothing here builds yet.**

Cathode is an Unreal Engine 5.7 C++ plugin that runs first-party HTML/CSS/JS "screens" and
renders them as textures onto in-world devices. It is assembled from four small, permissively
licensed libraries (quickjs-ng, litehtml, HarfBuzz, Blend2D — plus Blend2D's build dependency
AsmJit) and authors no HTML engine, JavaScript runtime, or CSS parser of its own. It is a
rendering surface for bundled, trusted screens — **not a web browser** (no networking, TLS,
cookies, storage, or history).

## Status

This repository is being stood up by extracting Cathode from the host project it was developed
in. **Current state: repository skeleton only.** The plugin source has not been relocated yet.

Deliberately not yet true — each is tracked as a deferred sprint item:

- **It does not build.** The vendored third-party libraries have no in-repo build orchestration.
- **It does not run its own tests.** A standalone test harness is designed but not implemented.
- **It provides no sample content.** Screen material, bundles, and fonts are host-provided today.

## Layout

```
Cathode/            the UE plugin (drops into a project's Plugins/ directory)
docs/               design-of-record and sprint history
.github/workflows/  CI
LICENSE             MIT
```

## Docs

The active sprint contract is
[`docs/teams/cathode-core/033-cathode-standalone-relocation.md`](docs/teams/cathode-core/033-cathode-standalone-relocation.md).

## License

MIT — see [`LICENSE`](LICENSE), copyright (c) 2026 Travis Dent. Third-party attribution for the
four vendored libraries, Blend2D's AsmJit build dependency, and the two committed test fixtures is
in [`THIRD_PARTY_NOTICES`](THIRD_PARTY_NOTICES), which reproduces each notice in full.
