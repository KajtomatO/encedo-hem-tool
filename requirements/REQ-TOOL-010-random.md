---
id: REQ-TOOL-010
title: hem-tool random — read device hardware RNG bytes
status: approved
priority: should
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: user decision 2026-07-17 (M6 decomposition addition — pattern of keys pub/sign at M4, keys gen at M5); ARCHITECTURE.md §8 (thin consumer, subcommands grow with milestones); HEM-SDK-7/HEM-OP-3; approved 2026-07-17; moved from encedo-hem-c-api (requirements/REQ-TOOL-010-random.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-TOOL-001", "REQ-OPS-002", "REQ-TOOL-009"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool random — read device hardware RNG bytes

hem-tool SHALL provide a `random <N>` subcommand that prints `N` bytes of
device hardware RNG (via `ehem_random`, REQ-OPS-002) as lowercase hex to
stdout (`--raw` for binary, binary-safe on Windows like `sign`/`keys pub`).

- **Key handling:** `--kid <KID>` uses an existing AES key. Without
  `--kid` the tool creates a transient `EHEMTEST hem-tool random` AES-128
  key (REQ-KEY-005), harvests, and deletes it (REQ-KEY-004) — the
  create/delete orchestration lives in the tool, never in the SDK
  (REQ-OPS-002 design), and the delete runs even when the harvest fails.
- **Bounds:** N 1..4096 (arg-parse bound; keeps worst case at 256
  round-trips) — outside → exit 2 usage error, no I/O.
- Connection/credential conventions and exit codes follow the existing
  subcommands (0 success, 2 usage/environment, 1 runtime with an
  actionable message); orchestration in `hem-tool-core` shared with the
  unit tests.

**Rationale:** makes the REQ-OPS-002 harvest drivable by hand and is the
living documentation for the one M6 capability with no other CLI story;
an operator gets device-entropy without writing C. Follows the M4/M5
pattern of one tool subcommand per new SDK capability group.

**Acceptance criteria:**
- [ ] Against the fake transport: `random 20 --kid K` issues 2 encrypt
      requests against `K` and prints 40 hex chars + newline; `--raw`
      writes exactly 20 bytes; without `--kid` the request sequence is
      create → harvest → delete (delete also asserted on injected harvest
      failure); N=0 / N>4096 / non-numeric → exit 2 with zero transport
      calls (unit tests on the hem-tool-core function).
- [ ] Live: `hem-tool random 32` (no `--kid`) prints 64 hex chars, two
      runs differ, and no `EHEMTEST` key remains afterwards
      (REQ-TEST-003 hygiene).
