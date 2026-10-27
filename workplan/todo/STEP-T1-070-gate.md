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
  commits: []
  tests: []
  notes: null
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
- [ ] CI green on `development`; `c-v1.3.0-rc1` (user) → pre-release, 3
      assets, checksums re-verified in the job.
- [ ] Attended (the user): Linux and Windows one-file binaries — `status`
      under system trust, `--version`, `ext list`; recorded under
      REQ-PKG-003.
- [ ] Docs checklist re-verified; TRACE regenerated (C column: every
      approved REQ implemented/verified or named with its reason).
- [ ] ARCHITECTURE §7 T1 marked gate-passed; `c-v1.3.0` suggested to the
      user (merge `development` → `main`, annotated tag with the release
      notes).
