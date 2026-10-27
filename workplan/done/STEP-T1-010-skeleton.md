---
id: STEP-T1-010
title: "Skeleton of the repository — process files, conformance vectors, the C implementation under c/ with the SDK as a submodule (v1.2.0), sources moved from encedo-hem-c-api 1.2.0 without the delay-load code, the dev helper"
milestone: T1
implementation: c
implements: ["REQ-PKG-001", "REQ-TOOL-030"]
traces:
  architecture: ["ARCHITECTURE.md#5-implementations", "ARCHITECTURE.md#2-specification-and-conformance", "ARCHITECTURE.md#7-milestones"]
depends_on: []
evidence:
  commits: ["1abb352"]   # the initial import (user permission 2026-10-27); the done commit follows
  tests:
    - "2026-10-27, c/: `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build -L unit` (OpenSSL backend, gcc 13.3) — 21/21 after the backend-free gate was adapted to a tree without crypto_ref modules (20/21 before); `-DEHEM_CRYPTO_BACKEND=wolfssl` build tree 21/21; `CC=clang` build tree 21/21. `./dev test it` (the SDK checkout's hem.env sourced) — test_random_live 24.4 s + test_keys_rm_live 10.8 s, 2/2 against the dev device (fw v1.2.2-DIAG)."
  notes: null
reopened: []
cancelled: null
---

**Goal:** the new repository exists locally with everything the C
implementation needs to build and test exactly as it did inside the SDK,
plus the shared layer it is now part of: `requirements/` (the 27 REQ-TOOL
files moved with their ids), `conformance/` (the first vectors), the
process documents, `workplan/`.

**Notes:** user decisions 2026-10-27 (ARCHITECTURE §1): monorepo, C first
under `c/`; SDK as submodule `c/sdk` pinned to `v1.2.0`
(`c/cmake/sdk-version.cmake` is the one declared pin, asserted against the
submodule's `project(VERSION)` at configure time); `delayload_win.c` and
the `-shared` tool are not moved (one-file only). The SDK's test support is
compiled from the submodule by path (`fake_transport.c`; the fixtures and
`integration_env.h` / `ehem_test_keys.h` by include path) — no SDK-side
change was needed to pin 1.2.0. `BUILD_TESTING`, `EHEM_BUILD_TOOL`,
`EHEM_BUILD_SHARED` are shadowed as normal variables (CMP0077) so the
SDK's own suite and tool stay out of this build; `EHEM_CRYPTO_BACKEND` is
pre-set in the cache to `openssl` (what ships). Fresh import: no history —
the sources are those of encedo-hem-c-api `v1.2.0` (`4d0a5ce` on its
development branch is the records commit after the release). Commits in
this repository happen only with the user's permission; the user creates
the GitHub repository and pushes.

**Definition of done**
- [x] Layout per ARCHITECTURE §6: root process files, `requirements/`,
      `conformance/`, `workplan/`, `scripts/`, `.github/workflows/`, `c/`.
- [x] `c/sdk` submodule at `v1.2.0`; `c/cmake/sdk-version.cmake` pins 1.2.0
      and the configure step refuses a mismatch.
- [x] The 17 core sources + `main.c` + headers + vendored qrcodegen under
      `c/src/`; the 18 unit tests, `tool_force_mismatch.c`,
      `check_tool_startup.cmake`, the two gate scripts under `c/tests/unit/`;
      the two live integration tests and the disruptive cert-install test.
- [x] `c/CMakeLists.txt` (submodule | package modes, `hem-tool-core`,
      `hem-tool`, install), `c/tests/*/CMakeLists.txt`, `c/dev`
      (build / test unit|it / ci / tool / clean).
- [x] Unit suite green with gcc and clang, OpenSSL and wolfSSL; the live
      tool tests green against the dev device.
- [x] `conformance/qr-payload.json` (three known answers) and
      `conformance/exit-codes.json`; `conformance/README.md`.
- [x] The 27 REQ-TOOL files imported: same ids, `status: approved`
      (specification lifecycle), rev +1 with the move note, anchors
      retargeted to this ARCHITECTURE, `applies_to: ["c"]` on REQ-TOOL-024.
- [x] First commit of the repository (user permission 2026-10-27) — `1abb352`.
