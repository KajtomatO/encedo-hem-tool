---
id: REQ-TOOL-014
title: hem-tool reboot — reboot the device from the CLI
status: approved
priority: should
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: user decision 2026-07-18 ("add reboot command to hem-tool"); REQ-SYS-005 (the binding); moved from encedo-hem-c-api (requirements/REQ-TOOL-014-reboot.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-SYS-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool reboot — reboot the device from the CLI

`hem-tool reboot [--wait]` SHALL reboot the device via the public
`ehem_system_reboot` binding, and with `--wait` SHALL poll until the
device serves `GET /api/system/status` again (bounded, ~90 s) before
exiting.

- Needs the passphrase (scope `system:config`); the standard
  `--passphrase` / `EHEM_PASSPHRASE` sources apply.
- Without `--wait` the tool prints that the reboot was accepted and that
  the device returns in roughly 30-60 s, then exits 0 immediately.
- Exit codes: 0 = reboot accepted (and, with `--wait`, the device came
  back); 1 = login/reboot failure or `--wait` timeout (distinct message);
  2 = usage/environment error — the keys-subcommand convention.
- DISRUPTIVE by nature (interrupts the device for all users); the tool
  says so in `--help`. It is also the manual companion to the
  REQ-TEST-005 per-test reboot flag.

**Rationale:** operators (and the stall-mitigation workflow) need a
one-command reboot; until now the only CLI paths were cert-install's
internal reboot or raw curl. The binding has existed since M2
(REQ-SYS-005) — this exposes it.

**Acceptance criteria:**
- [x] Live (2026-07-18): `hem-tool reboot --wait` returned 0 with
      "device back after ~13 s" (the wait requires the device to be
      SEEN DOWN first — the firmware keeps serving ~2 s after accepting
      the reboot, which fooled the first implementation into a false
      ~0 s success; bounded at ~180 s, probes paced via
      request_pace_ms). Without `--wait` it exits right after the
      accepted reboot with the ~30-60 s note.
- [x] Missing passphrase → exit 2 with the standard message; a failed
      reboot → exit 1 with the SDK error detail (the login/reboot error
      path shares print_last_error with every other subcommand).
- [x] `--help` documents the command and its disruptive nature.
