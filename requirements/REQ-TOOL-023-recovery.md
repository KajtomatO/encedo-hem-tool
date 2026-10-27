---
id: REQ-TOOL-023
title: hem-tool recovery — diagnose TLS/certificate trouble and run the matching remediation
status: approved
priority: should
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: user decision 2026-10-07 ("add to tool 'recovery' … based on previous problems"; single check-in attempt, no polling); incidents: expired device certificate (M1 gate 2026-07-16, REQ-NET-005; again 2026-10-06/07 — cloud renewed late, a cloud defect Encedo fixed after notification), TLS material lost after the 2026-07-22 wipe (REQ-SYS-013/TOOL-015), RTC unset after cold boot and ~8 % clock drift (KNOWN-ISSUES, REQ-AUTH-004/005); REQ-TOOL-003 and REQ-TOOL-015 (the building blocks); approved 2026-10-07 (M10 decomposition, user go-ahead); moved from encedo-hem-c-api (requirements/REQ-TOOL-023-recovery.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-TOOL-003", "REQ-TOOL-015", "REQ-SYS-003", "REQ-NET-005", "REQ-TOOL-018"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli", "ARCHITECTURE.md#7-milestones"]
---

# hem-tool recovery — diagnose TLS/certificate trouble and run the matching remediation

`hem-tool recovery` SHALL probe the device, classify what is wrong with
its TLS/certificate state, and run exactly the remediation the recorded
incidents call for — in one command, with a single attempt per leg (no
polling for the cloud).

- **Probe:** `GET /api/system/status` under the context's normal TLS mode
  (system trust unless `--insecure`/`--cacert` was given).
- **Classification and action:**
  1. *Healthy* — status answers with `https: true` → run one check-in
     (sets the RTC after a cold boot, resyncs the drifting clock,
     REQ-SYS-003) and report "nothing to recover"; exit 0.
  2. *Expired certificate* — the TLS failure is the REQ-NET-005
     expired-cert case → one check-in; if it delivers a new chain
     (REQ-SYS-006), run the `cert-install` sequence (REQ-TOOL-003:
     install under `--insecure`, reboot, poll, verify under system
     trust); exit 0. If the check-in delivers **no** chain: report that
     the cloud has not issued a renewal, point at rerunning later, exit
     3 — **one attempt, no waiting** (user decision 2026-10-07: the
     2026-10-07 late renewal was a cloud defect since fixed).
  3. *HTTPS down* — the https probe cannot connect → probe the same host
     over **http://**; if it answers with `https: false` (TLS material
     lost, the post-wipe state), run the `tls-recover` sequence
     (REQ-TOOL-015: check-in, login, `ehem_tls_recover`, reboot, poll
     until `https: true`); exit 0.
  4. *Unreachable* — neither scheme answers → exit 4 ("device
     unreachable — power-cycle?", the KNOWN-ISSUES stall note).
  5. *Other TLS failure* (hostname mismatch, unknown CA, …) → report the
     SDK detail and the manual options; exit 5. The tool never widens
     trust on its own except for the install leg of case 2.
- **Exit codes:** 0 recovered or healthy; 1 login/device/cloud failure;
  2 usage/environment; 3 cloud has not renewed yet; 4 unreachable or
  bounded wait exhausted; 5 unclassified TLS failure.
- **Auth:** passphrase only — the install legs write `/api/system/config`,
  which the firmware allows for `sub` U/M only (REQ-SYS-014 finding);
  the probe and check-in need no credentials.
- **Help placement:** `recovery` is listed in its auth group;
  `cert-install` and `tls-recover` move to the "manual recovery" section
  (REQ-TOOL-019 rev 2). Orchestration lives in hem-tool-core and reuses
  the cert-install and tls-recover code paths rather than duplicating
  them.

**Rationale:** every TLS/certificate incident so far was solved by the
same three building blocks in the right order, discovered by hand each
time (a day for the 2026-07-22 recovery; ~1 h for the 2026-10-07
rotation). One command that picks the order removes the archaeology.

**Acceptance criteria:**
- [x] RESOLVED (STEP-M10-060, 2026-10-09, option a — the user had left
      the choice to implementation): `ehem_error.tls_expired` appended to
      the public last-error struct (context-owned, append-only, ABI-safe;
      REQ-API-004 rev 2, mini §6.2 in chat) and set by the shared request
      path from the transport's REQ-NET-005 verdict — also when automatic
      recovery is off, which is how `recovery`'s probe context runs
      (`no_auto_checkin`), so the verdict surfaces instead of being
      repaired behind the tool's back. Documented in docs/API-GUIDE.md.
- [x] Unit (hem-tool-core, three fake transports for the three TLS
      postures): one test per case 1–5 — healthy (status + one check-in,
      exit 0); expired + chain → one relaxed check-in, then the full
      cert-install sequence and a trusted verify, exit 0; expired + no
      chain → exit 3 with NO retry (the insecure context sees exactly one
      check-in); https down + http `https:false` → the tls-recover
      sequence, exit 0; both down → exit 4; other TLS failure → exit 5
      with nothing written; `--mobile` / no passphrase → exit 2, zero
      traffic. *(tests/unit/test_recovery.c, 2026-10-09.)*
- [x] Live, attended (non-destructive on a healthy device): `recovery`
      exits 0 via case 1 with one check-in. *(2026-10-09, my.ence.do under
      system trust, binary from STEP-M10-060: "healthy: the device answers
      under the configured trust" → check-in → "nothing to recover", exit
      0. Finding: the healthy device's status omits the `https` field over
      HTTPS, so an absent field is treated as UNKNOWN (exit 5 on the http
      path), never as "down".)* The other cases are exercised whenever the
      incident recurs — standing note, recorded here with the date when it
      happens.
- [x] `--help` explains the five cases and that the cloud is tried once.
      *(registry.c entry, STEP-M10-060, 2026-10-09.)*
