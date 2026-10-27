---
id: REQ-TOOL-022
title: hem-tool wipe-device — factory reset with an unbypassable confirmation
status: approved
priority: should
revision: 3   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 2 = 2026-10-10 (STEP-M10-068): user decision "remove any hints about recovery" — the follow-up hint names init-device only; meaning change → draft; re-approved 2026-10-10 (user: "OK")
source: user decision 2026-10-07 (M10: "add both init-device and wipe-device to the tool"; attended-only); REQ-SYS-014 (the binding); ARCHITECTURE.md §8 protected-key confirmation convention; approved 2026-10-07 (M10 decomposition, user go-ahead); rev 2: user decision 2026-10-10 ("remove any hints about recovery"), approved the same day ("… REQ-TOOL-022 rev 2 : OK"); moved from encedo-hem-c-api (requirements/REQ-TOOL-022-wipe-device.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-SYS-014", "REQ-TOOL-018", "REQ-TEST-007"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli", "ARCHITECTURE.md#7-milestones"]
---

# hem-tool wipe-device — factory reset with an unbypassable confirmation

`hem-tool wipe-device [--wait]` SHALL factory-reset the device through
`ehem_system_wipeout` (REQ-SYS-014) only after an interactive
confirmation that `--yes` does not satisfy: the tool prints the device's
identity (hostname, `devid`, `instanceid` from `ehem_system_config`) and
proceeds only when the operator types that hostname exactly.

- **Auth:** passphrase only — the firmware demands token `sub` U or M
  for config writes (REQ-SYS-014), so `--mobile` is rejected up front
  with the same message shape as `ext pair --mobile` (REQ-TOOL-018).
- **Flow:** login → `ehem_system_config` (identity shown) →
  confirmation → `ehem_system_wipeout` → report "wipe accepted; the
  device erases its configuration and restarts in ~2 s". With `--wait`:
  poll `GET /api/system/status` over the device's **http://** URL
  (HTTPS is gone with the TLS material) until it answers, bounded
  (~180 s, reboot-style pacing), then print that the device is
  uninitialised and point at `init-device`. Neither the output nor the
  help points at `recovery` / `tls-recover` (rev 2, user decision
  2026-10-10).
- **Exit codes** (the reboot/tls-recover convention): 0 = wipe accepted
  (and, with `--wait`, the device came back); 1 = login/device failure;
  2 = usage/environment; 3 = confirmation declined; 4 = `--wait`
  timeout.
- DISRUPTIVE and irreversible; `--help` says so and names what is lost
  (keys, user passwords, TLS material, logs).

**Rationale:** the wipe is the most destructive call in the API and the
precondition for re-initialisation. The typed-hostname confirmation
reuses the protected-key idea (explicit, per-target, `--yes` ignored)
with a stronger token than `YES`, because the target is the whole
device.

**Acceptance criteria:**
- [x] Unit (hem-tool-core, fake transport): declined confirmation → exit
      3 with zero device writes; wrong hostname → exit 3; correct
      hostname → the wipeout request is sent exactly once; `--yes` does
      not skip the prompt (the command has no such option); `--mobile` →
      exit 2 naming the sub="U" constraint; `--wait` poll until status
      answers / exit 4 on exhaustion. *(STEP-M10-030, 2026-10-09:
      tests/unit/test_wipe.c, six cases.)*
- [x] **Attended-only** (REQ-TEST-007): no live CTest. Evidence to record
      here: date, device, typed confirmation, observed restart, device
      back uninitialised. *(2026-10-10, attended by the user, v1.1.0-rc4
      release binaries, dev device my.ence.do, FW v1.2.2: the device was
      wiped after the Windows test and again after the Linux test (the
      typed hostname is the only way past the prompt — there is no
      --yes); afterwards `hem-tool --url http://my.ence.do status`
      (read-only) showed it restarted and back uninitialised — `inited:
      no`, uptime 602 s — and still serving HTTPS with its trusted
      certificate (`https: yes`): this wipe kept the TLS material.)*
- [x] Rev 2: neither the wipe-device output nor its help mentions
      `recovery` / `tls-recover`. *(wipe.c "next:" line, registry.c help;
      STEP-M10-068, 2026-10-10; ./dev ci 47/47 gcc + clang.)*
- [x] `--help` and the per-command help (REQ-TOOL-020) document the
      confirmation, the irreversibility and the follow-up commands.
      *(registry.c entry, STEP-M10-030, 2026-10-09.)*
