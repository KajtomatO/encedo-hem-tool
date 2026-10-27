---
id: REQ-PKG-002
title: C implementation — the tool sources use the SDK's public API only, proven by a gate
status: approved
priority: must
revision: 1   # 2026-10-27: drafted with the split plan; approved with it (the standing hem-tool rule of encedo-hem-c-api — ARCHITECTURE §3/§8 there — made checkable)
source: encedo-hem-c-api ARCHITECTURE.md §3 ("hem-tool | CLI over the public API only") and §8 at 1.2.0; the exploration of 2026-10-27 (every SDK symbol the tool names is in include/ehem; no tool source includes an SDK-internal header; no gate enforced it); user decision 2026-10-27 (ARCHITECTURE.md §1 item 8)
applies_to: ["c"]
depends_on: ["REQ-PKG-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#5-implementations"]
---

# C implementation — public API only

The C implementation's sources (`c/src/**`, vendored code excepted) SHALL
include only the SDK's public headers (`ehem/*.h`), the tool's own headers
and the vendored QR encoder's header, and SHALL link no SDK-internal
symbol — enforced by a unit-labelled gate over the sources
(`public_api_only`) and by the package-mode build, where no SDK source
exists on the include path.

**Rationale:** the tool is the SDK's reference consumer; reaching into
internals would hide API gaps from the SDK and break the package-mode
release build. The SDK's `src/` is on the include path in submodule mode
only because the tests need it — the gate keeps that from leaking into the
tool.

**Acceptance criteria:**
- [ ] `c/tests/unit/check_public_api.cmake` (CTest `public_api_only`,
      label unit) scans every `#include "…"` in `c/src/*.c|*.h` outside
      `vendor/` and fails on anything but `ehem/*.h`, a tool header or
      `vendor/qrcodegen/qrcodegen.h`; negative-tested with a planted
      internal include (STEP-T1-020).
- [ ] The package-mode build (REQ-PKG-001) links `hem-tool` against the
      SDK package alone (STEP-T1-040; the CI package-mode legs,
      STEP-T1-050).
