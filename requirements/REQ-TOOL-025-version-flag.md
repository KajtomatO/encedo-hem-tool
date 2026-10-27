---
id: REQ-TOOL-025
title: hem-tool --version — SDK version, crypto backend compiled-against and loaded versions, libcurl version
status: approved
priority: should
revision: 6   # 2026-10-27 (STEP-T1-030): MEANING CHANGE, approved with the split plan 2026-10-27 — the first line is the implementation's own version (C: project(VERSION) of c/CMakeLists.txt, 1.3.0 first), and a second line `sdk:       encedo-hem <version>` names the SDK it embeds; "the tool and the SDK share the project version" is gone. The C cell is needs-reverify until test_tool_startup / tool_startup_cli are green again. Rev 5 = 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 4 = 2026-10-16 (STEP-M12-120 done): the both-backends criterion checked — the OpenSSL release jobs print the line (run 38894733780). No meaning change. Rev 3 = 2026-10-14 (STEP-M12-090 done): the both-backends criterion's evidence re-pointed — the OpenSSL CI legs cannot show the --version line; STEP-M12-120's OpenSSL release jobs print it. No meaning change. Rev 2 = 2026-10-13 (STEP-M12-020): output text settled (= the proposal); unit criteria recorded
source: user decision 2026-10-12 (M12 planning: "`hem-tool --version` — the SDK version, the wolfSSL version compiled against and the one loaded, and the libcurl version (today `--version` is an unknown option)"); ARCHITECTURE.md §11 M12 part A; REQ-API-011 (the facts); approved 2026-10-12 (M12 decomposition, user: "approve"); moved from encedo-hem-c-api (requirements/REQ-TOOL-025-version-flag.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-API-011", "REQ-TOOL-020"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli", "ARCHITECTURE.md#7-milestones"]
---

# hem-tool --version

hem-tool SHALL accept `--version` (and `version` as a command spelling,
like `help`) and print to stdout, exit 0, without touching the network
or requiring a successful SDK init: its own version (the implementation's
— in C `project(VERSION)` of `c/CMakeLists.txt`; rev 6), on a second line
the version of the SDK it embeds (`sdk:       encedo-hem <version>`,
`ehem_version()` in C; rev 6), the crypto backend name with the version
compiled against and the version loaded, and the libcurl versions compiled
against and loaded (the SDK's REQ-API-011); the link mode of the crypto
library (`dynamic` / `static`, a compile-time fact of the build) is shown
with it.

Output (exact text; rev 6 — until 1.2.0 the tool had no version of its own
and the first line carried the SDK's, with no `sdk:` line):

```
hem-tool 1.3.0
sdk:       encedo-hem 1.2.0
crypto:    OpenSSL 3.5.9 (compiled) / 3.5.9 (loaded), static
transport: libcurl 8.10.1 (compiled) / 8.10.1 (loaded), static
```

- When the loaded version differs from the compiled one the two values
  make it visible at a glance (the REQ-TOOL-024 refusal happens on the
  next real command, not here).
- The top-level help stays within its 45-line cap: `--version` is folded
  into the existing usage lines, not a new option line.

**Rationale:** 1.1.0 users could not tell which wolfSSL their binary was
built against or picked up — the first question in every foreign-library
report. Standard CLI hygiene; `--version` being "unknown option" today
was flagged by the user.

**Settled at STEP-M12-020 (2026-10-13):** the output is the proposal
above, both link words included (REQ-API-011 rev 2 carries them); the dev
host prints

```
hem-tool 1.1.0
crypto:    wolfSSL 5.6.6 (compiled) / 5.6.6 (loaded), dynamic
transport: libcurl 8.5.0 (compiled) / 8.5.0 (loaded), dynamic
```

`--version` anywhere wins over a command and over `--help`; the `version`
spelling takes no argument (`version extra` = usage error, exit 2). Both
are handled before the REQ-TOOL-024 init, so they run on any library.

**Acceptance criteria:**
- [x] `hem-tool --version` and `hem-tool version` print the three lines,
      exit 0, zero transport I/O; `hem-tool --version status` is still a
      version print (the flag wins, like `--help`) — unit smoke.
      *(2026-10-13, STEP-M12-020: `tool_startup_cli` on the real `main.c` —
      `--version`, `version`, `--version status`, `status --version` each
      exactly three lines, empty stderr, exit 0; `version` == `--version`.)*
- [x] Values equal what REQ-API-011 reports and `ehem_version()`; the
      output is ASCII (REQ-API-009 gate) and stable enough for scripts
      (first line `hem-tool <version>`).
      *(`tests/unit/test_tool_startup.c` `test_version_report`: first line
      exactly `hem-tool <ehem_version()>`, the crypto and transport lines
      rebuilt from `ehem_build_info_get`, every byte ASCII; the
      `ascii_strings` gate covers `startup.c`.)*
- [x] Works under a forced REQ-API-010 mismatch (unit) and on both
      backends — the OpenSSL CI leg shows `OpenSSL 3.x`.
      *(Forced mismatch: done — the report is identical with and without
      the seam (`tool_startup_cli`). OpenSSL, locally (STEP-M12-080):
      `crypto: OpenSSL 3.0.13 (compiled) / 3.0.13 (loaded), dynamic`. In
      CI test_build_info proves the struct this line prints names OpenSSL
      (run 38482938701), but no CI step prints the line and
      `tool_startup_cli` does not check the backend name — so the box
      stays open for STEP-M12-120: its OpenSSL release jobs run
      `hem-tool --version` in `package.sh` (job log, REQ-BUILD-009).
      Done 2026-10-16: release run 38894733780, both jobs green —
      `package-static.sh` fails unless the static OpenSSL hem-tool prints
      exactly `crypto:    OpenSSL 3.5.9 (compiled) / 3.5.9 (loaded),
      static` (and the matching libcurl line), on Linux and Windows.)*
- [x] Top-level help ≤ 45 lines with the usage line amended
      (test_registry test_top_page_shape stays green).
      *(Usage line 3 now ends `|   hem-tool --version`; the page stays 43
      lines; test_top_page_shape asserts `hem-tool --version`.)*
- [ ] Rev 6 (STEP-T1-030): four lines — `hem-tool <tool version>` first,
      `sdk:       encedo-hem <SDK version>` second, the two versions
      different values; the help header shows the tool version — unit
      (`test_version_report`) and the real binary (`tool_startup_cli`, both
      versions passed in from CMake); the release packaging asserts the
      exact lines (REQ-PKG-003).
