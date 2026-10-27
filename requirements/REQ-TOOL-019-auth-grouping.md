---
id: REQ-TOOL-019
title: hem-tool auth-requirement transparency in the command listing
status: approved
priority: should
revision: 3   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 2 = 
source: user decision 2026-08-06 (M9 scope reshape, ARCHITECTURE.md §11); rev 2 = user decision 2026-10-07 (M10: "manual recovery" help section; init-device / wipe-device / recovery added) — meaning change, status reset to draft per §3.3, §6.2 reported in chat 2026-10-07; encedo_firmware api_system.c:1060-1066 (config POST demands sub U/M); approved 2026-10-07 (M10 decomposition, user go-ahead); moved from encedo-hem-c-api (requirements/REQ-TOOL-019-auth-grouping.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-TOOL-018"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli", "ARCHITECTURE.md#7-milestones"]
---

# hem-tool auth-requirement transparency in the command listing

hem-tool's top-level command listing SHALL group commands by their
authentication requirement — **none**, **any bearer** (passphrase or
`--mobile`), and **passphrase-only** — followed by a separate trailing
section **"manual recovery"** that lists `cert-install` and
`tls-recover` (the building blocks `recovery` drives) in place of their
auth group (rev 2, user decision 2026-10-07).

- Membership (derived from device facts, finalized in code; rev 2
  additions marked M10):
  - **none:** `status`, `checkin`, `init-device` (M10 — the passphrase
    it takes is input data, not a credential);
  - **any bearer, mobile-capable:** `keys list/pub/gen/rm/update`,
    `sign`, `random`, `logs list/get/key`, `selftest`, `reboot`,
    `ext list`, `ext login` (mobile by definition);
  - **passphrase-only:** `ext pair` (the device demands `sub="U"` for
    the pairing trio, REQ-AUTH-006 — mobile bearers carry
    `sub=base64(kid)` and cannot manage pairings); M10: `wipe-device`
    and `recovery` (config writes demand `sub` U/M — see the open
    criterion);
  - **manual recovery (trailing section):** `cert-install`,
    `tls-recover` — each still carries its auth class in per-command
    help (REQ-TOOL-020).
- The grouping is **data**, not prose: each command's auth class and
  its section live in the shared command registry in hem-tool-core (the
  same single source of truth REQ-TOOL-020's per-command help renders),
  so the listing can never drift from what the commands actually
  enforce.

**Rationale:** the user asked (2026-08-06) for the tool to be honest
about which commands need which access — today a user discovers
credential requirements only by hitting an auth error. Grouping the
listing makes the tool's security model legible at a glance and
documents the one real device constraint (pairing is passphrase-only).

**Acceptance criteria:**
- [x] Top-level help renders the three groups with meaningful headers
      (none / bearer: "passphrase or --mobile" / passphrase-only:
      "the device demands sub=\"U\""), in that order
      (test_registry.c test_auth_classes, 2026-08-06).
- [x] Unit: every registered command carries an auth class and full
      descriptions (test_registry_complete walks the table — a new
      command with missing fields fails); the rendered listing places
      status under none and ext pair under passphrase-only
      (test_registry.c, 2026-08-06).
- [x] The auth class shown matches enforced behavior, one command per
      group: status/checkin never log in (no login call), keys list
      accepts either credential (test_tool_auth.c mobile run), ext pair
      rejects --mobile (test_tool_auth.c) — spot-checks in
      test_registry.c test_auth_classes (2026-08-06).
- [x] Rev 2 (M10): the listing renders the "manual recovery" section
      after the three auth groups with `cert-install` and `tls-recover`
      in it and nowhere else; `recovery`, `wipe-device`, `init-device`
      appear in their auth groups as they land (STEP-M10-030/040/060);
      the section membership is registry data (`hem_command.section`).
      *(STEP-M10-050, 2026-10-09: tests/unit/test_registry.c
      test_manual_recovery_section — section + class data, listing order,
      no occurrence above the header, reboot still in the bearer group,
      per-command help wording; tests/unit/test_tool_auth.c
      test_auth_class_guard — `hem_tool_check_auth_class` refuses --mobile
      for every PASSPHRASE_ONLY command with the sub="U"/"M" reason and
      passes bearer / no-auth / unknown commands; main.c calls it from
      the registry before any traffic.)*
- [x] RESOLVED by firmware read (STEP-M10-050, 2026-10-09; the device
      cert is currently valid, so no live `--mobile` probe was possible):
      `api_post_system_config` (`api_system.c:1052-1066`) requires scope
      `system:config` AND token `sub` ∈ {U, M} — a mobile bearer
      (`sub=base64(kid)`) gets 403 — so `cert-install` (cert install =
      config write) and `tls-recover` (bundle install = config write) are
      **passphrase-only**; rev 1 listed them as mobile-capable in error.
      `api_get_system_reboot` (`api_system.c:2174-2213`) checks scope only
      (config / upgrade / shutdown) with no `sub` test, so `reboot` stays
      mobile-capable. Registry classes changed accordingly; the guard
      (`hem_tool_check_auth_class`, enforced in main.c from the registry
      data) refuses `--mobile` for every PASSPHRASE_ONLY command before any
      traffic. Recorded in REQ-TOOL-018 rev 3.
