---
id: REQ-TOOL-009
title: hem-tool keys gen — generate a key on the device
status: approved
priority: should
revision: 3   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 2 = 
source: user decision 2026-07-16 (M5 decomposition: add hem-tool keys gen); ARCHITECTURE.md §8 (thin consumer, subcommands grow with milestones); HEM-SDK-6 (generate); approved 2026-07-16; rev 2 — note 2026-10-12 (M12 decomposition, §6.2 approved in chat; no status change): `keys gen -h` lists every creatable type and points at `keys types` (REQ-TOOL-027); the verbatim pass-through and every behaviour here are unchanged; moved from encedo-hem-c-api (requirements/REQ-TOOL-009-keys-gen.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-TOOL-001", "REQ-KEY-005", "REQ-KEY-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool keys gen — generate a key on the device

hem-tool SHALL provide a `keys gen <TYPE>` subcommand that creates a key
via `ehem_key_create` (REQ-KEY-005) and prints the new kid (32-char hex)
to stdout. Options:

- `--label LABEL` — **required** (the device rejects create without a
  label); passed through to the SDK, whose validation governs (printable,
  length bound per REQ-KEY-005 — `EHEM_ERR_ARG` maps to exit 2).
- `--descr STR` — optional; the argument's raw bytes are passed as the
  binding's descr (the SDK base64-encodes per REQ-KEY-005).
- `--mode ECDH|ExDSA|ECDH,ExDSA` — optional, passed verbatim (the device
  matches these literals exactly; `ExDSA,ECDH` is a device 400). **Tool
  default for the NIST-P/K types** (`SECP256R1/384R1/521R1/256K1`,
  classified via REQ-KEY-006): when `--mode` is omitted the tool sends
  `ECDH,ExDSA` and prints a note — the device's own default is ECDH-only
  (python OQ-19), which produces a key that cannot sign. For all other
  types an omitted `--mode` sends none (the firmware ignores mode for
  them). The SDK itself stays verbatim/no-default per REQ-KEY-005; this
  default lives in the tool.

`TYPE` is passed through verbatim (no tool-side allowlist — REQ-KEY-005;
an unknown type is a device 400 reported as a runtime failure). *(Rev 2,
2026-10-12: from 1.2.0 the per-command help lists the 23 creatable types
grouped by purpose and points at `keys types` — REQ-TOOL-027; the list is
documentation, the pass-through stays.)*
Connection and credentials follow the existing tool conventions
(`EHEM_URL`/`--url`, `EHEM_PASSPHRASE`/`--passphrase`); exit 0 on
success, 2 on usage/environment errors (missing TYPE or `--label`,
invalid `--mode` literal, SDK `EHEM_ERR_ARG` pre-validation, missing
URL/passphrase), 1 on runtime failure (auth, scope, device 400/406,
network) with an actionable message. The orchestration lives in
`hem-tool-core` (shared with the unit tests, like the other subcommands).

**Rationale:** user decision at M5 decomposition (2026-07-16). Makes the
M3/M5 create path drivable by hand and is the operator's counterpart to
`keys rm`/`keys pub`/`sign` — with `keys gen` the tool covers the full
key lifecycle. The NIST mode default exists because an operator using
`keys gen` + `sign` should not silently get an unsignable key.

**Acceptance criteria:**
- [ ] Against the fake transport: request body carries exactly the given
      `{type, label[, mode][, descr(b64)]}`; the kid from the reply is
      printed; a NIST type without `--mode` sends `"mode":"ECDH,ExDSA"`
      (with a stderr note), a non-NIST type without `--mode` sends no
      mode field, an explicit `--mode` is sent verbatim for any type
      (unit tests on the hem-tool-core function asserting body bytes and
      request sequence).
- [ ] An invalid `--mode` literal, missing TYPE/`--label`, or missing
      URL/passphrase → exit 2 with usage and no transport I/O; SDK label
      pre-validation failure (`EHEM_ERR_ARG`) → exit 2; device 400
      (unsupported type) and 406 (repo failure) → exit 1 with the device
      status in the message (unit tests).
- [x] Live (M5, STEP-M5-030, 2026-07-17): `keys gen ED25519 --label
      'EHEMTEST m5gen ed'` and `keys gen SECP256R1 --label 'EHEMTEST m5gen
      ec'` created keys (the SECP256R1 run printed the ECDH,ExDSA
      default-mode note, ED25519 none); both appeared in `keys list`, both
      signed via `sign` (default-alg Ed25519 / SHA256WithECDSA), `keys pub`
      read the ED25519 material, and both were removed via `keys rm
      --label-prefix 'EHEMTEST m5gen' --yes` (2 deleted, 0 protected).
