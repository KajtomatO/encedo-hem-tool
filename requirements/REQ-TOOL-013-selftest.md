---
id: REQ-TOOL-013
title: hem-tool selftest — run the device self-test and report health
status: approved
priority: should
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: user decision 2026-07-17 (M7 decomposition tool set); REQ-SYS-007; approved 2026-07-17; moved from encedo-hem-c-api (requirements/REQ-TOOL-013-selftest.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-SYS-007"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool selftest — run the device self-test and report health

`hem-tool selftest` SHALL run `ehem_system_selftest` and report the
device's health verdict, exiting non-zero when the device itself reports
a fail state.

- Prints: `fls_state` (with a PASS/FAIL word), the four timestamps
  (human-readable UTC), `kat_busy` when set, `se_state` when present,
  and the `repo_stats` block (total/deleted/invalid/fragmented/
  freeslots) — the only place key-slot exhaustion is visible.
- **Exit code carries the verdict:** 0 = reachable AND `fls_state == 0`;
  1 = SDK/device error; 3 = selftest ran but `fls_state != 0` (distinct
  code so scripts can tell "unhealthy" from "unreachable", mirroring the
  cert-install multi-code convention).
- Warns on stderr that the call re-runs the battery on-device (side
  effect), so operators don't hammer it in tight loops.
- Credentials via the standard flags/env.

**Rationale:** living documentation for REQ-SYS-007 and a scriptable
health gate — the natural companion to `hem-tool status` for the
device's own self-verdict (status reports reachability, selftest reports
integrity).

**Acceptance criteria:**
- [x] Unit (hem-tool-core, fake transport, 2026-07-18): healthy fixture
      → exit 0 with PASS + repo stats; `fls_state = 2` → exit 3 with
      FAIL; device 500 → exit 1; missing passphrase → exit 2
      (tests/unit/test_tool_m7.c).
- [x] Live demo (2026-07-18): `hem-tool selftest` exited 0 — PASS,
      UTC-rendered battery timestamps, secure-enclave state 0, repo
      stats (5 keys / 1584 free slots) consistent with the key count.
- [x] `--help` updated (the usage text is the CLI reference; the README
      carries no per-subcommand table by design).
