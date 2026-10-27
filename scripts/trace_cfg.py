# Hand-kept prose for scripts/trace_gen.py — edited before each regeneration.

# Generation stamp: what happened since the last matrix.
STAMP = (
    "Generated: 2026-10-27 (the repository's first matrix — STEP-T1-010/020 in progress: the C "
    "implementation moved from encedo-hem-c-api 1.2.0 with its 27 REQ-TOOL requirements (same ids, "
    "`status: approved` = the specification; the C column is computed from the moved tags), the first "
    "conformance vectors, the process documents. The new REQs of milestone T1 (REQ-TOOL-029/030, "
    "REQ-PKG-001…004) were approved with the plan of 2026-10-27.)"
)

# Appended to the Totals line.
TOTALS_NOTE = (
    "Milestone T1 (`c-v1.3.0`) is in progress; the cells of the REQ-TOOL set show `in progress` until "
    "STEP-T1-010/020 close (their tags moved with the code and tests)."
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
