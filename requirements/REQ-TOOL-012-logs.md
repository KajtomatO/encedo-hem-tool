---
id: REQ-TOOL-012
title: hem-tool logs — audit log listing, download, and signing-key display
status: approved
priority: should
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: user decision 2026-07-17 (M7 decomposition tool set); REQ-SYS-009; approved 2026-07-17; moved from encedo-hem-c-api (requirements/REQ-TOOL-012-logs.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-SYS-009"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool logs — audit log listing, download, and signing-key display

`hem-tool logs list`, `hem-tool logs get <ID> [--out FILE]`, and
`hem-tool logs key` SHALL drive the REQ-SYS-009 bindings through the
public API only.

- `logs list`: walks ALL pages (advancing by each page's returned
  count), prints one id per line plus a `total:` footer to stderr —
  stdout stays machine-consumable.
- `logs get <ID>`: writes the raw log text to stdout by default or to
  `--out FILE` (binary-safe, `_setmode` discipline on Windows like
  `sign`); non-existent id → device 404 → exit 1 with the SDK error on
  stderr.
- `logs key`: prints the base64 Ed25519 signing key, nonce, and
  nonce-signature, one labeled line each. Local signature verification
  stays OUT of the tool (the shim's Ed25519 verify is not public API;
  the tool is public-API-only per ARCHITECTURE §3) — the SDK-level
  integration test carries that proof.
- On an EPA device (list/get unrouted → NOT_FOUND) the tool reports
  "not available on this device (EPA build)" and exits 1; `logs key`
  works on both build variants.
- Exit codes: 0 success, 1 device/SDK error, 2 usage.

**Rationale:** living documentation for the logger group; gives the
device owner a one-command way to pull audit evidence off the device —
the CC-relevant workflow the tester scripts do in PHP.

**Acceptance criteria:**
- [x] Unit (hem-tool-core, fake transport, 2026-07-18): list pagination
      walk (two-page fixture, offsets 0 → 2); get bytes verbatim; key
      prints three labeled base64 lines; id guards (non-hex, "list",
      NULL) → usage with no wire traffic (tests/unit/test_tool_m7.c).
- [x] Live demo (PPA device, 2026-07-18): `logs list` printed 62 ids;
      `logs get` of the first returned the pipe-delimited log file
      ("# Encedo nGINE FW v1.2.2-DIAG" header); `logs key` printed the
      real Ed25519 triple.
- [x] `--help` updated (the usage text is the CLI reference; the README
      carries no per-subcommand table by design).
