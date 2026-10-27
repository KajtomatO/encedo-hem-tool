---
id: REQ-TOOL-029
title: Every literal the tool prints or compares is 7-bit ASCII
status: approved
priority: should
revision: 1   # 2026-10-27: drafted with the split plan; approved with it — the hem-tool half of encedo-hem-c-api's REQ-API-009 ("SDK and hem-tool sources"), whose SDK half stays there
source: encedo-hem-c-api REQ-API-009 at 1.2.0 (its scope covered "the SDK and hem-tool sources"; user decision 2026-10-27 splits it along the repositories); the SDK's ascii_strings gate (tests/unit/check_ascii_strings.c), copied for the C implementation
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# ASCII-only tool text

Every string and character literal in an implementation's sources SHALL be
7-bit ASCII — messages, prompts, help text, option names, the QR cells'
encoding being the one exception handled as bytes — so that consoles of
any code page show the tool's own output intact; strings the device or the
user supply are passed through unchanged. Comments and vendored code are
not covered.

**Rationale:** the Windows console decodes stdout in its OEM code page
(KNOWN-ISSUES: the 1.2.0-rc3 QR display); ASCII output is the one
encoding every console agrees on. Device-supplied strings are the device's
(BACKLOG item 12 for their display on Windows).

**Acceptance criteria:**
- [ ] C: the `ascii_strings` CTest (`c/tests/unit/check_ascii_strings.c`, a
      small lexer over `c/src/**` minus `vendor/`) passes on every CI leg
      *(local 2026-10-27: green — STEP-T1-010)*.
- [ ] Every later implementation adds an equivalent gate over its sources
      (recorded when it exists).
