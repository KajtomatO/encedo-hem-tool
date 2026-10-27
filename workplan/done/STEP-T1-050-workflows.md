---
id: STEP-T1-050
title: "GitHub workflows — c-ci.yml (Linux gcc/clang/wolfssl + package mode; Windows UCRT64 submodule + package mode) and c-release.yml (tags c-v*: the one-file hem-tool per OS against the fetched SDK package, SHA256SUMS, the release text from the annotated tag)"
milestone: T1
implementation: c
implements: ["REQ-PKG-003", "REQ-PKG-004"]
traces:
  architecture: ["ARCHITECTURE.md#5-implementations"]
depends_on: ["STEP-T1-040"]
evidence:
  commits: ["a02a180", "7d7c84e"]   # start, the workflows; the done commit follows
  tests:
    - "2026-10-27: `.github/workflows/c-ci.yml` (6 legs: Linux gcc/clang OpenSSL, gcc wolfSSL, gcc package mode; Windows UCRT64 submodule and package mode; every leg checks pin == submodule tag; the package legs fetch the SDK release package, build with no tests, smoke help/--version (the `sdk:` line)/keys types and assert the glibc-only / system-DLL-only link) and `.github/workflows/c-release.yml` (push to main / c-v* tags / dispatch: fetch-sdk.sh + package.sh per platform, artifacts, the release job assembling SHA256SUMS, release-notes.sh, softprops/action-gh-release — named `hem-tool <label>`, pre-release for -rc) written from the SDK's ci.yml / release.yml at 1.2.0. YAML parsed locally. The runs themselves need the user's first push (box 1) and a dispatch/tag (box 2) — recorded here when they are in."
    - "2026-10-27, the user's first push (7d7c84e to development, then development → main): C CI run 40529996276 — 6/6 legs green: Linux (gcc, submodule, openssl) 0:41, Linux (clang, submodule, openssl) 5:04, Linux (gcc, submodule, wolfssl) 6:19, Linux (gcc, package) 0:22, Windows (UCRT64, submodule) 2:12, Windows (UCRT64, package) 2:21 — the Windows package leg links the SDK package's archives with the runner's MSYS2 GCC (ARCHITECTURE §8 risk 1 retired). C release run 40530024709 on the push to main (the dry run, label 1.3.0+g7d7c84e): Linux x86_64 0:23 and Windows x86_64 (MinGW UCRT64) 2:25 green — fetch-sdk.sh, package.sh with every assert on both platforms — artifacts release-linux-x86_64 (3.09 MB) and release-windows-x86_64 (3.07 MB); the publish job skipped, as designed for a branch push."
  notes: "Done 2026-10-27. The tag path (c-v1.3.0-rc1 → pre-release) is STEP-T1-070's."
reopened: []
cancelled: null
---

**Goal:** every push runs the C suite in submodule mode on both platforms
and builds the tool in package mode against the pinned SDK release (the
Windows package-mode leg is the early proof that the MSYS2 GCC links the
package's `.a` files); a `c-v*` tag builds, packages and publishes the
release (2 archives + `SHA256SUMS`), pre-release when `-rc`.

**Notes:** `actions/checkout` with `submodules: true`; path filters
`c/**`, `requirements/**`, `conformance/**` and the workflow files. Linux
deps via `c/sdk/scripts/install-deps-linux.sh --yes` + `libssl-dev`;
Windows via MSYS2 UCRT64 pacboy (toolchain, cmake, ninja, pkgconf, cmocka,
openssl, curl, wolfssl for the submodule leg). The release job needs no
dependency prefix — only the fetched SDK package — so no cold-cache
minutes. Runners `ubuntu-22.04` (the SDK package's glibc floor) and
`windows-latest`. The first push and the tags are the user's.

**Definition of done**
- [x] `c-ci.yml`: 6 legs green on the user's first push (run 40529996276).
- [x] `c-release.yml`: a branch-push run (main, 40530024709) produces the two
      archives and `SHA256SUMS` as artifacts on both platforms; the
      `c-v1.3.0-rc1` pre-release is STEP-T1-070's gate.
- [x] REQ-PKG-004 and REQ-PKG-001/003 criteria recorded with the run ids.
