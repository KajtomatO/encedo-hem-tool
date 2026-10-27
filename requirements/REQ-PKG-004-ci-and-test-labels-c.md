---
id: REQ-PKG-004
title: C implementation — CI legs and test labels mirrored from the SDK: both platforms, both SDK backends, submodule and package modes; unit / integration / disruptive gating
status: approved
priority: should
revision: 1   # 2026-10-27: drafted with the split plan; approved with it
source: encedo-hem-c-api ARCHITECTURE.md §9 (test policy) and .github/workflows/ci.yml at 1.2.0 (10 legs: Linux gcc/clang/OpenSSL, Windows MSYS2 UCRT64); REQ-TEST-002/003/005/007 there (the live-test gating and key hygiene the moved tests rely on); user decisions 2026-10-27 (ARCHITECTURE.md §4, §5.1)
applies_to: ["c"]
depends_on: ["REQ-PKG-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-testing-policy", "ARCHITECTURE.md#5-implementations"]
---

# C implementation — CI legs and test labels

The C implementation's test suite SHALL keep the SDK's three CTest labels
and their gating — `unit` (offline, always), `integration` (real device,
skipped unless `EHEM_TEST_URL` is set, `EHEMTEST`-prefixed keys only),
`disruptive` (additionally `EHEM_ALLOW_DISRUPTIVE=1`; never in CI) — and
`c-ci.yml` SHALL run the unit suite on every push on Linux (gcc, clang,
and the wolfSSL SDK backend) and Windows (MSYS2 UCRT64) in submodule mode,
plus one package-mode build per platform against the SDK's published
package of the pinned version (`help`, `--version`, `keys types` smoke;
glibc-only / system-DLL-only link check), with `submodules: true` on
checkout and path filters on `c/**`, `requirements/**`, `conformance/**`
and the workflow files.

- No test under `c/tests` includes a crypto backend header (gate
  `tests_backend_free`); the suite passes unchanged under both SDK backends.
- The live tests skip with exit 77 when ungated and pace their requests
  (`EHEM_TEST_PACE_MS`) as the SDK's do.

**Rationale:** the moved tests were written for exactly this gating; the
package-mode legs are the early, every-push proof of the release link path
(the Windows one proves the runner's GCC links the SDK package's archives).

**Acceptance criteria:**
- [ ] `ctest -L unit` lists the 18 tests, `tool_startup_cli` and the gates;
      `ctest -L integration` → `test_random_live`, `test_keys_rm_live`;
      `ctest -L disruptive` → `test_cert_install_live`; the live ones skip
      (77) without `EHEM_TEST_URL` *(local 2026-10-27: 21 unit tests green
      on gcc/clang, OpenSSL/wolfSSL; the two integration tests green against
      the dev device — STEP-T1-010)*.
- [ ] `c-ci.yml` — 6 legs green on the user's first push (run id recorded;
      STEP-T1-050).
