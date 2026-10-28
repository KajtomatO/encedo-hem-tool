---
id: STEP-T1-070
title: "T1 gate — c-v1.3.0-rc1 published from the user's tag, the attended checks on both platforms, the docs checklist, TRACE; then c-v1.3.0"
milestone: T1
implementation: c
implements: []
traces:
  architecture: ["ARCHITECTURE.md#7-milestones"]
depends_on: ["STEP-T1-010", "STEP-T1-020", "STEP-T1-030", "STEP-T1-040", "STEP-T1-050", "STEP-T1-060"]
evidence:
  commits: ["3c1976e"]   # start; the done commit (this step's records) follows
  tests:
    - "2026-10-27: C CI 40549249453 on development (3c1976e) 6/6; the user's annotated c-v1.3.0-rc1 on the same commit: C CI 40549324395 6/6 and c-release 40549324183 (Linux 0:30, Windows 2:18, publish 0:12; every package.sh assert green) → pre-release 425769575 'hem-tool 1.3.0-rc1' with hem-tool-1.3.0-rc1-linux-x86_64.tar.gz, hem-tool-1.3.0-rc1-windows-x86_64.zip and SHA256SUMS (re-verified in the publish job); release text = the tag's annotation + the checksums."
    - "2026-10-27: attended — the user ran the published Linux and Windows binaries (`--version`, `status` under system trust, `ext list`): 'It works'. Linux repeated on the dev host from the downloaded archive: checksum OK, glibc-only, no RUNPATH, `--version` naming SDK 1.2.0 with both libraries `static`, `status` → firmware v1.2.2-DIAG exit 0, `ext list` → the paired phone exit 0. Recorded under REQ-PKG-003 (rev 2; its last two criteria ticked)."
    - "2026-10-27: `python3 scripts/trace_gen.py requirements/TRACE.md` — 33 approved REQs; C column: 28 verified / 5 implemented (TOOL-001/002/014 live-only; PKG-001 proven by the pin check of the CI legs, PKG-003 by the release run and the attended checks — no `verifies:` unit test can) / 0 in progress / 0 open; no orphan tag, no broken anchor. The one `in progress` cell the 060 matrix carried was REQ-TOOL-029 (its gate check_ascii_strings.c had only a `verifies:` tag, no code tag), not REQ-PKG-003 as the 060 note says; the gate now carries `implements:` too, like the other three."
  notes: "Gate passed 2026-10-27. Docs checklist: root README Status → hem-tool 1.3.0 (2026-10-27) and the implementations table; ARCHITECTURE §7 gate passed; c/docs/RELEASING.md gained the observed rc1 timings; c/README.md and KNOWN-ISSUES.md needed nothing (no version-bound text). The final c-v1.3.0 is the user's: merge development → main, annotated tag from the drafted release notes; its run and release id are recorded afterwards under REQ-PKG-003 as the SDK does."
reopened: []
cancelled: null
---

**Goal:** hem-tool 1.3.0 ships from this repository: the user's annotated
`c-v1.3.0-rc1` publishes a pre-release with `hem-tool-1.3.0-rc1-{linux,windows}-x86_64`
and `SHA256SUMS`; the user runs the published binaries as a user would
(`status` under system trust on Linux and Windows, `--version` showing
`sdk:       encedo-hem 1.2.0`, `ext list`); findings are fixed on
`development` and re-tagged; then the final `c-v1.3.0` on `main`.

**Notes:** milestone gate (chore). The docs checklist of the SDK's process
applies here: root README Status/implementations table, `c/README.md`,
KNOWN-ISSUES, `c/docs/RELEASING.md` "Final release". Tagging, merging and
pushing are the user's. SDK milestone M13 (encedo-hem-c-api) depends on
this gate — the SDK's gate requires `c-v1.3.0` published.

**Definition of done**
- [x] CI green on `development`; `c-v1.3.0-rc1` (user) → pre-release, 3
      assets, checksums re-verified in the job. *(2026-10-27: CI
      40549249453; the tag's c-release 40549324183 → pre-release 425769575.)*
- [x] Attended (the user): Linux and Windows one-file binaries — `status`
      under system trust, `--version`, `ext list`; recorded under
      REQ-PKG-003. *(2026-10-27: "It works" on both; the Linux archive
      repeated on the dev host — REQ-PKG-003 rev 2.)*
- [x] Docs checklist re-verified; TRACE regenerated (C column: every
      approved REQ implemented/verified or named with its reason). *(README
      Status 1.3.0; RELEASING timings; TRACE 28 verified / 5 implemented,
      each implemented cell named with its reason.)*
- [x] ARCHITECTURE §7 T1 marked gate-passed; `c-v1.3.0` suggested to the
      user (merge `development` → `main`, annotated tag with the release
      notes). *(Gate PASSED 2026-10-27; the tag command and the notes
      handed over with this step's close.)*
