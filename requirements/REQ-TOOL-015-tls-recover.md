---
id: REQ-TOOL-015
title: hem-tool tls-recover — one-command HTTPS restoration after TLS loss
status: approved
priority: should
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: user decision 2026-07-22; REQ-SYS-013 (the binding); the 2026-07-22 manual recovery sequence; moved from encedo-hem-c-api (requirements/REQ-TOOL-015-tls-recover.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-SYS-013", "REQ-SYS-001", "REQ-SYS-003", "REQ-SYS-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool tls-recover — one-command HTTPS restoration after TLS loss

`hem-tool tls-recover [--force]` SHALL restore the device's HTTPS end to
end: skip-if-healthy check, clock sync, cloud key+cert recovery
(REQ-SYS-013), reboot, and verification that the device serves HTTPS
again.

- **Intended use:** after a device wipe, against the device's HTTP URL
  (`hem-tool --url http://<host> tls-recover`) — the state in which the
  2026-07-22 recovery was needed. The full sequence:
  1. unauthenticated status GET — when the device already reports
     `https: true`, print "nothing to recover (renewals: cert-install)"
     and exit 0, unless `--force`;
  2. check-in (sets the RTC — required after a cold boot, when login
     would otherwise fail — and may refresh an expired cert on the way);
     a check-in failure warns and continues (the RTC may already be
     set);
  3. login + `ehem_tls_recover` (factory register endpoint);
  4. when the install reports `reboot_required`: reboot and poll status
     (bounded, cert-install-style pacing) until the device answers with
     `https: true`;
  5. report the restored state.
- Exit codes (the cert-install multi-code convention): 0 = recovered or
  nothing to do; 1 = login/device/cloud failure; 2 = usage/environment;
  3 = the cloud delivered no usable bundle; 4 = device did not return
  with HTTPS within the bounded wait.
- DISRUPTIVE (installs key material and reboots); `--help` says so.
  Orchestration lives in hem-tool-core so the CLI, unit tests, and any
  live demo drive one code path, public API only.

**Rationale:** the manual recovery took a day of discovery (browser tool
archaeology, firmware reading, scheme pitfalls — the recovery tool
itself defaults to `https://`, the very protocol that is down). One
command with a skip-guard makes the next wipe a non-event.

**Acceptance criteria:**
- [x] Unit (hem-tool-core, fake transport, tests/unit/test_recover.c,
      2026-07-22): healthy-status skip (exit 0, status probe only);
      `--force` runs the full sequence; downed-device flow (status →
      check-in legs → login → recovery legs → reboot → poll until
      `https:true`) exit 0; no-bundle → exit 3; poll exhaustion →
      exit 4; missing passphrase → exit 2. (Caught + fixed a real bug:
      `ehem_system_checkin` rejects a NULL `out`, so the clock-sync leg
      was a silent no-op — the tool now passes and frees an info.)
- [x] Live demo (2026-07-22): on the healthy device `tls-recover` exits
      0 via the skip path; `tls-recover --force` performed the full
      recovery (reinstall + reboot) and ended with the device serving a
      system-trusted HTTPS cert.
- [x] `--help` documents the command, `--force`, and its disruptive
      nature.
