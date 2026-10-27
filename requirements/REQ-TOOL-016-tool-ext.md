---
id: REQ-TOOL-016
title: hem-tool ext family — pair (terminal QR), list, login
status: approved
priority: should
revision: 7   # 2026-10-27 (STEP-T1-060): the QR-encoder bullet split into the shared rule (a local encoder, never a web service; the encoder belongs to the tool) and a C-only sentence (the vendored Nayuki qrcodegen in hem-tool-core) — wording only, no meaning change. Rev 6 = 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 5 = 2026-10-27 (STEP-M12-137 done): re-verified — the rev 3 unit criterion green on every CI leg (d532433 and a481d07) and both attended criteria met: the user's rc3 tests of 2026-10-20 (the Windows QR display the only finding) and, 2026-10-27, "Ext pair works on windows" from the published `v1.2.0-rc4` Windows hem-tool with Encedo HEM Auth 2.0.0-dev.8 — `ext list` shows the Samsung SM-S938B (Android) authenticator, kid 3dff0e4888f77b1e14e6812c17dbc4ce, [PROTECTED]. No meaning change. Rev 4 = 2026-10-20 (STEP-M12-137, the rc3 Windows finding): the terminal QR on a Windows console — the UTF-8 half-block rows showed as mojibake ("a mess of chars") because the console decodes stdout bytes in its OEM code page; `ext pair` now writes the rows as UTF-16 through the console API when stdout is a console (bytes otherwise). Annotation of the rendering clause + an attended criterion on `v1.2.0-rc4` — NO meaning change (the clause already promised a QR in the terminal; Windows showed none); status stays needs-reverify from rev 3. Rev 3 = 2026-10-20 (STEP-M12-137): MEANING CHANGE — the QR payload comes from the SDK's `ehem_ext_qr_payload()` (REQ-AUTH-008 rev 5: `hash` = base64(SHA-256(request)), the check the new Encedo Auth app makes); the tester's placeholder "not_implemented_yet" is gone from the flow text. §6.2 report approved with the plan 2026-10-20. needs-reverify until the unit criterion passes and the user's rc3 scan. Rev 2 = the M8-080 attended record (2026-08-05/06)
source: user decision 2026-07-22 (M8 decomposition; "Add QR code generation to hem-tool" — user decision same day); hem-api-tester test_5.php QR payload (its `hash` placeholder — superseded by REQ-AUTH-008 rev 5, the Manager's definition); REQ-TOOL-012 family-in-one-REQ precedent; user bug report 2026-10-20 on v1.2.0-rc2 (the new Encedo Auth app refuses the QR: "the pairing link served a different request than the code promised") and user decision the same day: the SDK composes the payload; user bug report 2026-10-20 on v1.2.0-rc3 (Windows: "the QR code is a mess of chars and not a QR-Code"); moved from encedo-hem-c-api (requirements/REQ-TOOL-016-tool-ext.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-AUTH-006", "REQ-AUTH-008", "REQ-AUTH-009", "REQ-AUTH-010", "REQ-KEY-001", "REQ-TOOL-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool ext family — pair (terminal QR), list, login

hem-tool SHALL provide an `ext` subcommand family — `ext pair`,
`ext list`, `ext login` — making the ExtAuth surface drivable by hand,
including rendering the pairing QR code directly in the terminal.

- **`ext pair [--label-note <s>]`:** orchestrates registration
  end-to-end: login (passphrase; pairing needs `sub=U`) → device eid via
  config → broker `session` → `ehem_ext_init` → broker `register/init`
  → the QR payload from the SDK's `ehem_ext_qr_payload()` (REQ-AUTH-008
  rev 5: `{link, hash, user, email, hostname}` in the Manager's order,
  `hash` = base64(SHA-256(request)) — the phone checks it against the
  request the link serves; the tool composes nothing itself) → **render
  it as a QR code in the terminal** (UTF-8 half-block cells; on a
  Windows console the same rows go out as UTF-16 through the console
  API — the console decodes stdout bytes in its OEM code page, which
  made mojibake of the UTF-8 cells (rev 4); a file or pipe gets the
  bytes; `--no-qr` prints the JSON payload only) → poll
  `register/check` (bounded, ~60 s, tester cadence) → `ehem_ext_validate`
  → broker `register/finalise` → print the new `kid` + label. Distinct
  nonzero exit codes: scan timeout, slots-full/dedup 406, broker error,
  auth error (the REQ-TOOL-003 style).
- **QR encoder:** the QR is encoded locally, by code that is part of the
  tool — NOT rendered via any web service: the hem-api-tester's
  quickchart.io approach ships `user`/`email`/`hostname` to a third party
  and is explicitly rejected. *C implementation:* a vendored single-file
  MIT encoder (Project Nayuki `qrcodegen`, C port) under
  `c/src/vendor/qrcodegen/` — verbatim upstream plus a provenance note,
  compiled into hem-tool-core only; it never entered the SDK's
  `libencedo-hem` (the SDK's header/export gates proved that while the
  tool lived there).
