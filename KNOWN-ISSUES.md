# Known issues

What users of the released hem-tool binaries hit, and what was fixed when.
Device-side issues (firmware behaviour the SDK works around) are recorded in
the SDK's [KNOWN-ISSUES.md](https://github.com/KajtomatO/encedo-hem-c-api/blob/main/KNOWN-ISSUES.md);
this file covers the CLI. Entries tagged `[c]` concern the C implementation.
The first three entries came with the sources from encedo-hem-c-api at
1.2.0 (2026-10-27).

## FIXED in 1.2.0 `[c]` — 1.1.0 `ext pair`: the new Encedo Auth app rejects the QR

**Affected:** `hem-tool ext pair` in 1.0.0 and 1.1.0. **Symptom.** The new
Encedo Auth app (2.0.0-dev.8 and later) scans the terminal QR and refuses:
"the pairing link served a different request than the code promised". The
Manager's QR pairs. **Cause.** The payload's `hash` is a commitment the
app verifies: it fetches `link`, hashes the `request` the broker serves and
compares. hem-tool sent the placeholder `"not_implemented_yet"`, which the
older app never checked. **Fix.** The payload is composed by the SDK
(`ehem_ext_qr_payload`, encedo-hem-c-api 1.2.0) with `hash` =
base64(SHA-256(request)) — pinned here as `conformance/qr-payload.json`
(REQ-TOOL-016). 1.0/1.1 users pair from the Manager or upgrade.

## FIXED in 1.2.0 `[c]` — 1.1.0 `ext pair` on Windows: the terminal QR is "a mess of chars"

**Affected:** `hem-tool.exe ext pair` in 1.0.0, 1.1.0 and the 1.2.0
candidates up to rc3, in every Windows console whose output code page is
the default OEM one. **Symptom.** Rows of letters and symbols instead of a
QR code, three per cell (`Γûê` in code page 437, `Ôľł` in 852). Output
redirected to a file, and every Linux terminal, were fine. **Cause.** The
render wrote UTF-8 half-block characters through stdio; a Windows console
decodes the bytes it receives in its OUTPUT code page, never UTF-8 unless
`chcp 65001` was run. **Fix.** When stdout is a console the rows are
written as UTF-16 through the console API (`WriteConsoleW`); a file or pipe
still receives the UTF-8 bytes. Other device strings hem-tool prints still
go through the code page: BACKLOG item 12.

## HISTORY `[c]` — 1.1.0 release hem-tool: a foreign libwolfssl crashed it, a missing libwolfssl.dll stopped it before `main`

The 1.0/1.1 binaries linked wolfSSL dynamically and the Linux one aborted
("stack smashing detected") when it loaded a system `libwolfssl.so.42`
other than the companion asset's; on Windows a missing `libwolfssl.dll`
stopped `hem-tool.exe` before its own code ran (exit `0xC0000135`). 1.2.0
added the SDK's library check (exit 12 with a message) and a delay-load
import on Windows. From hem-tool 1.3.0 the released binary is the one-file
build only — OpenSSL and libcurl linked in, no companion library — so
neither condition can arise; the SDK's check still guards a from-source
build against a mismatched library (REQ-TOOL-024).
