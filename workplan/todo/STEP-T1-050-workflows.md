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
  commits: []
  tests: []
  notes: null
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
- [ ] `c-ci.yml`: 6 legs green on the user's first push.
- [ ] `c-release.yml`: a `workflow_dispatch` run produces the two archives
      and `SHA256SUMS` as artifacts; a `c-v1.3.0-rc1` tag publishes a
      pre-release (STEP-T1-070's gate).
- [ ] REQ-PKG-004 criteria recorded with the run ids.
