---
id: REQ-TOOL-018
title: hem-tool --mobile — mobile auth for every bearer-needing subcommand
status: approved
priority: should
revision: 4   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 3 = 
source: user decision 2026-08-06 (M9 scope reshape, ARCHITECTURE.md §11; user's working name "--app-auth", finalized "--mobile" to match ehem_login_mobile and the start_point HEM-CFG-3 vocabulary `auth = passphrase | mobile`); rev 2 = STEP-M9-020 no-pairing rework (user decision 2026-08-06: "timeout + hint" — nothing-paired is not detectable in pure mobile mode) + exit codes 13/14 recorded; rev 3 = STEP-M9-057 push notices (user request 2026-08-06); moved from encedo-hem-c-api (requirements/REQ-TOOL-018-mobile-auth-flag.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-AUTH-006", "REQ-AUTH-010", "REQ-TOOL-016"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli", "ARCHITECTURE.md#7-milestones"]
---

# hem-tool --mobile — mobile auth for every bearer-needing subcommand

Every hem-tool subcommand that needs a bearer token SHALL accept a
global `--mobile` flag selecting mobile push confirmation
(`ehem_login_mobile`) instead of passphrase login.

- **One login chokepoint:** a shared helper in hem-tool-core replaces
  the 13 scattered `ehem_login(ctx, o->passphrase)` call sites; every
  subcommand routes through it, so `--mobile` (and any future auth
  mode) lands in exactly one place.
- **Precedence:** `--mobile` overrides an `EHEM_PASSPHRASE` from the
  environment (hem.env is commonly auto-sourced by `./dev`); passing
  `--mobile` together with an explicit `--passphrase` flag is a usage
  error.
- **Passphrase-only commands:** `ext pair` rejects `--mobile` with a
  message naming the reason — the device demands `sub="U"` for the
  pairing trio (REQ-AUTH-006), and mobile bearers carry
  `sub=base64(kid)` (REQ-AUTH-007). No-auth commands (`status`,
  `checkin`) ignore the flag like they ignore `--passphrase` today.
  *(Rev 3, 2026-10-09 — M10 finding, REQ-TOOL-019: `cert-install` and
  `tls-recover` are passphrase-only as well — their install legs are
  `POST /api/system/config`, which firmware v1.2.2 allows for `sub` U/M
  only (`api_system.c:1060-1066`); with `--mobile` they would have
  failed with 403 at the install leg. `reboot` is unaffected (scope-only
  check). The rejection is now generic: main.c checks the registry's
  auth class before any traffic — `hem_tool_check_auth_class` — so
  "every bearer-needing subcommand" means every command whose class is
  BEARER; PASSPHRASE_ONLY commands refuse the flag up front.)*
- **Confirmation semantics** (rev 2): `--timeout SEC` maps to
  `ehem_options.confirm_timeout_ms` (default 60 s); rejected-on-phone
  and timeout produce distinct tool-wide exits on every subcommand —
  **13 = timeout, 14 = rejected** (above every per-command vocabulary;
  `ext login` keeps its older documented 3/4/5). **Nothing-paired is
  NOT separately detectable in pure mobile mode** — the push goes to
  nobody and the confirm expires (`ext login`'s distinct exit 5 relies
  on a passphrase-assisted pre-check) — so it surfaces as the timeout
  exit whose message hints at `hem-tool ext list` (user decision
  2026-08-06: "timeout + hint", no per-command pre-check — avoids an
  extra login + full key walk per run on the slow device).
- Scope behavior is the SDK's own (REQ-AUTH-010): one push per scope
  needing a bearer — a multi-scope command (e.g. `cert-install`) may
  push more than once; the tool documents this in its help.

**Rationale:** with a phone paired, no command should force the
passphrase onto a command line — this is HEM-CFG-3's
`auth = passphrase | mobile` made real in the reference consumer, and
the 1.0 polish item the user requested 2026-08-06.

**Acceptance criteria:**
- [x] Unit (hem-tool-core, scripted broker/fake transport per the
      test_ext_tool.c pattern): the shared helper routes passphrase vs
      mobile (both lazy, zero traffic); missing credentials → ARG/USAGE
      naming `--mobile`; `ext pair --mobile` → the sub="U" error with
      zero traffic; approve / reject(14) / timeout(13, with the
      `ext list` hint) proven on `keys list --mobile` against a
      scripted broker, with mobile outranking an environment-style
      passphrase. — tests/unit/test_tool_auth.c (2026-08-06); the
      `--mobile --passphrase` usage error lives in main.c argument
      parsing (exe-only) and is proven by the live smoke below.
- [x] All bearer-needing subcommands (keys list/pub/gen/rm/update,
      sign, random, logs list/get/key, selftest, cert-install,
      tls-recover, reboot, ext list) accept `--mobile`; grep shows
      ZERO `ehem_login(` call sites outside the chokepoint — even
      `ext pair` and `ext login`'s pre-check route through it with
      mobile=false (2026-08-06). Offline smoke: `--mobile --passphrase`
      → exit 2; `ext pair --mobile` → exit 2 + sub="U" message;
      credential-less `keys list` → exit 2 naming `--mobile`;
      passphrase-mode `keys list` live-green against my.ence.do.
- [x] Live (ATTENDED, M9-060 gate, 2026-08-06, real phone SM-S938B):
      `keys list --mobile` approved → push notice + listing, exit 0 —
      at measured +4 s device drift (the STEP-M9-055 recovery window);
      immediate second run rejected on the phone → exit 14. Closes the
      criterion; broker pushes were never sent unattended.

**Rev 3 (STEP-M9-057, user request 2026-08-06):** under `--mobile` the
tool announces EVERY push on stderr — `mobile: push sent — approve
"<scope>" on your phone (waiting up to N s)` — one line per scope
acquisition (multi-scope commands explain each push), via the
REQ-AUTH-010 confirm_notice hook set in make_ctx. `ext login` keeps its
own richer push line (the hook is set only under --mobile).
