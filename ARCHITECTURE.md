# encedo-hem-tool — architecture

**TL;DR.** `hem-tool` is the command-line client of the
[Encedo HEM](https://ence.do) network cryptographic device: every operation
the device offers, drivable by hand and by scripts, with a fixed set of
commands, flags and exit codes. This repository holds **one specification**
of that CLI (`requirements/`, the `TOOL` area) and **its implementations**,
one per language SDK, each in its own directory with its own build, tests,
version and releases. Today there is one: **`c/`**, on the C SDK
[encedo-hem-c-api](https://github.com/KajtomatO/encedo-hem-c-api). The
specification and the C sources came from that SDK repository at its 1.2.0
(user decision 2026-10-27: hem-tool is its own deliverable).

## 1. Decisions (fixed)

User decisions of 2026-10-27, after the SDK's 1.2.0 release:

1. **Separate deliverable.** hem-tool is released from this repository, on
   its own version line and tags; the SDK's releases carry no hem-tool
   binaries from SDK 1.3.0 on.
2. **Monorepo for several implementations sharing one requirements set.**
   `requirements/` and `conformance/` are language-neutral; each
   implementation lives under its directory (`c/` now; `js/`, `python/`
   may follow). A requirement has one specification status and one
   implementation state per implementation (REQUIREMENTS-MANAGEMENT.md §3.2).
3. **Versions and tags per implementation.** The C tool continues the
   version line users know: **1.3.0** first; tags `c-v<version>[-rcN]`;
   assets keep their names, `hem-tool-<version>-<os>-x86_64.{tar.gz,zip}`.
   `--version` prints the tool's version first and the SDK version it
   embeds on a second line.
4. **One-file binaries only (C).** OpenSSL and libcurl linked in, no
   companion library. The wolfSSL `-shared` tool of 1.0–1.2, its companion
   asset, the Windows delay-load import and the missing-DLL message
   (REQ-TOOL-026) are not carried over.
5. **The C implementation consumes the SDK two ways** (§5.1): as a git
   submodule pinned to the SDK release tag for its build and tests (the
   tests need the SDK's hidden test seams and fake transport, which exist
   only in a static SDK built from source), and as the SDK's published static
   package for the release binary (`find_package(encedo-hem)` of the
   checksum-verified `encedo-hem-<version>-<os>-x86_64` asset of the same
   version). One declared pin, `c/cmake/sdk-version.cmake`.
6. **Fresh import, no history.** The first commit of this repository holds
   the sources as they were in encedo-hem-c-api at `v1.2.0`; their history
   up to then is that repository's.
7. **Process carried over** from the SDK (REQUIREMENTS-MANAGEMENT.md),
   with the per-implementation matrix; the 27 `REQ-TOOL` requirements keep
   their ids and texts.
8. **Every implementation is a thin consumer of its SDK's public API** —
   it reaches no SDK internals (the C rule since the SDK's first milestone,
   now REQ-PKG-002 with a gate).

## 2. Specification and conformance

- `requirements/REQ-TOOL-*.md` say what every hem-tool does: the commands
  (§3), their options, output, exit codes (`conformance/exit-codes.json`),
  the protected-key policy, the pairing QR payload
  (`conformance/qr-payload.json`). They name behaviour, never a library.
- `requirements/REQ-PKG-*.md` say how an implementation is built, tested,
  packaged and released; most carry `applies_to`.
- `conformance/` pins the bytes where a requirement has an exact answer
  (REQ-TOOL-030). Every implementation's tests check against these files or
  carry copies a gate compares (C: `conformance_sync`). A vector changes only
  through its requirement (§6.2 of the process).
- The SDK repository stays the ground truth for device behaviour and for the
  SDK API the C implementation calls (REQUIREMENTS-MANAGEMENT.md §8).

## 3. The CLI

What a user gets, in every implementation (moved from encedo-hem-c-api's
ARCHITECTURE.md §8 at 1.2.0; the milestone references are the SDK's):

- Commands are grouped by what they need — none (`status`, `checkin`,
  `init-device`, `init-wizard`, `keys types`), any bearer (`keys list/rm/pub/
  gen/update`, `sign`, `random`, `logs`, `selftest`, `reboot`, `ext list/
  login`), or a passphrase only (`ext pair` — the device demands `sub="U"` for
  pairing changes; `wipe-device` and `recovery` — config writes). A "manual
  recovery" help section lists the building blocks of `recovery`
  (`cert-install`, `tls-recover`). The top-level help is capped at 45 lines;
  `hem-tool help <command>` (= `<command> --help`) is the per-command page.
  (REQ-TOOL-017/019/020.)
- `cert-install` (REQ-TOOL-003): harvests the cloud certificate from the
  check-in exchange, skips if the device already serves it, else
  authenticates, installs, **reboots**, waits and verifies — one distinct
  exit code per failure mode. `tls-recover` (REQ-TOOL-015) restores HTTPS
  after a wipe via the provisioning cloud; `recovery` (REQ-TOOL-023) is the
  diagnose-then-remediate orchestrator over `checkin`, `cert-install` and
  `tls-recover` — a single check-in attempt, no polling.
- `keys list` marks the protected set; `keys rm` removes by `--all` or
  `--label-prefix`, never touching protected keys in bulk — removing one
  requires naming its exact label **and** typing the literal `YES` at a
  per-key prompt; `--yes` is deliberately ignored for them (REQ-TOOL-004/
  005/006). **Protected** (mirrors the Python `wipe_keys.py`): labels exactly
  `TLS PrivateKey` / `TLS Certificate`, or containing `(Android)` /
  `(iPhone)` — the paired phones.
- `keys pub`, `sign`, `keys gen` (with the key-type table of `keys types`,
  REQ-TOOL-027; the NIST-mode default lives in the tool), `keys update`,
  `random` (a transient `EHEMTEST` AES key when `--kid` is not given),
  `logs`, `selftest`, `reboot` (REQ-TOOL-007…014).
- `ext pair` / `ext list` / `ext login` (REQ-TOOL-016): pairing renders the
  QR **in the terminal** — never through a web service, which would leak
  user/email/hostname — with the payload the Encedo Auth app checks
  (`conformance/qr-payload.json`); on a Windows console the rows go out as
  UTF-16 through the console API. `ext login` demonstrates the push-confirm
  login with distinct exit codes for approved / rejected / timeout /
  no-authenticator. Unpairing is plain `keys rm` (the phones are protected).
- `init-device` and `wipe-device` over the device's init and wipeout
  (attended-only verification, §4); `init-wizard` (REQ-TOOL-028) asks one
  explained question per setting, hides the passphrase, confirms the backup
  words, and only then runs the init flow; `hostname` defaults to
  `my.ence.do`, the host the default URL points at (REQ-TOOL-017).
- Connection parameters via flags or env (`EHEM_URL`, `EHEM_PASSPHRASE`);
  `--mobile` takes a push confirmation instead of a passphrase (REQ-TOOL-018;
  exit 13 = no answer, 14 = rejected). `--passphrase` is **deprecated**
  since 1.2.0 (a passphrase on the command line stays in the shell history
  and the process list): one warning, removal in a later release; a safer
  entry (`hem-tool login`) is BACKLOG item 9.
- Start of run (REQ-TOOL-024/025): the SDK is initialised explicitly
  before any command; a crypto library that is missing or not the one the
  SDK was built against ends the run with one message and exit 12.
  `--version` reports the tool, the SDK it embeds, the crypto library and
  the transport (compiled against / loaded / how linked).
- Every string the tool prints or compares is 7-bit ASCII (REQ-TOOL-029;
  device-supplied strings are passed through).

## 4. Testing policy

Carried over from the SDK's §9:

- **Unit** (CTest label `unit`, or the implementation's equivalent): no
  network; every command driven through a fake transport with scripted
  device and broker answers, exit codes and output asserted; plus the gates
  (ASCII strings, backend-free tests, public API only, conformance sync).
  Run on every build, both platforms.
- **Integration** (`integration`): real device; enabled only when
  `EHEM_TEST_URL` (+ `EHEM_TEST_PASSPHRASE`) is set — otherwise skipped, so
  CI without a device stays green. Tests may create/delete any key **except**
  the protected set; test keys carry the `EHEMTEST` label prefix.
- **Disruptive** (`disruptive`): mutates device availability — `cert-install`
  reboots. Attended, never in CI; needs the label opt-in and
  `EHEM_ALLOW_DISRUPTIVE=1`.
- **Attended-only**: `init-device`, `init-wizard`, `wipe-device` and the
  release binaries' smoke on both platforms have no live test; the outcome
  of an attended run is recorded as evidence in the owning REQ and step.
- The dev HEM stalls under back-to-back load (the SDK's KNOWN-ISSUES); the
  live suite paces requests and retries a stalled test.

## 5. Implementations

### 5.1 C (`c/`)

- **SDK:** [encedo-hem-c-api](https://github.com/KajtomatO/encedo-hem-c-api),
  public API only (`include/ehem/*.h`). Two modes (REQ-PKG-001):
  - `HEM_TOOL_SDK_MODE=submodule` (default): `c/sdk` is the SDK at the tag
    named by `c/cmake/sdk-version.cmake` (asserted at configure time), built
    from source with its own tests, shared library and in-SDK tool off, and
    linked statically — the hidden test seams (pinned clock, fast PBKDF2,
    poll interval, forced crypto mismatch) and the fake transport are plain
    symbols of that archive, so the 18 unit tests moved unchanged. The
    crypto backend defaults to OpenSSL (what ships); wolfSSL stays a CI leg.
  - `HEM_TOOL_SDK_MODE=package`: `find_package(encedo-hem <pin> EXACT)` of
    the SDK's published static package (`libencedo-hem.a` + `libcurl.a` +
    `libssl.a` + `libcrypto.a`, headers, CMake config, `.pc`) — the release
    build; no tests.
- **Targets:** `hem-tool-core` (every command; the CLI, the unit tests and
  the live tests share it), `hem-tool` (`main.c`), the vendored QR encoder
  (Project Nayuki `qrcodegen`, MIT, verbatim — `c/src/vendor/qrcodegen/`).
- **Version:** `project(hem-tool VERSION x.y.z)` → `HEM_TOOL_VERSION_STRING`;
  the SDK version comes from `ehem_version()`. A bug fix is a patch, a new
  command or flag a minor, a changed exit code or removed flag a major
  (REQ-PKG-003).
- **Tests and gates** (§4; REQ-PKG-002/004): `c/tests/unit` (18 tests, the
  forced-mismatch CLI run, `ascii_strings`, `tests_backend_free`,
  `public_api_only`, `conformance_sync`), `c/tests/integration` (`random`,
  `keys rm`), `c/tests/disruptive` (`cert-install`). `c/dev`: `build`,
  `test unit|it`, `ci`, `tool`, `clean`.
- **CI** (`.github/workflows/c-ci.yml`): Linux gcc/clang/wolfSSL in submodule
  mode and one package-mode leg against the pinned SDK release; Windows
  MSYS2 UCRT64 submodule and package mode — the latter proves on every push
  that the runner's GCC links the package's archives.
- **Release** (`.github/workflows/c-release.yml`, `c/scripts/release/`;
  REQ-PKG-003): a `c-v*` tag → `fetch-sdk.sh` downloads and verifies the
  SDK package of the pinned version → `package.sh` builds in package mode,
  asserts (glibc-only / system-DLL-only, no wolfSSL symbol, exact `--version`
  lines, one file in the archive, closed-port and `OPENSSL_CONF` controls),
  packages `hem-tool-<label>-<os>-x86_64/{hem-tool[.exe], README.txt,
  LICENSES/}` → the job publishes two archives + `SHA256SUMS`, pre-release
  for `-rcN`, the text from the annotated tag. Runners `ubuntu-22.04` (the
  SDK package's glibc floor) and `windows-latest`. Bumping the SDK pin:
  `c/docs/RELEASING.md`.
- **Known limitation:** device strings print through the Windows console's
  OEM code page (BACKLOG item 12).

## 6. Layout

```
README.md  LICENSE  ARCHITECTURE.md  REQUIREMENTS-MANAGEMENT.md  KNOWN-ISSUES.md  BACKLOG.md
requirements/        REQ-TOOL-*.md (the CLI), REQ-PKG-*.md (per implementation), TRACE.md (generated)
conformance/         qr-payload.json, exit-codes.json, README.md
workplan/            TEMPLATE.md, todo/ doing/ done/
scripts/             trace_gen.py + trace_cfg.py (the TRACE generator)
.github/workflows/   c-ci.yml, c-release.yml (one pair per implementation)
c/                   the C implementation (a self-contained CMake project)
  CMakeLists.txt  cmake/sdk-version.cmake  dev  README.md  docs/RELEASING.md
  sdk/               submodule → encedo-hem-c-api @ the pinned tag
  src/               the commands (hem-tool-core) + main.c; src/vendor/qrcodegen/
  tests/             unit/ integration/ disruptive/
  scripts/release/   fetch-sdk.sh, package.sh, release-notes.sh
```

## 7. Milestones

- **T1 — the C implementation stands on its own: `c-v1.3.0`** *(planned and
  approved 2026-10-27 together with the SDK's M13, which removes hem-tool
  from encedo-hem-c-api once T1 has shipped; the plan: the SDK repository's
  planning record of that date)*.
  - **A. Skeleton** — the layout above, the SDK submodule at `v1.2.0`, the
    sources moved without the delay-load code, the dev helper, the first
    conformance vectors, the 27 REQ-TOOL files imported (STEP-T1-010).
  - **B. Tests and gates** — the 18 unit tests, the forced-mismatch CLI run
    and the two live tests green under `c/`; `public_api_only` and
    `conformance_sync` added (STEP-T1-020).
  - **C. The tool's own version** — 1.3.0; `--version` with the `sdk:`
    line; remedies without asset names (STEP-T1-030; REQ-TOOL-024/025 revs).
  - **D. Package mode and the release build** — `fetch-sdk.sh`, `package.sh`
    with the asserts of the SDK's one-file packaging, the `c-v` tag rule
    (STEP-T1-040); the workflows (STEP-T1-050).
  - **E. Docs and process** — the REQ set complete (REQ-TOOL-029/030,
    REQ-PKG-001…004), READMEs, RELEASING, KNOWN-ISSUES, BACKLOG, the
    committed TRACE generator (STEP-T1-060).
  - *Out of scope:* any behaviour change of the CLI (BACKLOG items 6–12
    wait); other implementations.
  - **Gate (draft):** CI green on both platforms in both SDK modes; the
    user's annotated `c-v1.3.0-rc1` publishes the pre-release (2 archives +
    `SHA256SUMS`); the user runs the published binaries on Linux and Windows
    (`status` under system trust, `--version` showing `sdk: encedo-hem
    1.2.0`, `ext list`); docs checklist; TRACE with the C column complete;
    then `c-v1.3.0` (STEP-T1-070).
  - *Decomposition (2026-10-27):* STEP-T1-010 … 070 in order, 010 and 020
    started together (the skeleton is only proven by the tests).

## 8. Risks

1. **The SDK package's archives vs the release runner's compiler** — the
   package's `.a` files were built by the SDK job's MSYS2 GCC; the tool
   links with the runner's current one. Plain C archives, no LTO, same UCRT:
   low risk; the Windows package-mode CI leg catches drift on every push.
   Fallback: build the release in submodule mode against the SDK's
   `build-deps.sh` prefix.
2. **A system OpenSSL found instead of the package's** in package mode —
   `OPENSSL_USE_STATIC_LIBS=ON` and `CMAKE_PREFIX_PATH` first; the ldd /
   objdump asserts fail the job otherwise.
3. **CMake variable leakage between the two projects** (`BUILD_TESTING`,
   the SDK's options) — normal-variable shadowing (CMP0077), checked by
   `ctest -N` listing no SDK tests.
4. **The hidden seams are the SDK's internals** — a future SDK may rename or
   remove them; the pin bump procedure runs the suite first, and the fixture
   headers come from the pinned submodule, never copies.
5. **Two repositories, one behaviour** — until SDK M13 ships, encedo-hem-c-api
   still builds its own 1.2.0 hem-tool; this repository is the only source
   of hem-tool from 1.3.0 on. The SDK's README points here from 1.3.0.
