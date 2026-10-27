---
id: REQ-TOOL-030
title: Conformance vectors — language-neutral test data under conformance/ that every implementation checks itself against
status: approved
priority: must
revision: 1   # 2026-10-27: drafted with the split plan; approved with it (user decision: one requirements set for several implementations)
source: user decision 2026-10-27 (ARCHITECTURE.md §1 item 2, §2); the QR payload known answers of encedo-hem-c-api tests/unit/test_notify.c and test_ext_tool.c at 1.2.0 (STEP-M12-137 there: the FIPS 180-2 "abc" answer, the scripted "r.j.w" request); the exit-code enumerations of the C sources and the registry's help texts at 1.2.0
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#2-specification-and-conformance"]
---

# Conformance vectors

Where a requirement has an exact answer — the bytes of the pairing QR
payload (REQ-TOOL-016), the exit code of every command (REQ-TOOL-003 …
028) — the answer SHALL be pinned once, language-neutrally, under
`conformance/` (`qr-payload.json`, `exit-codes.json`, further files with
the requirements that need them), and every implementation's test suite
SHALL check against those files or carry copies that a gate compares with
them; a vector changes only through its requirement (§6.2 of the process).

**Rationale:** several implementations sharing one specification stay
interchangeable only if the exact answers are shared too, not re-derived
per language. Pinning the bytes beside the requirements keeps the vectors
under the same change control.

**Acceptance criteria:**
- [ ] `conformance/qr-payload.json` holds the three known answers (the
      "abc" and "r.j.w" requests, the escaping/empty-field case) with the
      exact expected strings; `conformance/exit-codes.json` the common codes
      and every command's own; `conformance/README.md` the rules
      (STEP-T1-010).
- [ ] C: the `conformance_sync` CTest compares the literals the C tests
      assert against with the JSON files and the registry's help texts with
      `exit-codes.json`; negative-tested by altering one value (STEP-T1-020).
- [ ] Every later implementation reads the files or adds its own sync gate
      (recorded when it exists).
