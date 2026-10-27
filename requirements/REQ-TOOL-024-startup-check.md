---
id: REQ-TOOL-024
title: hem-tool initialises the SDK up front and refuses a foreign crypto library with a clear message and its own exit code
status: approved
priority: must
revision: 10   # 2026-10-27 (STEP-T1-030): MEANING CHANGE, approved with the split plan 2026-10-27 — the remedy for a dynamically linked crypto library names no release asset any more (install the library hem-tool was built against; the released hem-tool has its library linked in, REQ-PKG-003 — the wolfSSL companion and the one-file asset of the SDK-era releases are gone with the split); the exit-12 contract, the message shape and the linked-in remedies are unchanged. The C cell is needs-reverify until test_tool_startup / tool_startup_cli are green again. Rev 9 = 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 8 = 2026-10-19 (STEP-M12-135 done): re-verified — the rev 7 remedies' unit test (test_tool_startup) and tool_startup_cli green on all 10 legs of CI run 39320511927 and in both release jobs' unit suites (run 39320626797 / `v1.2.0-rc2`); verified again. No meaning change. Rev 7 = 2026-10-19 (STEP-M12-135, §6.2 approved in chat): the dynamic remedies name the one-file hem-tool by its release asset name `hem-tool-<version>-<os>-x86_64` — from 1.2.0 the plain asset (user decision 2026-10-19: the wolfSSL build is `-shared`; until v1.2.0-rc1 the one-file build was `-static`, which the remedy said); needs-reverify until test_tool_startup / tool_startup_cli are green again (STEP-M12-135). Rev 6 = 2026-10-16 (STEP-M12-120 done): re-verified — the static-link remedy's unit test green on every CI leg (run 38892261894) and the Linux release job's null-provider control (run 38894733780); its criterion checked. No meaning change. Rev 5 = 2026-10-15 (STEP-M12-120, §6.2 approved in chat): when the crypto library is linked into hem-tool, the remedy names the OpenSSL configuration — needs-reverify until its unit test passes. Rev 4 = 2026-10-13: `keys types` criterion closed (STEP-M12-040). Rev 3: pre-gate preview recorded (STEP-M12-030). Rev 2 = 2026-10-13 (STEP-M12-020): message text and init placement settled; unit and documentation criteria recorded. No meaning change — approved status kept
source: ARCHITECTURE.md §11 M12 part A ("hem-tool refuses up front — hem-tool calls ehem_global_init explicitly at startup (today it calls only the cleanup) and exits with a clear message and an exit code of its own"; user decision 2026-10-10); KNOWN-ISSUES.md "Release hem-tool with a foreign libwolfssl crashes"; REQ-API-010 (the SDK check), REQ-API-011 (the versions for the message); approved 2026-10-12 (M12 decomposition, user: "approve"); the static-link remedy approved 2026-10-15 (STEP-M12-120, §6.2 in chat: the -static hem-tool's OpenSSL is linked in, so its refusal can only come from an OpenSSL configuration); moved from encedo-hem-c-api (requirements/REQ-TOOL-024-startup-check.md at v1.2.0) by user decision 2026-10-27
applies_to: ["c"]   # the crypto-library startup check is the C implementation's (its SDK links a crypto library)
depends_on: ["REQ-API-010", "REQ-API-011", "REQ-TOOL-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli", "ARCHITECTURE.md#7-milestones"]
---

# hem-tool initialises the SDK up front and refuses a foreign crypto library

hem-tool SHALL call `ehem_global_init()` before any other SDK call — right
after argument parsing, before any output or traffic — and, when it
fails, exit with **code 12** after one stderr message naming the cause:
for `EHEM_ERR_BACKEND_MISMATCH` the crypto backend, the version hem-tool
was built against, the version it loaded (the SDK's REQ-API-011) and the
remedy (rev 10: for a dynamically linked library — a from-source build —
install the library hem-tool was built against, a released hem-tool having
its crypto library linked in, REQ-PKG-003; when the crypto library is
linked into hem-tool, the OpenSSL configuration it read, `OPENSSL_CONF`
or `openssl.cnf`); for any other code the `ehem_rc_str()` name.

- Today `main.c` calls only `ehem_global_cleanup()` at the end; the init
  happens implicitly inside the first `ehem_ctx_create()`, deep in a
  command, and a mismatch crashes before any check (1.1.0).
- Exit code **12** is new and reserved for "the SDK could not be
  initialised" (today's codes: 0, 1, 2 = usage/environment, 3–9 per
  command, 13/14 mobile). It is distinct from 2 so that scripts can tell
  a broken installation from a wrong command line. (The SDK repository's
  REQ-TOOL-026 — the Windows missing-DLL message of the 1.2.0 `-shared`
  tool — used the same code; the one-file-only release of 1.3.0 retired
  it.)
- `help`, `--help`, `--version` (REQ-TOOL-025) and `keys types`
  (REQ-TOOL-027) run **before** the init and work on any library — the
  user can always read the help and see what hem-tool expected.
- The message is ASCII (REQ-API-009) and the exit code is documented in
  the top-level help's exit-code note and in the archive README.

**Rationale:** a crash with "stack smashing detected" (or 0xC0000135 on
Windows) tells the operator nothing; the SDK can now detect the
condition (REQ-API-010), and the tool is where the human reads the
answer. Doing it first also removes the only implicit-init path in the
tool.

**Settled at STEP-M12-020 (2026-10-13).** Placement in `main.c`: argument
parsing and its usage errors (exit 2, including the REQ-TOOL-019 auth-class
check, now run before the URL resolution) → `--version` / `version` → the
help pages → **`hem_tool_global_init()`** (hem-tool-core `startup.c`) → the
REQ-TOOL-017 URL notice → dispatch. So the refusal is the first and only
output of a refused run. `HEM_TOOL_EXIT_INIT` (= 12) lives in `startup.h`
for REQ-TOOL-026's hook. The message (stderr, ASCII, `%s` only):

```
error: hem-tool cannot use the wolfSSL library it loaded (EHEM_ERR_BACKEND_MISMATCH)
  wolfSSL: built against 5.7.2, loaded 5.6.6
  remedy: install the wolfSSL 5.7.2 library hem-tool was built against; a released
          hem-tool has its crypto library linked in and needs no separate one
```

(Rev 10 — the SDK-era text named the release's wolfSSL companion asset and
the one-file `hem-tool-<version>-<os>-x86_64` asset; both left with the
split. A dynamically linked crypto library is a from-source build's.)

Equal versions (the struct self-test refused a same-version library built
with other options) append `- same version, incompatible build` to the
second line; a non-wolfSSL backend's remedy names that library instead of
the companion asset; any other init failure prints
`error: the SDK could not be initialised: <ehem_rc_str>` — exit 12 in every
case. An unknown command under a refused library also exits 12 (the
command is not looked up before the init — the installation is the
problem either way).

**Settled at STEP-M12-120 (2026-10-15, §6.2 approved in chat).** When the
build information reports the crypto library linked in (`crypto_link`
`static` — the one-file hem-tool, REQ-BUILD-009), no other library can be
the cause. For OpenSSL the remedy names the configuration instead (on
Linux the one-file binary reads `/etc/ssl/openssl.cnf`; on Windows it
reads none):

```
  remedy: OpenSSL is linked into hem-tool, so the OpenSSL configuration it
          read (OPENSSL_CONF or openssl.cnf) left it without the algorithms
          it needs - fix or unset it
```

Any other backend linked in (a development build only — no published
package links wolfSSL statically) says that the build itself is broken.

**Acceptance criteria:**
- [x] Unit (hem-tool-core / main.c smoke, REQ-API-010 seam forcing a
      mismatch): `hem-tool status` prints one message with the backend
      name, both versions and the remedy, exits 12, and performs zero
      transport I/O; `hem-tool help`, `--help`, `--version`, `keys types`
      still succeed under the same forced mismatch.
      *(`keys types` added by STEP-M12-040 the same day — `keys types` /
      `keys types ED25519` exit 0 under both seam modes.)*
      *(2026-10-13, STEP-M12-020 — all but `keys types`, which joins the
      pre-init commands with STEP-M12-040 and is added to the same check
      there: `tests/unit/test_tool_startup.c` (the message for both seam
      modes, exactly one `error:` line, ASCII) and `tool_startup_cli`
      (`check_tool_startup.cmake`) on the REAL `main.c` — a test build
      whose constructor (`tool_force_mismatch.c`) forces the refusal from
      `EHEM_TEST_FORCE_MISMATCH`: `status` → exit 12, empty stdout, no
      default-URL notice before the message; `keys list` → 12; `help`,
      `--help`, `status --help`, `help keys rm`, `--version`, `version`
      → 0; zero I/O shown by the URL `http://127.0.0.1:1` — the same run
      without the seam reaches the transport and exits 1.)*
- [x] Unit: with the crypto library linked in, the refusal's remedy
      names the OpenSSL configuration (`OPENSSL_CONF`, `openssl.cnf`) and
      neither the companion asset nor the one-file hem-tool; a statically
      linked non-OpenSSL backend is reported as a broken build. Release
      job: the Linux one-file hem-tool under an `OPENSSL_CONF` that
      activates only the `null` provider prints that remedy and exits 12
      (REQ-BUILD-009's control).
      *(`hem_tool_print_refusal()` in `startup.c`, split out of
      `hem_tool_global_init` so the unit test can feed it other builds'
      facts. `test_static_refusal_message` (tests/unit/test_tool_startup.c):
      OpenSSL linked in → the remedy names `OPENSSL_CONF` and `openssl.cnf`,
      and neither "-static hem-tool", "install the" nor "wolfssl-"; wolfSSL
      linked in → "this hem-tool build is broken"; the dynamic builds keep
      their remedies. `test_version_refusal_message` takes this build's
      link mode, so the release static build runs it as well. Green on all
      10 legs of CI run 38892261894 (2026-10-16) and in both release jobs'
      static unit suite (run 38894733780). The Linux job ran the unpacked
      `-static` hem-tool under a null-provider `OPENSSL_CONF`: exit 12 and
      the remedy, else the job fails. Rev 7, 2026-10-19: the marker the
      tests look for is now "one-file hem-tool", and the dynamic remedies
      must name `hem-tool-<ehem_version()>-` + "-x86_64 (1.2.0 and later)";
      re-verification: STEP-M12-135 — done 2026-10-19: `startup.c` prints
      both dynamic remedies with `ehem_version()` + `HEM_RELEASE_OS`
      (`%s` only, ASCII), test_tool_startup asserts every dynamic remedy
      contains "one-file hem-tool", `hem-tool-<ehem_version()>-` and
      "-x86_64 (1.2.0 and later)" and the linked-in remedies neither;
      green on all 10 legs of CI run 39320511927 (8dd915f) and in both
      release jobs' unit suites (run 39320626797, `v1.2.0-rc2`).)*
- [x] Without a forced mismatch every command behaves as before
      (existing unit suite green; `ehem_global_init` is called exactly
      once per run — the SDK's idempotency makes a second implicit call
      harmless).
      *(`./dev ci` 51/51 gcc + clang, ASan 51/51; one explicit call in
      `main.c`; usage errors keep exit 2 under the seam — `version extra`,
      no command, `--mobile cert-install`.)*
- [x] Demonstrated at the M12 gate on Linux: the release dynamic hem-tool
      in a directory WITHOUT the companion library, on a host whose
      `libwolfssl.so.42` is 5.6.6 → the message, exit 12, no crash
      (recorded with date and versions; this is REQ-API-010's gate
      criterion seen from the tool).
      *(Gate, 2026-10-19 (STEP-M12-140), the PUBLISHED binary: the
      `hem-tool-1.2.0-rc2-linux-x86_64-shared.tar.gz` asset of release
      417736562, checksum verified against the release's `SHA256SUMS`,
      unpacked alone on the dev host (Ubuntu, system `libwolfssl.so.42` =
      5.6.6-1.3build1; `ldd` resolves it from `/lib/x86_64-linux-gnu`, the
      RUNPATH `$ORIGIN` finds nothing). `env -i ./hem-tool --version` →
      "wolfSSL 5.7.2 (compiled) / 5.6.6 (loaded), dynamic";
      `env -i ./hem-tool --url https://my.ence.do status` → exit 12, empty
      stdout, stderr exactly the message of this REQ — "error: hem-tool
      cannot use the wolfSSL library it loaded (EHEM_ERR_BACKEND_MISMATCH)
      / wolfSSL: built against 5.7.2, loaded 5.6.6 / remedy: put the wolfSSL
      library from the release asset wolfssl-5.7.2-linux-x86_64 next to
      hem-tool, or use the one-file hem-tool asset
      hem-tool-1.2.0-linux-x86_64 (1.2.0 and later), which needs no separate
      crypto library" — no crash (1.1.0 aborted here with "stack smashing
      detected"). With the companion asset's `libwolfssl.so.42` copied
      beside it: `ldd` resolves the `$ORIGIN` copy, `--version` "5.7.2
      (compiled) / 5.7.2 (loaded)", `status` under system trust → device
      data (fw v1.2.2-DIAG), exit 0.)*
      *(Pre-gate preview 2026-10-13 (STEP-M12-030), a dev Release build —
      not the published binary: hem-tool built against the pinned wolfSSL
      5.7.2 + libcurl 8.10.1 prefix, copied alone into an empty directory on
      this host (system `libwolfssl.so.42` = 5.6.6, `ldd` resolves it):
      `env -i ./hem-tool --url https://my.ence.do status` printed
      "error: hem-tool cannot use the wolfSSL library it loaded
      (EHEM_ERR_BACKEND_MISMATCH) / wolfSSL: built against 5.7.2, loaded
      5.6.6 / remedy: …" and exited 12 — no crash; `--version` showed
      "wolfSSL 5.7.2 (compiled) / 5.6.6 (loaded), dynamic". The box stays
      open for the gate's run of the published binary.)*
- [x] Documentation: the exit code in the help page's exit-code note
      (page still ≤ 45 lines, REQ-TOOL-020) and in the release README.txt.
      *(The top page's closing line now reads ``command options: `hem-tool
      help <command>`; exit 12 = crypto library missing or wrong`` — 43
      lines, test_registry asserts `exit 12`; `scripts/release/package.sh`
      README.txt paragraph "hem-tool checks the wolfSSL it loaded …" and
      the job log runs `hem-tool --version`.)*
- [ ] Rev 10 (STEP-T1-030): the dynamic-link remedy names the library
      hem-tool was built against and no release asset (`test_version_refusal_message`
      on a dynamic build, `test_static_refusal_message`'s dynamic cases);
      the linked-in remedies and `tool_startup_cli` unchanged — green on
      both compilers and both SDK backends.
