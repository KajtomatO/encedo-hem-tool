---
id: REQ-TOOL-002
title: hem-tool reports certificate refresh and offers a checkin subcommand
status: approved
priority: must
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: user decision 2026-07-15 ("hem-tool should inform user if certificate was invalid and was refreshed"); moved from encedo-hem-c-api (requirements/REQ-TOOL-002-tool-cert-notice.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-TOOL-001", "REQ-NET-005", "REQ-SYS-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool reports certificate refresh and offers a checkin subcommand

`hem-tool` SHALL inform the user whenever an operation involved an automatic
certificate recovery (REQ-NET-005), and SHALL provide an explicit `checkin`
subcommand that runs the check-in handshake on demand.

**Rationale:** A silent security-relevant event (the device presented an
invalid certificate; the SDK refreshed it) must be visible to the operator.
The explicit subcommand doubles as the manual driver for cert renewal, RTC
setting, and update discovery without needing the browser-based Manager.

**Acceptance criteria:**
- [ ] After any successful operation where `ehem_cert_refreshed(ctx)` is
      true, hem-tool prints a notice to stderr stating the certificate was
      invalid (expired) and was refreshed via check-in.
- [ ] `hem-tool checkin` runs `ehem_system_checkin()` and reports the result
      (certificate refreshed?, firmware/UI update available?, status);
      failures print `ehem_last_error` detail and exit nonzero.
- [ ] The notice and subcommand use only the public API (greppable).
