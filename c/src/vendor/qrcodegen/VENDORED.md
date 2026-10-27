# Vendored: qrcodegen (QR Code generator library, C)

- **Upstream:** https://github.com/nayuki/QR-Code-generator
- **Version:** 1.8.0 (git tag `v1.8.0`)
- **License:** MIT (header block at the top of `qrcodegen.c` / `qrcodegen.h`)
- **Files vendored:** `qrcodegen.c`, `qrcodegen.h`
- **Retrieved:** 2026-07-23 from
  `https://raw.githubusercontent.com/nayuki/QR-Code-generator/v1.8.0/c/`

## Provenance / integrity

SHA-256 of the files as vendored (unmodified upstream):

```
300eff07ee25baaa7578f20284411638154716379437391e7e689c0e6ce81403  qrcodegen.c
e82df4bff37d18b5863b9e7486fe6bda1b6cda8c3b9ecebfec473907265cb589  qrcodegen.h
```

## Why vendored (REQ-TOOL-016)

`hem-tool ext pair` renders the pairing QR code directly in the terminal so
a registration completes without any external tool — and deliberately
WITHOUT a web QR service (the tester's quickchart.io approach ships the
user/email/hostname payload to a third party). Two-file, MIT, no
dependencies — the cJSON vendoring pattern (REQ-BUILD-003).

## Symbol containment (REQ-API-006)

TOOL-ONLY: compiled into `hem-tool-core` (static, CLI + tests), never into
`libencedo-hem`. The export and public-header gates prove the SDK library
stays free of it; no `qrcodegen` symbol or header is reachable from
`include/ehem/`.

## Local modifications

None. Vendored verbatim so a future version bump is a clean re-download.

## Updating

1. Re-download both files from the new tag's raw URL.
2. Update the version, retrieval date, and SHA-256 lines above.
3. Rebuild and run `ctest -L unit` — `test_ext_tool` exercises the encoder
   against a fixed payload.
