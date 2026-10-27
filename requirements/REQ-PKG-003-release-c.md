---
id: REQ-PKG-003
title: C implementation — release matrix: one-file hem-tool per OS, built against the checksum-verified SDK package of the pinned version, tagged c-v<version>
status: approved
priority: must
revision: 2   # 2026-10-27 (STEP-T1-070, the T1 gate): record only — the tag-path and attended criteria met on the user's `c-v1.3.0-rc1` (c-release run 40549324183 → pre-release 425769575, 3 assets; the user's Linux and Windows runs of the published binaries; the Linux archive smoke-tested on the dev host as well). No meaning change. Rev 1 = 2026-10-27: drafted with the split plan; approved with it (user decisions: one-file only; clean split; tags c-v<version>; version 1.3.0 first). Takes over the hem-tool half of the SDK's REQ-BUILD-005/009 and REQ-TOOL-026's concern (a companion library cannot be missing from a one-file binary)
source: user decisions 2026-10-27 (ARCHITECTURE.md §1 items 1, 3, 4, 5); encedo-hem-c-api scripts/release/package-static.sh at v1.2.0 (the asserts of the one-file build), REQ-BUILD-009 rev 12 there (the SDK package contents), docs/RELEASING.md there (tag rules, annotated-tag release text); encedo-hem-c-api REQ-TOOL-026 (superseded by this REQ: the missing-DLL message has no DLL to miss)
applies_to: ["c"]
depends_on: ["REQ-PKG-001", "REQ-TOOL-025"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-implementations", "ARCHITECTURE.md#1-decisions-fixed"]
---

# C implementation — release matrix

A `c-v<version>` or `c-v<version>-rc<N>` annotated tag SHALL publish a
GitHub release (pre-release for `-rc`) holding, for Linux x86_64 and
Windows x86_64, the one-file `hem-tool-<label>-<os>-x86_64.{tar.gz|zip}`
built in package mode against the SDK's `encedo-hem-<pin>-<os>-x86_64`
package downloaded from the SDK's release and verified against its
`SHA256SUMS`, plus one `SHA256SUMS` over the archives; the release text is
the tag's annotation followed by the checksums.

- `<version>` is `project(VERSION)` of `c/CMakeLists.txt`; the tag must
  match it (`c-v<version>` or `c-v<version>-rc<N>`), else the job fails
  before building; `<label>` is the tag minus `c-v`, or
  `<version>+g<sha>` for a non-tag run.
- The archive holds exactly one `hem-tool[.exe]`, a `README.txt` naming
  the versions (tool, SDK, OpenSSL, libcurl) and `LICENSES/` (the tool's
  MIT licence, the SDK's, curl's, OpenSSL's, cJSON's, the BIP39 word list's,
  qrcodegen's).
- The job asserts before packaging: Linux — `ldd` names glibc-family
  libraries only, no RUNPATH; Windows — system DLLs and the UCRT only; no
  wolfSSL symbol or string in the binary; `--version` prints exactly
  `hem-tool <version>`, `sdk:       encedo-hem <pin>`, the OpenSSL and
  libcurl lines with `static`; `status` against a closed port exits 1; with
  a null-provider `OPENSSL_CONF` the binary exits 12 on Linux and 1 on
  Windows (the SDK's one-file controls).
- Versioning: a bug fix is a patch, a new command or flag a minor, a changed
  exit code or a removed flag a major.

**Rationale:** the user's decisions of 2026-10-27 — one asset per OS,
nothing to install beside it, built against what the SDK actually released
rather than a parallel dependency build. The asserts are the SDK's one-file
packaging checks, carried over so the split loses no guarantee.

**Acceptance criteria:**
- [x] `c/scripts/release/fetch-sdk.sh <version> <os> <dir>` downloads the
      SDK package and `SHA256SUMS`, verifies, unpacks into an empty
      directory; the version read from `c/cmake/sdk-version.cmake`
      (STEP-T1-040). *(2026-10-27: `fetch-sdk.sh linux deps-sdk` → the pin
      1.2.0 taken from the cmake file, `encedo-hem-1.2.0-linux-x86_64.tar.gz:
      OK` against the release's SHA256SUMS, the package unpacked, the
      archive removed; a non-empty target directory is refused.)*
- [x] `c/scripts/release/package.sh` enforces the tag rule, builds in
      package mode, runs every assert above, writes the archive and
      `SHA256SUMS`; local run against the published 1.2.0 Linux package
      recorded (STEP-T1-040). *(2026-10-27, Linux, no tag → label
      `1.3.0+g2381efd`: the SDK package's config-version 1.2.0 matched the
      pin; package mode built; `ldd` = linux-vdso, libc, ld-linux only, no
      RUNPATH; no wolfSSL symbol or string; `--version` exactly `hem-tool
      1.3.0` / `sdk:       encedo-hem 1.2.0` / `crypto:    OpenSSL 3.5.9
      (compiled) / 3.5.9 (loaded), static` / `transport: libcurl 8.10.1
      (compiled) / 8.10.1 (loaded), static`; archive
      `hem-tool-1.3.0+g2381efd-linux-x86_64.tar.gz` with README.txt (SDK
      version named, glibc floor) and seven LICENSES; smoke from an empty
      directory: one hem-tool file, `help` and `keys types` exit 0,
      `status` on a closed port exit 1, the null-provider `OPENSSL_CONF`
      exit 12 with the configuration remedy; SHA256SUMS written. The
      Windows half and the tag rule run in the workflows (STEP-T1-050).)*
- [x] `c-release.yml`: a branch run yields the two archives + `SHA256SUMS`
      as artifacts; the user's `c-v1.3.0-rc1` publishes the pre-release with
      3 assets (run id, release id recorded; STEP-T1-050/070). *(Branch half
      2026-10-27: the push to `main` ran 40530024709 — Linux 0:23, Windows
      2:25, every assert of package.sh green on both platforms, artifacts
      release-linux-x86_64 and release-windows-x86_64, the publish job
      skipped as designed. Tag half 2026-10-27 (STEP-T1-070): the user's
      annotated `c-v1.3.0-rc1` on 3c1976e ran 40549324183 — Linux 0:30,
      Windows 2:18, publish 0:12, every assert green — and published the
      pre-release 425769575 "hem-tool 1.3.0-rc1" with
      `hem-tool-1.3.0-rc1-linux-x86_64.tar.gz`,
      `hem-tool-1.3.0-rc1-windows-x86_64.zip` and a two-line `SHA256SUMS`
      re-verified in the publish job; the release text = the tag's
      annotation followed by the checksums. C CI ran green on the tag too
      (40549324395, 6/6).)*
- [x] Attended (the user, STEP-T1-070): the published Linux and Windows
      binaries run `status` under system trust, `--version` shows the SDK
      pin, `ext list` works — recorded here. *(Met 2026-10-27 on the
      published `c-v1.3.0-rc1` assets: the user, Linux and Windows, each
      binary unpacked alone — `--version`, `status` under system trust,
      `ext list`: "It works". The Linux archive repeated on the dev host:
      `sha256sum -c SHA256SUMS --ignore-missing` → `OK`; the directory holds
      `hem-tool`, `README.txt` (tool 1.3.0-rc1, SDK 1.2.0, OpenSSL 3.5.9,
      libcurl 8.10.1, glibc floor) and seven LICENSES; `ldd` = linux-vdso,
      libc, ld-linux, no RUNPATH; with `env -i` and
      `--url https://my.ence.do` (system trust): `--version` →
      `hem-tool 1.3.0` / `sdk:       encedo-hem 1.2.0` /
      `crypto:    OpenSSL 3.5.9 (compiled) / 3.5.9 (loaded), static` /
      `transport: libcurl 8.10.1 (compiled) / 8.10.1 (loaded), static`;
      `help` and `keys types` exit 0; `status` → firmware v1.2.2-DIAG,
      exit 0; `ext list` with the bearer → the one paired authenticator
      (Samsung SM-S938B, PROTECTED), exit 0.)*
