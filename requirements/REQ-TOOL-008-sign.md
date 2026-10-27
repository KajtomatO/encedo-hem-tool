---
id: REQ-TOOL-008
title: hem-tool sign — produce a signature with a device key
status: approved
priority: should
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: user decision 2026-07-16 (M4 decomposition: add hem-tool sign and keys pub); ARCHITECTURE.md §8 (thin consumer, subcommands grow with milestones), §11 (M4 gate); HEM-SDK-7 (sign); approved 2026-07-16; moved from encedo-hem-c-api (requirements/REQ-TOOL-008-sign.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-TOOL-001", "REQ-OPS-001", "REQ-KEY-003", "REQ-KEY-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool sign — produce a signature with a device key

hem-tool SHALL provide a `sign <kid>` subcommand that reads the message
from `--in FILE` or stdin (device cap: 1..2048 bytes, REQ-OPS-001),
signs it via `ehem_sign`, and writes the signature to stdout as padded
base64 by default — `--hex` for lowercase hex, `--raw` for the raw
signature bytes alone (pipeline-friendly). Options:

- `--alg ALG` — the REQ-OPS-001 selector, passed verbatim. When omitted,
  the tool fetches the key's type via `ehem_key_get` and picks the
  family's canonical selector via the REQ-KEY-006 classification
  (SECP256R1/256K1 → `SHA256WithECDSA`, SECP384R1 → `SHA384WithECDSA`,
  SECP521R1 → `SHA512WithECDSA`, ED25519 → `Ed25519`, ED448 → `Ed448`);
  a family with no signing selector (X25519/448, AES, HMAC, CERT, …) →
  exit 1 naming the type. The lookup rides the same per-KID scope token
  as the sign itself (REQ-OPS-001 shared cache), so it costs one extra
  request and no extra login.
- `--sigctx STR` — optional RFC 8032 context (UTF-8 bytes, ≤ 255) for the
  `Ed25519ctx`/`Ed25519ph`/`Ed448`/`Ed448ph` selectors.

Connection and credentials follow the existing tool conventions
(`EHEM_URL`/`--url`, `EHEM_PASSPHRASE`/`--passphrase`); exit 0 on
success, 2 on usage/environment errors (missing kid, unreadable `--in`,
empty or oversized message, oversized `--sigctx`, conflicting format
flags), 1 on runtime failure (auth, scope, device 400/406,
network) with an actionable message. The orchestration lives in
`hem-tool-core` (shared with the unit tests, like the other subcommands).

**Rationale:** user decision at M4 decomposition (2026-07-16). Living
documentation of the M4 signing path and the manual driver for the M4
gate demo (`sign` piped against `keys pub --raw` + local wolfCrypt
verify). The default-alg lookup exists because a PKCS#11-less operator
should not need to know the selector vocabulary for the common case.

**Acceptance criteria:**
- [x] Against the fake transport: message from `--in` and from stdin
      signs and prints base64; `--hex`/`--raw` switch the encoding;
      `--alg` given verbatim skips the metadata fetch (request-sequence
      assert); omitted `--alg` fetches type once and picks the documented
      default per family (unit tests on the hem-tool-core function). —
      test_sign_tool: test_sign_explicit_alg_formats (3 requests, no get),
      test_sign_default_alg_lookup (4 requests, Ed25519 +
      SHA256WithECDSA defaults), test_sign_message_sources (binary-safe
      --in incl. a NUL byte).
- [x] Default-alg on a non-signing family (fixture CURVE25519) → exit 1
      naming the type, no sign request (unit test). —
      test_sign_non_signing_family.
- [x] Empty or > 2048-byte message, unreadable `--in`, `--sigctx` > 255
      bytes, or missing kid/URL/passphrase → exit 2 with usage, no sign
      request; device 403 → exit 1 with the scope message; 406 → exit 1
      (unit tests). — test_sign_usage_errors (0 requests),
      test_sign_device_errors.
- [x] Live (M4 gate): `hem-tool sign` of a small file with an `EHEMTEST`
      ED25519 key and with an `EHEMTEST` SECP256R1 (ExDSA) key produces
      signatures that verify locally against `keys pub` material
      (manual/integration). — M4 demo 2026-07-16: default-alg notes
      printed ("using Ed25519/SHA256WithECDSA from key type"); 64-byte
      raw + 72-byte DER signatures; BOTH verified with OpenSSL (pkeyutl
      Ed25519 "Signature Verified Successfully"; dgst -sha256 "Verified
      OK") against `keys pub --raw` material; cleanup via `keys rm
      --label-prefix 'EHEMTEST tool demo' --yes` (2 deleted, protected
      untouched).
