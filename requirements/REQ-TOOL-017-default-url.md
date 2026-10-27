---
id: REQ-TOOL-017
title: hem-tool default device URL
status: approved
priority: should
revision: 2   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 1 = 
source: user decision 2026-08-06 (M9 scope reshape, ARCHITECTURE.md §11; proposed default confirmed at M9 decomposition); moved from encedo-hem-c-api (requirements/REQ-TOOL-017-default-url.md at v1.2.0) by user decision 2026-10-27
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli", "ARCHITECTURE.md#7-milestones"]
---

# hem-tool default device URL

When neither `--url` nor `EHEM_URL` supplies a device URL, hem-tool
SHALL use the built-in default `https://my.ence.do`.

- Precedence is unchanged and explicit: `--url` > `EHEM_URL` > built-in
  default. Today the tool errors out instead of falling back.
- Using the default is announced once on stderr (e.g.
  `notice: no --url/EHEM_URL — using default https://my.ence.do`) so a
  user pointed at the wrong device finds out immediately, and `--raw`
  output pipelines stay clean (stdout untouched).
- The default is the **product `my`-domain convention**, not a
  user-specific address: the provisioning cloud registers devices under
  the `my` domain (`api.encedo.com/domain/register/my`,
  REQ-SYS-013's `EHEM_DEFAULT_REGISTER_URL`) and the device's own TLS
  certificate is issued for its `my.ence.do` hostname.
- Tool-side only. The SDK keeps taking the URL as an explicit
  `ehem_ctx_create` parameter (ARCHITECTURE §1: the SDK reads no
  config); no `EHEM_DEFAULT_URL`-style constant enters the public
  headers.

**Rationale:** zero-config UX for the common one-device case
(`hem-tool status` just works out of the box), and the 1.0 polish item
the user requested 2026-08-06. The stderr notice keeps the fallback
honest for scripts.

**Acceptance criteria:**
- [x] URL resolution (flag > env > default, plus the notice decision)
      lives in hem-tool-core (registry.c hem_tool_resolve_url) and a
      unit test covers all three precedence levels
      (tests/unit/test_registry.c, 2026-08-06).
- [x] With no `--url` and no `EHEM_URL`, `hem-tool status` targets
      `https://my.ence.do` and prints the stderr notice exactly once
      (resolution runs once per invocation, stdout untouched); with
      either source present, no notice (unit + live smoke 2026-08-06).
- [x] Top-level help documents the default next to `--url`/`EHEM_URL`
      (registry SHARED_OPTIONS; asserted in test_registry.c).
- [x] Live (2026-08-06): `hem-tool status` with a clean environment
      reached the dev device via the default URL — notice printed,
      status + version rendered.