- **`ext list`:** `ehem_key_list` walk filtered to keys whose descriptor
  carries the `EXTAID` prefix; prints kid, label, and the pid (base64 of
  the 32-byte descriptor suffix). Real phones label themselves (e.g.
  "… (iPhone)") — such labels hit the REQ-TOOL-005 protected classifier,
  so `keys rm --all` already refuses them; `ext list` marks
  `[PROTECTED]` the same way `keys list` does. Unpairing stays
  `keys rm` (protected ritual included) — no `ext rm`.
- **`ext login [--scope <s>] [--timeout <sec>] [--note <s>]`:**
  demonstrates the blocking mobile flow: `ehem_login_mobile` + one
  authenticated call (default scope `system:config`, default timeout
  60 s) → prints the outcome. Exit codes: 0 approved (+ config summary),
  distinct codes for rejected, timeout, no-paired-authenticator, and
  unreachable — the rejected/timeout distinction visible to scripts is
  the point (HEM-SDK-8 made observable).
- All subcommands are public-API-only (the standing hem-tool rule);
  broker access goes through the REQ-AUTH-008 client.

**Rationale:** the tool is the living documentation of the flow and the
vehicle for STEP-M8-080's attended run: `ext pair` + a phone is how the
user actually onboards, and `ext login`'s exit codes let the attended
transcript prove USER_REJECTED vs CONFIRM_TIMEOUT end-to-end. Terminal
QR (user decision 2026-07-22) removes the last external tool from the
loop.

**Acceptance criteria:**
- [x] Unit (hem-tool-core, tests/unit/test_ext_tool.c, 2026-07-23):
      pair happy path hits the legs in order (incl. TWO login exchanges
      — config + auth:ext:pair scopes) and passes `{kid, code}` verbatim
      to finalise; scan-timeout / 406 exit codes; list EXTAID filter +
      protected marking; login exit codes for approved, rejected,
      timeout, NOAUTH (scripted broker).
- [x] Unit (same file): QR v1 module matrix (line count + all-light
      quiet rows) against the vendored qrcodegen; >80-col renders are
      refused so the caller falls back to payload print (a 1.2 kB text
      exercises it); observed pairing payloads land ≈ v10-13 ≤ 77 cols.
- [x] Export/header gates green with qrcodegen vendored (tool-only
      linkage proven by the standing check suite, 2026-07-23).
- [x] Live (attended, STEP-M8-080, 2026-08-05): `ext pair` scanned from
      the TERMINAL QR by the real Encedo app completed registration
      (phone label "SM-S938B (Android)"); `ext list` shows it with the
      `[PROTECTED]` mark (the (Android) classifier — real phones are
      guarded from bulk rm exactly as designed); `ext login`
      demonstrated approve/reject/timeout as exit 0/4/3 back-to-back.
      Pairing KEPT on the device (user decision 2026-08-06) for future
      mobile testing. *(That app did not check `hash` — the new Encedo
      Auth app does; see the rev 3 criteria.)*
- [x] Unit (test_ext_tool.c, rev 3 — local 2026-10-20: `./dev ci` 56/56
      gcc + clang, `./dev ci --openssl` 56/56 gcc + clang, ASan both
      backends; CI follows with the push): the `--no-qr` print of the pair
      happy path is the SDK payload for the scripted request, verbatim —
      `{"link":"https://l.ink/x","hash":"BQixETmS1vjcSjAPqjT0yq9poZjfeTnWczNU6VHYuI4=","user":"Alice","email":"a@example.com","hostname":"hem.example"}`
      (base64(SHA-256("r.j.w")): the request reaches the SDK unaltered),
      on its own line, and `not_implemented_yet` appears nowhere.
- [x] Attended (rev 3, STEP-M12-137 / the M12 gate): `ext pair` from the
      published `v1.2.0-rc3` hem-tool on Linux, scanned from the terminal
      QR by the NEW Encedo Auth app, completes the registration (date,
      app/phone, kid; `ext list` shows it) — recorded here and under
      REQ-AUTH-008. **Met:** the user's rc3 report of 2026-10-20
      ("Finished tests on RC") named the Windows QR display as the only
      finding; the pairing on the device is the rev 4 record below (one
      paired authenticator).
- [x] Attended (rev 4, STEP-M12-137 / the M12 gate): `ext pair` from the
      published `v1.2.0-rc4` Windows hem-tool shows a scannable QR in a
      Windows console as it comes (cmd, PowerShell or Windows Terminal;
      no `chcp`) and the new Encedo Auth app pairs from it (date,
      console, app/phone, kid) — recorded here. **Met 2026-10-27** — the
      user: "Ext pair works on windows" (the published rc4 Windows
      hem-tool); app **Encedo HEM Auth 2.0.0-dev.8**; `hem-tool ext list`:
      `3dff0e4888f77b1e14e6812c17dbc4ce  Samsung SM-S938B (Android)
      [PROTECTED]`, "1 paired authenticator" — the `(Android)` label lands
      in the protected classifier as designed. *(rc3, 2026-10-20: the
      user's Windows run showed "a mess of chars" — the UTF-8 rows
      decoded in the console's OEM code page. The byte path is unchanged
      and unit-tested; the console path needs a console and is
      attended-only — no Windows or Wine on the dev host.)*
