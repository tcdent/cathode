#!/usr/bin/env bash
#
# Cathode dependency installer — vendors the five upstream libraries Cathode is
# assembled from, at pinned commits, into vendor/lib/cathode/<lib>/.
#
# WHY THIS EXISTS
#   Cathode authors no HTML engine, JS runtime, or CSS parser of its own; it assembles
#   four mature libraries (quickjs-ng, litehtml, HarfBuzz, Blend2D) plus Blend2D's build
#   dependency (AsmJit). The library sources are NOT committed to this repo — vendor/ is
#   gitignored. This script reproduces the exact pinned trees on any checkout. Each lib is
#   a FULL clone with its origin/.git retained, so a future version bump is just "fetch,
#   pick a newer tag, update the SHA below, re-run" — the repo stays preconfigured to track
#   upstream through the engine's lifetime rather than being frozen to a historyless export.
#
#   Identity of a vendored lib is its commit: this script verifies `git rev-parse HEAD`
#   equals the pinned SHA after checkout. That is the provenance proof.
#
# BUMP PROCEDURE
#   To move a library to a newer version:
#     1. cd vendor/lib/cathode/<lib> && git fetch --tags origin
#     2. Inspect/choose the target: `git tag --sort=-v:refname | head` or `git log origin/<branch>`
#     3. Update that library's pinned SHA (and tag comment) in the PINS table below.
#     4. Re-run this script — it is idempotent: an existing clone is fetched + re-checked-out
#        to the new pin; a missing one is freshly cloned.
#   Pin to released tags where upstream publishes them; Blend2D and AsmJit have no release
#   tags (rolling release), so they pin to a recorded master commit.
#
# CONSTRAINTS (sprint 001, scaffold-only)
#   This script clones + checks out + verifies ONLY. It performs NO build, NO configure, and
#   NO source modification. Building the libraries from source and producing platform binaries
#   is the integration sprint's (002) concern.
#
# ASMJIT NOTE (deferred to 002)
#   Blend2D's JIT pipeline build expects AsmJit at [blend2d]/3rdparty/asmjit. We stage AsmJit
#   as a sibling under vendor/lib/cathode/asmjit/; the relocation/symlink into Blend2D's
#   3rdparty/ tree is a 002 build-time step, not done here.
#
set -euo pipefail

# Resolve repo root from this script's location: Cathode/ -> .. = repo root. Staging is REPO-ROOT
# relative, deliberately NOT plugin-local: each lib is a FULL clone retaining its own .git, and
# Cathode/ is the directory a consumer drops into their project's Plugins/ — vendored upstream
# trees do not belong there. The repo-root-anchored /vendor/ ignore rule already covers this path.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEST_ROOT="$REPO_ROOT/vendor/lib/cathode"

# PINS — "lib | upstream url | tag (informational) | pinned commit"
PINS=(
  "quickjs-ng|https://github.com/quickjs-ng/quickjs|v0.15.1|fd0a0210b7be00957751871e7e01b8291268fc29"
  "litehtml|https://github.com/litehtml/litehtml|v0.9|6ca1ab0419e770e6d35a1ef690238773a1dafcee"
  "harfbuzz|https://github.com/harfbuzz/harfbuzz|14.2.1|56feae4035bdd48f62ba2b8d8c16232d4d89b3a4"
  "blend2d|https://github.com/blend2d/blend2d|(no release tag)|6dbc2cefbc996379e07104e34519a440b49b15d7"
  "asmjit|https://github.com/asmjit/asmjit|(no release tag)|0bd5787b54b575ed94bf32ac452153b34385c514"
)

mkdir -p "$DEST_ROOT"

echo "Cathode dependency installer"
echo "  repo root : $REPO_ROOT"
echo "  staging to: $DEST_ROOT"
echo

fail=0
for entry in "${PINS[@]}"; do
  IFS='|' read -r lib url tag sha <<< "$entry"
  dest="$DEST_ROOT/$lib"
  echo "===== $lib  ($tag)  pin=$sha ====="

  if [[ -d "$dest/.git" ]]; then
    echo "  existing clone -> fetch + checkout pin"
    git -C "$dest" fetch --quiet origin
    git -C "$dest" checkout --quiet "$sha"
  else
    echo "  cloning $url"
    rm -rf "$dest"
    git clone --quiet "$url" "$dest"
    git -C "$dest" checkout --quiet "$sha"
  fi

  head="$(git -C "$dest" rev-parse HEAD)"
  if [[ "$head" == "$sha" ]]; then
    echo "  HEAD==pin OK : $head"
  else
    echo "  HEAD MISMATCH: got $head, expected $sha"
    fail=1
    continue
  fi

  lic="$(cd "$dest" && ls | grep -iE '^(LICENSE|COPYING|COPYRIGHT)' | head -1 || true)"
  if [[ -n "$lic" ]]; then
    echo "  license file: $lic"
  else
    echo "  license file: NONE FOUND AT ROOT"
    fail=1
  fi

  echo "  size        : $(du -sh "$dest" | cut -f1)"
  echo
done

if [[ "$fail" -ne 0 ]]; then
  echo "install.sh: one or more libraries failed verification." >&2
  exit 1
fi
echo "All five libraries staged at pinned commits."
