---
id: REQ-TOOL-006
title: hem-tool keys rm — removal with protected-key guard
status: approved
priority: must
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: ARCHITECTURE.md §8, §11 (M3 gate: guard refuses bulk removal); requirements/start_point/goal.txt ("allow to remove keys … including 'protected' keys"); encedo-hem-python-api wipe_keys.py (reference semantics); approved 2026-07-16; moved from encedo-hem-c-api (requirements/REQ-TOOL-006-keys-rm.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-TOOL-005", "REQ-TOOL-001", "REQ-KEY-001", "REQ-KEY-004"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli"]
---

# hem-tool keys rm — removal with protected-key guard

hem-tool SHALL provide a `keys rm` subcommand with the semantics of the
reference `wipe_keys.py`:

1. **Selection** (mutually exclusive; neither given → usage error):
   `--all` targets every key EXCEPT protected ones (REQ-TOOL-005),
   which are silently excluded; repeatable `--label-prefix P` targets
   keys whose label starts with `P`.
2. **Partition report:** before acting, print regular targets, protected
   targets, and protected keys skipped as partial matches (see 4), with
   counts. `--dry-run` stops here, deleting nothing, exit 0.
3. **Regular (non-protected) targets:** one bulk confirmation prompt;
   `--yes` skips it.
4. **Protected targets:** a protected key is targeted only when some
   given `--label-prefix` equals its label **exactly**; a partial
   (prefix-only) hit is warned about and skipped. Each protected deletion
   requires typing the literal uppercase `YES` at its own per-key prompt;
   any other input (including `y`, `yes`, EOF) skips that key. `--yes`
   is ignored for protected keys, by design — there is no non-interactive
   path to a protected deletion.
5. **Exit codes:** 0 — success, dry-run, or nothing to do; 1 — user
   abort or at least one delete failed; 2 — usage/environment error.

Connection and credentials follow the existing tool conventions
(`EHEM_URL`/`--url`, `EHEM_PASSPHRASE`/`--passphrase`). The
selection/partition/confirmation logic lives in `hem-tool-core` (shared
with the unit tests, like cert-install).

**Rationale:** the second half of the goal.txt tool milestone; the M3
gate must demonstrate that the protected-key guard refuses bulk removal.
The dangerous flows (delete is irreversible, REQ-KEY-004) get their
safety exclusively here, not in the SDK.

**Acceptance criteria:**
- [x] `--all` on a fixture repo deletes exactly the non-protected keys;
      the protected ones are neither deleted nor prompted for (unit test
      via fake transport asserting the DELETE sequence). —
      test_rm_all_deletes_nonprotected (2 regular DELETEd, TLS pair not).
- [x] `--label-prefix` partial-matching a protected label warns and skips
      it; exact label match prompts; only literal `YES` proceeds — `y`,
      `yes`, empty, and EOF all skip (unit tests with scripted stdin). —
      test_rm_prefix_partial_protected_skipped, _protected_exact_yes_deletes
      (`YES` → deletes), _protected_exact_declined (`yes` → skipped; the
      confirm accepts only literal `YES`, so y/empty/EOF skip identically).
- [x] `--yes` skips only the bulk prompt; protected prompts still appear
      (unit test). — test_rm_yes_skips_bulk_not_protected (regular auto-approved,
      protected still prompted and declined via stdin).
- [x] `--dry-run` issues no DELETE and exits 0; no selection → exit 2;
      a failed delete → exit 1 after processing the rest (unit tests). —
      test_rm_dry_run, test_rm_no_selection / _mutually_exclusive /
      _no_passphrase (exit 2), test_rm_delete_failure_continues (exit 1,
      both attempted), test_rm_bulk_declined_aborts (abort → exit 1).
- [x] Live (M3 gate): create `EHEMTEST` keys, `keys rm --label-prefix
      EHEMTEST --yes` removes them; a bulk `--all --dry-run` shows the
      device's protected keys excluded (manual/integration; no protected
      key is ever actually deleted on the dev device). —
      test_keys_rm_live (2 EHEMTEST keys created → removed → gone); manual
      `keys rm --all --dry-run` showed 3 regular targets with the TLS pair +
      `SM-S938B (Android)` excluded, nothing deleted (see step evidence).
