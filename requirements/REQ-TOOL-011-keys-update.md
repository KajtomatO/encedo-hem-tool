---
id: REQ-TOOL-011
title: hem-tool keys update — rename LABEL / set DESCR with protected-key guard
status: approved
priority: should
revision: 3   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 2 = 
source: user decision 2026-07-17 (M7 decomposition tool set); REQ-KEY-007; REQ-TOOL-005 (protected-key policy); approved 2026-07-17; moved from encedo-hem-c-api (requirements/REQ-TOOL-011-keys-update.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-KEY-007", "REQ-TOOL-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool keys update — rename LABEL / set DESCR with protected-key guard

`hem-tool keys update <KID> --label <LABEL> [--descr <TEXT>]` SHALL
update a key's metadata via the public `ehem_key_update`, refusing to
touch protected keys without the per-key confirmation ritual.

- `--label` required (mirrors the firmware/REQ-KEY-007 rule); `--descr`
  optional UTF-8 text (the tool encodes; binary DESCR stays an SDK-level
  capability).
- **Preserve-descr behavior (rev2, from the REQ-KEY-007 whole-record
  finding):** the firmware CLEARS an omitted descr, so when `--descr` is
  not given the tool re-sends the stored descr (read from the same list
  walk that classifies the key) and notes it on stderr; `--descr ""`
  clears explicitly. Least-surprise for a metadata-rename tool.
- **Protected-key guard (REQ-TOOL-005 classifier):** if the CURRENT
  label classifies the key as protected (`TLS PrivateKey`,
  `TLS Certificate`, `(Android)`/`(iPhone)` substrings), the update — 
  which could strip the very label that protects it — requires the
  interactive literal `YES` per key, and `--yes` is ignored, exactly as
  `keys rm`. The tool must therefore `get`/`list` the key first to
  classify it.
- Renaming an unprotected key TO a protected-looking label is allowed
  but warned on stderr (it will make the key protected for later bulk
  operations).
- Exit codes follow the keys-rm convention: 0 success, 1 device/SDK
  error, 2 usage error; refusal at the prompt exits 0 with "skipped".
- Credentials via the standard flags/env (`EHEM_URL`,
  `EHEM_PASSPHRASE`).

**Rationale:** living documentation for REQ-KEY-007 and the first
metadata-mutating tool path; without the guard, a one-line rename could
silently disarm the protected-set convention that keys rm relies on.

**Acceptance criteria:**
- [x] Unit (hem-tool-core, fake transport, 2026-07-18): label-only
      (descr preserved + note), `--descr ""` (cleared), protected
      current-label → prompt required, `YES` proceeds, refusal skips
      with exit 0, `--yes` ignored; warn-on-rename-to-protected
      emitted; exit codes 0/1/2 covered (tests/unit/test_tool_m7.c).
- [x] Live demo (2026-07-18): EHEMTEST key created → renamed twice
      (once with --descr, once omitted → "preserving the stored descr
      (7 bytes)" note fired) → list showed the new label → `keys rm`
      cleaned up.
- [x] `--help` documents the guard and the preserve/clear semantics
      (the tool's usage text is the CLI reference; the README carries
      no per-subcommand table by design).
