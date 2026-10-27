---
id: REQ-TOOL-003
title: hem-tool cert-install — harvest, install, reboot, verify
status: approved
priority: must
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: user decision 2026-07-16 ("implement the working cert install" → "plan hem-tool cert-install"); live remediation procedure executed 2026-07-16 (python script); REQ-SYS-003 root-cause finding; moved from encedo-hem-c-api (requirements/REQ-TOOL-003-cert-install.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-TOOL-001", "REQ-SYS-004", "REQ-SYS-005", "REQ-SYS-006", "REQ-AUTH-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool cert-install — harvest, install, reboot, verify

hem-tool SHALL provide a `cert-install` subcommand automating the manual
remediation proven on 2026-07-16, for devices whose firmware (v1.2.2)
cannot apply check-in certificate updates itself:

1. **Harvest:** run the check-in flow; take the cloud-delivered chain
   (REQ-SYS-006). No chain → explain and exit nonzero.
2. **Skip-if-current:** if the certificate the device currently serves
   already matches the harvested chain's leaf, report "already current"
   and exit 0 without touching the device; `--force` overrides.
3. **Install:** authenticate (passphrase from `EHEM_PASSPHRASE` or
   `--passphrase`, consistent with existing tool conventions) and install
   via REQ-SYS-004; require `reboot_required` in the reply.
4. **Reboot:** REQ-SYS-005, then poll until the device answers again
   (bounded, ~2 min), tolerating the connection-refused window.
5. **Verify:** re-fetch device status under the context's normal TLS mode
   and report the outcome, including the new chain's leaf validity dates.
   In SYSTEM trust mode a successful verify proves the rotation end to
   end; with `--insecure` the tool says verification was skipped.

The command mutates device state and reboots it: it SHALL print what it is
about to do, and its live integration test is disruptive-gated
(`disruptive` CTest label + `EHEM_ALLOW_DISRUPTIVE=1`) — the unit-level
flow is covered via the fake transport.

**Skip-if-current mechanism (RESOLVED at implementation, 2026-07-16):** two
signals, both keyed on the leg-1 `csn` serial (REQ-SYS-006 `current_serial`),
whichever the broker gives:
1. **Broker-suppressed chain.** The broker delivers a `newcrt` chain only when
   it deems the device's `csn` stale. So `newcrt_chain` absent WITH a
   `current_serial` present means the broker already judged the device current
   → the tool reports "already current" and exits 0. (`newcrt_chain` absent AND
   `current_serial` absent — no cert loaded / non-TLD hostname — is the genuine
   nothing-to-do error, exit 3.)
2. **Explicit serial match.** When a chain IS delivered, the tool parses its
   leaf serial with `ehem_cert_inspect()` (wolfCrypt `wc_ParseCert` in the
   crypto shim; serials normalized to hex with a single leading 0x00 sign byte
   dropped so both producers agree) and compares it to `current_serial`; equal →
   "already current", exit 0. `--force` overrides only path 2 (path 1 has no
   chain to reinstall).
Live-confirmed 2026-07-16: against the current dev device the broker took path 1
(`current_serial=C173D2A9…`, no chain) and the tool exited 0 without rebooting.

**Acceptance criteria:**
- [x] Full flow against the fake transport: check-in legs, install POST
      body, reboot GET, polling, and final verify asserted in sequence;
      exit 0 with a summary naming old→new serial + the new validity window
      (test_cert_install.c `test_full_install_flow`).
- [x] Skip-if-current path exits 0 without install/reboot; `--force`
      proceeds (test_cert_install.c `test_skip_if_current`,
      `test_already_current_no_chain`, `test_force_reinstalls`). Mechanism
      recorded above.
- [x] Each failure mode exits nonzero with a distinct code + actionable
      message: no-chain(3), no-passphrase(2), check-in(4), parse(5), auth(6),
      install-rejected(7), reboot(8), device-timeout(9) — one
      test_cert_install.c case each; the timeout message adapts to the TLS mode
      (insecure vs verifying).
- [x] `disruptive` CTest label + `EHEM_ALLOW_DISRUPTIVE=1` gate wired
      (tests/disruptive/test_cert_install_live.c, excluded from
      `-L integration`; skips 77 without the gate).
- [x] Live run recorded (2026-07-16, NON-disruptive): `hem-tool cert-install`
      against my.ence.do harvested the chain and exited 0 "already current"
      (serial C173D2A9148ECD6225A3DFE5299B82CE) with no reboot — proving the
      harvest + skip-if-current path end to end. The full install→reboot→verify
      path stays **deferred** to the next natural cert expiry (running it now
      would pointlessly reboot a current device); the 2026-07-16 python-script
      remediation is the reference behavior for that path.
