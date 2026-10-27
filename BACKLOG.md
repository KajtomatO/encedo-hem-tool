# Backlog

Unscheduled work. Nothing in this file has a milestone, a requirement or a
step, and no implementation promises any of it. An item leaves this file
only by user decision into a milestone of [ARCHITECTURE.md](ARCHITECTURE.md)
§7, after which it is decomposed per
[REQUIREMENTS-MANAGEMENT.md](REQUIREMENTS-MANAGEMENT.md) §5.3.

Items 6–9, 11 and 12 moved here from encedo-hem-c-api's BACKLOG.md on
2026-10-27 (user decision: hem-tool is its own deliverable); their numbers
and dated notes are kept as they were. They concern the CLI's behaviour, so
they apply to every implementation unless marked.

## Items

### 6. `init-wizard` explains the fields it asks for

User note 2026-10-19 (from the 1.2.0-rc1 test; not analysed): the wizard
should explain some of the fields — what `origin` or `dnsd` is, why it is
needed and how it affects the HEM.

### 7. `init-wizard` readability

User note 2026-10-19 (not analysed): the wizard should be more readable —
colours, maybe a simple GUI.

### 8. Backup words: scrollback and a file

User note 2026-10-19 (not analysed): the backup words stay visible in the
terminal history (scrolling up); there should be an option to save them to
a file.

### 9. A safer way to pass the passphrase

User note 2026-10-19 (not analysed): the passphrase lands in the console
history; consider sessions — a `hem-tool login` that takes the passphrase
safely from the user — though how to store it is open. Related:
`--passphrase` is deprecated since 1.2.0 (REQ-TOOL-020 rev 2) and will be
removed; `EHEM_PASSPHRASE` stays until this item lands.

### 11. `hem-tool random` is slow

User note 2026-10-19 (not analysed): `hem-tool random` takes a long time —
investigate; update the help text or the algorithm. Does it create a key
only for itself? Perhaps keep that key instead of removing it every time:
by default create it only if absent, with options to clean up and to use
another existing key. (Background: the firmware's RNG endpoint is disabled,
so the SDK harvests IVs through crypto calls, which need a key.)

### 12. `[c]` hem-tool on Windows: device strings in the console's OEM code page

Found 2026-10-20 while fixing the `ext pair` QR display (not analysed):
hem-tool writes UTF-8 to stdout, and a Windows console decodes it in its
OEM code page — a non-ASCII key label, user name or hostname from the
device (the device and the Manager are UTF-8) shows as mojibake there, and
what the wizard reads from the console comes back in that code page. The
QR fix bypasses the code page for the QR rows only. Options: switch the
console to UTF-8 for the process (`SetConsoleOutputCP(65001)` +
`SetConsoleCP(65001)`, restored at exit — also on Ctrl-C, as the prompt
helper restores the echo), or a wide-char console path for all console
I/O.

## Candidates (no decision taken)

- **Other implementations** (JavaScript on hem-sdk-js, Python on
  encedo-hem-python-api) — the reason this repository is laid out as it is;
  none planned. A second implementation also triggers converting the C
  eJWT/certificate test fixtures into `conformance/` vectors.
- **`hem-tool fw-upgrade`** — the CLI over the SDK's `system/upgrade`
  family, which is itself unbound (encedo-hem-c-api BACKLOG items 1–2).
- **`diag/*`** — a hem-tool-only implementation is one of the two candidate
  shapes (encedo-hem-c-api BACKLOG item 4); the SDK stays diag-free either
  way.

## Provenance

Items 6–12 were user notes of 2026-10-19/20 in encedo-hem-c-api; item 10
of that file ("Split hem-tool from the SDK") became this repository.
Numbers 1–5 and 10 are not reused here.
