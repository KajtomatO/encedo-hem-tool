---
id: STEP-T1-040
title: "Package mode and the release build — fetch-sdk.sh (the checksum-verified static SDK package of the pinned version), package.sh with the asserts ported from the SDK's package-static.sh and the c-v tag rule; a local run against the published 1.2.0 package"
milestone: T1
implementation: c
implements: ["REQ-PKG-001", "REQ-PKG-003"]
traces:
  architecture: ["ARCHITECTURE.md#5-implementations"]
depends_on: ["STEP-T1-030"]
evidence:
  commits: ["2381efd", "fadf9c4"]   # start, implementation; the done commit follows
  tests:
    - "2026-10-27, Linux (gcc 13.3, the published encedo-hem-1.2.0-linux-x86_64 package): `scripts/release/fetch-sdk.sh linux deps-sdk` — pin 1.2.0 read from cmake/sdk-version.cmake, checksum OK against the SDK release's SHA256SUMS, package unpacked into the new directory. `scripts/release/package.sh deps-sdk/encedo-hem-1.2.0-linux-x86_64 linux dist` — label `1.3.0+g2381efd`; the package's config-version 1.2.0 == the pin; package-mode configure (tests OFF) finding CURL in the package's lib/cmake and OpenSSL in the package; `ldd` glibc-family only, no RUNPATH; no wolfSSL symbol/string; the four `--version` lines exact; archive + README.txt + 7 LICENSES; smoke from an empty directory: one hem-tool file, help/keys types exit 0, closed-port status exit 1, null-provider OPENSSL_CONF exit 12 with the configuration remedy; SHA256SUMS written. REQ-PKG-001 package-mode criterion and REQ-PKG-003's two script criteria ticked."
  notes: null
reopened: []
cancelled: null
---

**Goal:** the shipped binary is built against the SDK's released static
package, not the submodule: `c/scripts/release/fetch-sdk.sh <version> <os>
<dir>` downloads `encedo-hem-<v>-<os>-x86_64.{tar.gz|zip}` + `SHA256SUMS`
from the SDK's GitHub release, verifies and unpacks into an empty
directory; `c/scripts/release/package.sh <sdk-pkg> <os> dist` configures
`-DHEM_TOOL_SDK_MODE=package -DCMAKE_PREFIX_PATH=<pkg>
-DOPENSSL_USE_STATIC_LIBS=ON -DCMAKE_BUILD_TYPE=Release`, builds, asserts
and packages `hem-tool-<label>-<os>-x86_64/{hem-tool[.exe], README.txt,
LICENSES/}` + `SHA256SUMS`.

**Notes:** asserts ported from encedo-hem-c-api `scripts/release/
package-static.sh` (v1.2.0): glibc-only `ldd` and no RUNPATH on Linux /
system DLLs + UCRT only on Windows; no wolfSSL symbol or string; the exact
`--version` lines including `sdk:       encedo-hem <v>`; exactly one
`hem-tool` file in the archive; `status` against a closed port → exit 1;
the `OPENSSL_CONF` null-provider control → exit 12 on Linux, 1 on
Windows. Tag rule: `c-v<tool version>` or `c-v<tool version>-rc<N>`,
annotated; `LABEL` = the tag minus `c-v`, else `<version>+g<sha>`.
LICENSES: this repo's `LICENSE`, plus `LICENSE.encedo-hem`, `COPYING.curl`,
`LICENSE.openssl`, `LICENSE.cjson`, `LICENSE.bip39-wordlist` copied from the
SDK package's `LICENSES/`, and `LICENSE.qrcodegen` from the vendored
header (presence-checked). `release-notes.sh` copied from the SDK
unchanged (it reads the annotated tag by name).

**Definition of done**
- [x] `fetch-sdk.sh linux deps-sdk` → checksum OK, package unpacked; the
      version read from `cmake/sdk-version.cmake`.
- [x] `cmake -DHEM_TOOL_SDK_MODE=package -DCMAKE_PREFIX_PATH=deps-sdk/...`
      configures and builds `hem-tool` with tests OFF; `ldd` glibc only.
- [x] `package.sh deps-sdk/… linux dist` → `dist/hem-tool-1.3.0+g2381efd-linux-x86_64.tar.gz`
      + `SHA256SUMS`, every assert green, the archive's README.txt names the
      SDK version.
- [x] REQ-PKG-003 criteria recorded; the Windows half is the CI/release
      job's (STEP-T1-050).
