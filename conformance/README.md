# Conformance vectors

Language-neutral test data every hem-tool implementation checks itself
against (REQ-TOOL-030). The requirements in `requirements/` say what the CLI
does; the files here pin the bytes where a requirement has an exact answer.

| File | What it pins | Source |
|---|---|---|
| `qr-payload.json` | the pairing QR payload (`ext pair`): key order, compact JSON, `hash` = base64(SHA-256(request)), escaping, empty fields | REQ-TOOL-016; the Encedo Manager's `pairdeviceNow` and the SDK's `ehem_ext_qr_payload` (encedo-hem-c-api 1.2.0, STEP-M12-137) |
| `exit-codes.json` | the exit codes of every command, and the codes every command shares (0, 1, 2, 12) | REQ-TOOL-003 … 028; the C implementation's command registry at 1.2.0 |

Rules:

- A vector changes only through the requirement it belongs to (§6.2 of
  REQUIREMENTS-MANAGEMENT.md): the requirement's revision names the
  change, the vector follows.
- Each implementation's test suite reads these files, or carries copies that
  a gate compares against them (the C implementation: `conformance_sync`).
- New vectors come with the requirement that needs them; the C eJWT and
  certificate fixtures stay C headers until a second implementation needs
  them.
