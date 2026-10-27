# Hand-kept prose for scripts/trace_gen.py — edited before each regeneration.

# Generation stamp: what happened since the last matrix.
STAMP = (
    "Generated: 2026-10-27 (STEP-T1-070 DONE — the T1 gate passed: the user's annotated c-v1.3.0-rc1 (3c1976e) ran c-release 40549324183 → pre-release 425769575 (two archives + SHA256SUMS, re-verified in the publish job); C CI 6/6 on development (40549249453) and on the tag (40549324395); attended: the published Linux and Windows binaries run by the user (`--version`, `status` under system trust, `ext list`), the Linux archive repeated on the dev host — REQ-PKG-003 rev 2, its last two criteria ticked. The final c-v1.3.0 is the user's merge + tag. Before: STEP-T1-060 — docs and process complete (REQ-TOOL-016 rev 7 wording; risk 1 retired). Before: STEP-T1-050 — the workflows: C CI run 40529996276 6/6 legs (both SDK modes, both platforms — the Windows package leg links the SDK package with the runner's GCC), C release dry run 40530024709 on main (artifacts, publish skipped). Before: STEP-T1-040 — package mode: fetch-sdk.sh + package.sh, the one-file build against the published SDK 1.2.0 package proven locally on Linux with every release assert. Before: STEP-T1-030 — hem-tool 1.3.0: the tool's own version, `--version` with the `sdk:` line, the remedies without release assets (REQ-TOOL-025 rev 6, REQ-TOOL-024 rev 10 — meaning changes approved with the plan, re-verified in the same step). Before: STEP-T1-010/020 — the C "
    "implementation moved from encedo-hem-c-api 1.2.0 with its 27 REQ-TOOL requirements (same ids, "
    "`status: approved` = the specification; the C column is computed from the moved tags), the first "
    "conformance vectors, the process documents. The new REQs of milestone T1 (REQ-TOOL-029/030, "
    "REQ-PKG-001…004) were approved with the plan of 2026-10-27.)"
)

# Appended to the Totals line.
TOTALS_NOTE = (
    "Milestone T1 (`c-v1.3.0`) is complete: STEP-T1-010…070 done, the gate passed 2026-10-27 on "
    "`c-v1.3.0-rc1`; the final tag is the user's. No milestone is open; the next work is the SDK's M13 "
    "(encedo-hem-c-api drops hem-tool) and, here, a pin bump to SDK 1.3.0 once it ships."
)

# Appended to the "implemented without a tagged test" bullet.
UNVERIFIED_NOTE = (
    "REQ-TOOL-001/002/014 (`status`, `checkin`, `reboot`) are driven live and have no `verifies:` unit "
    "test — as in the SDK repository, by convention. REQ-PKG-001 is proven by the CI legs' pin check "
    "and the two SDK modes building, REQ-PKG-003 by the release run and the attended checks recorded in "
    "the REQ itself — neither can carry a `verifies:` unit test."
)

# Deliberately open acceptance criteria, with their reasons.
OPEN_CRITERIA = (
    "none — the attended criterion of REQ-PKG-003 was met on the published `c-v1.3.0-rc1` (2026-10-27)."
)

# Cells set to needs-reverify by §6.2: {"REQ-ID": {"impl": "why"}}; emptied when the rework lands.
REVERIFY = {}

# REQs to detail in the console summary.
SHOW = []
