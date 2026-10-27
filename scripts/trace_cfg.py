# Hand-kept prose for scripts/trace_gen.py — edited before each regeneration.

# Generation stamp: what happened since the last matrix.
STAMP = (
    "Generated: 2026-10-27 (STEP-T1-040 DONE — package mode: fetch-sdk.sh + package.sh, the one-file build against the published SDK 1.2.0 package proven locally on Linux with every release assert (REQ-PKG-003 opens its C cell). Before: STEP-T1-030 — hem-tool 1.3.0: the tool's own version, `--version` with the `sdk:` line, the remedies without release assets (REQ-TOOL-025 rev 6, REQ-TOOL-024 rev 10 — meaning changes approved with the plan, re-verified in the same step). Before: STEP-T1-010/020 — the C "
    "implementation moved from encedo-hem-c-api 1.2.0 with its 27 REQ-TOOL requirements (same ids, "
    "`status: approved` = the specification; the C column is computed from the moved tags), the first "
    "conformance vectors, the process documents. The new REQs of milestone T1 (REQ-TOOL-029/030, "
    "REQ-PKG-001…004) were approved with the plan of 2026-10-27.)"
)

# Appended to the Totals line.
TOTALS_NOTE = (
    "Milestone T1 (`c-v1.3.0`) is in progress: 010–040 done, 050–070 to go; REQ-PKG-001/002/004 and "
    "REQ-TOOL-029/030 show their state from the gates and the CMake tags; REQ-PKG-003 opens with STEP-T1-040."
)

# Appended to the "implemented without a tagged test" bullet.
UNVERIFIED_NOTE = (
    "REQ-TOOL-001/002/014 (`status`, `checkin`, `reboot`) are driven live and have no `verifies:` unit "
    "test — as in the SDK repository, by convention."
)

# Deliberately open acceptance criteria, with their reasons.
OPEN_CRITERIA = (
    "the attended ones of milestone T1: the published `c-v1.3.0-rc1` binaries on Linux and Windows "
    "(REQ-PKG-003; STEP-T1-070, the user's run)."
)

# Cells set to needs-reverify by §6.2: {"REQ-ID": {"impl": "why"}}; emptied when the rework lands.
REVERIFY = {}

# REQs to detail in the console summary.
SHOW = []
