---
id: REQ-TOOL-001
title: hem-tool status subcommand
status: approved
priority: must
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: ARCHITECTURE.md §8; §11 M1 gate; moved from encedo-hem-c-api (requirements/REQ-TOOL-001-status-subcommand.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-SYS-001", "REQ-SYS-002"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli", "ARCHITECTURE.md#7-milestones"]
---

# hem-tool status subcommand

`hem-tool` SHALL provide a `status` subcommand that connects to the HEM
given by `--url` (or the `EHEM_URL` environment variable) and prints the
device's live status and version information.

**Rationale:** The M1 gate: proof of a working end-to-end path (public API →
transport → real device → parsed structs). hem-tool consumes the public API
only — it doubles as living documentation and the manual integration
driver. TLS trust options must be reachable from the CLI so the M1 gate can
confirm the device certificate model (REQ-NET-003).

**Acceptance criteria:**
- [ ] `hem-tool status --url https://...` prints status fields (uptime,
      temperature, storage, hostname when present) and version fields
      (hardware, firmware, bootloader).
- [ ] `EHEM_URL` is honored when `--url` is absent; missing both yields a
      usage error and nonzero exit code.
- [ ] TLS options (CA file / insecure) are available as flags and map to
      the context options.
- [ ] Failure paths (unreachable, TLS failure, protocol error) print the
      `ehem_last_error` detail and exit nonzero.
- [ ] hem-tool sources include only `include/ehem/` public headers
      (greppable — no internal headers).
