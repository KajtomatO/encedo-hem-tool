---
id: STEP-T1-060
title: "Docs and process — the REQ set complete (REQ-TOOL-029/030, REQ-PKG-001…004, the REQ-TOOL-024/025 revs), REQUIREMENTS-MANAGEMENT with the per-implementation rule, ARCHITECTURE, READMEs, c/docs/RELEASING.md, KNOWN-ISSUES, BACKLOG, scripts/trace_gen.py and the first TRACE matrix"
milestone: T1
implementation: c
implements: []
traces:
  architecture: ["ARCHITECTURE.md#2-specification-and-conformance", "ARCHITECTURE.md#7-milestones"]
depends_on: ["STEP-T1-050"]
evidence:
  commits: ["39b87d5", "1abb352"]   # start; the initial import carried the process files, READMEs, REQ set, generator; this step's own commit follows
  tests:
    - "2026-10-27: `python3 scripts/trace_gen.py requirements/TRACE.md` — 33 approved REQs, C column 27 verified / 5 implemented (TOOL-001/002/014 live-only; PKG-003 and the gate-only ones until the tag) / 1 in progress (REQ-PKG-003 → STEP-T1-070), no orphan tag, no broken anchor. REQ-TOOL-016 rev 7: the encoder bullet split into the shared rule and the C sentence (wording only). The live-test skip path checked: integration and disruptive tests report Skipped (77) without their gates (REQ-PKG-004 criterion 1 ticked)."
  notes: "Chore. Most of the content landed with the initial import (1abb352): REQUIREMENTS-MANAGEMENT with the per-implementation rule, ARCHITECTURE §1–§8, root README and c/README, c/docs/RELEASING.md, KNOWN-ISSUES and BACKLOG with provenance, the 27 imported REQs + REQ-TOOL-029/030 + REQ-PKG-001…004 (approved with the plan 2026-10-27), scripts/trace_gen.py + trace_cfg.py. This step finished the REQ-TOOL-016 wording, retired ARCHITECTURE §8 risk 1 by the first CI run, and re-checked every document against the shipped shape."
reopened: []
cancelled: null
---

**Goal:** the repository explains itself: what hem-tool is, which
implementations exist and their state, how the shared specification and
the conformance vectors govern them, how the C implementation is built,
tested and released, and the full requirements set with a reproducible
TRACE matrix (REQ × implementation) from the committed generator.

**Notes:** chore (`implements: []`) — documentation and process. The
REQ-TOOL-016 vendoring clause becomes a C-only sentence (`applies_to`
on the clause's REQ-PKG counterpart rather than the whole REQ). BACKLOG
items 6, 7, 8, 9, 11, 12 of encedo-hem-c-api move here with their
provenance; KNOWN-ISSUES takes the tool sections of the SDK's file
(the Windows QR display; the ext pair hash; the 1.1.0 wolfSSL issues as
history). `scripts/trace_gen.py` replaces the SDK-side scratchpad
generator — committed, run as `python3 scripts/trace_gen.py
requirements/TRACE.md`.

**Definition of done**
- [x] REQ-TOOL-029, REQ-TOOL-030, REQ-PKG-001…004 drafted and approved (the
      plan approval of 2026-10-27 counts); REQ-TOOL-024/025 revs applied
      (STEP-T1-030); REQ-TOOL-016 rev 7 (the encoder clause).
- [x] README (root): implementations table, Download, usage; `c/README.md`:
      build/test from source, the two SDK modes, the pin.
- [x] ARCHITECTURE: §1–§8 per the plan (§8 risk 1 retired by the first CI
      run); REQUIREMENTS-MANAGEMENT: areas TOOL/PKG, `applies_to`, the
      per-implementation matrix rule, code/test roots per implementation,
      §8 ground truth incl. encedo-hem-c-api.
- [x] `c/docs/RELEASING.md`: variants, the pin-bump procedure, rc and
      final procedures, attended checks.
- [x] KNOWN-ISSUES and BACKLOG populated with provenance.
- [x] `scripts/trace_gen.py` committed; `requirements/TRACE.md` generated:
      no orphan tags, no broken anchors, the C column complete but for
      REQ-PKG-003's tag half (STEP-T1-070).
