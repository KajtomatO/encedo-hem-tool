---
id: REQ-PKG-001
title: C implementation — the SDK is consumed at one declared version, from source for build and tests, from the published package for the release binary
status: approved
priority: must
revision: 1   # 2026-10-27: drafted with the split plan; approved with it the same day (user decision: separate repository, SDK as a submodule for tests + the published static package for the release)
source: user decisions 2026-10-27 (ARCHITECTURE.md §1 items 5 and 8; the plan "Split hem-tool into its own repository", approved 2026-10-27); the SDK's hidden test seams (encedo-hem-c-api src/proto_auth.h, proto_ext.h, crypto_shim.h) exist only in a static SDK built from source; the SDK's static package (encedo-hem-<version>-<os>-x86_64) carries libencedo-hem.a, libcurl.a, libssl.a, libcrypto.a, headers, CMake config and .pc (REQ-BUILD-009 of the SDK at 1.2.0)
applies_to: ["c"]
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-implementations", "ARCHITECTURE.md#1-decisions-fixed"]
---

# C implementation — SDK consumption at one declared version

The C implementation SHALL build against exactly one declared version of
the Encedo HEM C SDK, `HEM_TOOL_SDK_VERSION` in `c/cmake/sdk-version.cmake`,
in two modes: **submodule** (default) — `c/sdk` is the SDK repository at
the tag `v<version>`, built from source with its test suite, shared
library and in-SDK tool off and linked statically; **package** —
`find_package(encedo-hem <version> EXACT CONFIG)` of the SDK's installed
static package, with no SDK source on any include path. The configure step
refuses a submodule whose `project(VERSION)` differs from the pin; the
release build (REQ-PKG-003) uses package mode against the SDK's published
package of the pinned version.

- Submodule mode builds the SDK's test support (fake transport) from the
  submodule by path, so the unit tests reach the SDK's hidden seams
  (pinned clock, PBKDF2 iteration override, poll interval, forced crypto
  mismatch) through the static link; the crypto backend defaults to
  OpenSSL (what ships) and may be switched to wolfSSL.
- Package mode builds no tests (the seams' headers are not installed).
- Bumping the pin is one edit plus moving the submodule to the matching
  tag; the pin, the submodule tag and the package the release fetches must
  agree (checked in CI: `git -C c/sdk describe --tags --exact-match`).

**Rationale:** the tests' value lies in the seams and the fake transport,
which the SDK deliberately keeps out of its public ABI; the shipped binary
should link exactly what the SDK released and what its consumer gate
proved. One pin keeps the two modes honest with each other.

**Acceptance criteria:**
- [x] Submodule mode: `cmake -S c -B build` with `c/sdk` at `v1.2.0`
      configures, builds `hem-tool` and the test suite; `ctest -N` lists
      no SDK test; with the submodule at another tag the configure step
      fails naming both versions. *(Local 2026-10-27: configure + build +
      21/21 unit tests, gcc and clang, OpenSSL and wolfSSL — STEP-T1-010;
      23/23 with the two added gates — STEP-T1-020/030; `ctest -N` lists
      the tool's 23 tests and no SDK test.)*
- [x] Package mode: against the unpacked `encedo-hem-1.2.0-<os>-x86_64`
      package, `-DHEM_TOOL_SDK_MODE=package` configures without `c/sdk` on
      any include path, builds `hem-tool`, and `HEM_TOOL_BUILD_TESTS` is
      forced off (STEP-T1-040). *(Local 2026-10-27, Linux: fetch-sdk.sh →
      checksum OK; package.sh configured package mode ("tests OFF" notice),
      built the one-file binary — ldd glibc only — and its `--version`
      reads `sdk:       encedo-hem 1.2.0`.)*
- [x] CI asserts pin == submodule tag == fetched package version on every
      run (STEP-T1-050). *(2026-10-27: every leg of C CI run 40529996276
      ran the "Check the SDK pin" step — `pin=1.2.0 submodule=v1.2.0`; the
      package legs and the release run 40530024709 fetched
      `encedo-hem-1.2.0-<os>-x86_64`, whose config-version package.sh
      compared with the pin.)*
