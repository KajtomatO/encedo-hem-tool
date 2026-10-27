---
id: STEP-T1-030
title: "hem-tool 1.3.0 — the tool's own version: `--version` prints the tool version first and the SDK's on a second line; the remedies no longer name release assets; tests updated"
milestone: T1
implementation: c
implements: ["REQ-TOOL-024", "REQ-TOOL-025"]
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli", "ARCHITECTURE.md#5-implementations"]
depends_on: ["STEP-T1-020"]
evidence:
  commits: ["e8e0ff9", "65ff2f3"]   # start, implementation; the done commit follows
  tests:
    - "2026-10-27: test_tool_startup `test_version_report` (four lines, the first two exact — `hem-tool <HEM_TOOL_VERSION_STRING>` / `sdk:       encedo-hem <ehem_version()>`, the two versions different), `test_version_refusal_message` and `test_static_refusal_message` (the dynamic-link remedy: `install the <backend> <version> library hem-tool was built against … needs no separate one`, no asset name); `tool_startup_cli` on the real main.c (`--version`, `version`, `--version status`, `status --version`: four lines, `hem-tool 1.3.0` / `sdk:       encedo-hem 1.2.0`, both versions passed from CMake). Suites: `ctest -L unit` 23/23 (gcc, OpenSSL), `./dev ci` 23/23 gcc + 23/23 clang, `./dev test unit --wolfssl` 23/23. `build/hem-tool --version` → `hem-tool 1.3.0` / `sdk:       encedo-hem 1.2.0` / crypto / transport; `--help` header `hem-tool 1.3.0 - Encedo HEM device CLI`."
  notes: null
reopened: []
cancelled: null
---

**Goal:** `hem-tool --version` reports four lines — `hem-tool 1.3.0`,
`sdk:       encedo-hem 1.2.0`, the crypto line, the transport line
(REQ-TOOL-025 rev — the version is the implementation's,
`HEM_TOOL_VERSION_STRING` from `project(VERSION)`); the help header shows
the tool version; the startup refusal (REQ-TOOL-024) keeps its exit 12 and
message but drops the wolfSSL-asset remedies and `HEM_RELEASE_OS` (a
released hem-tool has its crypto library linked in).

**Notes:** `startup.c` (`hem_tool_version()`, `hem_tool_print_version`,
the remedy block), `main.c` (the `HEM_TOOL_DELAYLOAD` blocks go; the help
header takes `hem_tool_version()`), `test_tool_startup.c` (four lines, the
`sdk:` line from `ehem_version()`, the asset-name assertions removed),
`check_tool_startup.cmake` (`hem-tool ${HEM_TOOL_VERSION}` first,
`sdk:       encedo-hem ${EHEM_VERSION}` second — both passed from
`tests/unit/CMakeLists.txt`). The exact `--version` lines are what the
release packaging asserts (STEP-T1-040).

**Definition of done**
- [x] `build/hem-tool --version` → the four lines; `hem-tool --help` header
      `hem-tool 1.3.0 - Encedo HEM device CLI`.
- [x] REQ-TOOL-025 rev 6 and REQ-TOOL-024 rev 10 (meaning changes approved
      with the plan 2026-10-27) — texts updated, new criteria ticked by the
      tests above; the C cells go straight back to verified (the rework and
      its tests landed in the same step).
- [x] Unit suite green (gcc + clang, both backends).
