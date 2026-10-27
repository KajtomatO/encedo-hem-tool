---
id: REQ-TOOL-021
title: hem-tool init-device — personalise an uninitialised device
status: approved
priority: should
revision: 4   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 3 = 2026-10-10 (STEP-M10-068): user decision "remove any hints about recovery" — the Output no longer points at recovery / tls-recover; meaning change → draft; re-approved 2026-10-10 (user: "OK")
source: user decision 2026-10-07 (M10: "add both init-device and wipe-device to the tool"; attended-only); REQ-AUTH-011 (the binding); Encedo Manager assets/build.js initFinal (the authoritative client flow); encedo-hem-api-doc auth/init.md; approved 2026-10-07 (M10 decomposition, user go-ahead); rev 3: user decision 2026-10-10 ("remove any hints about recovery"), approved the same day ("Approve REQ-TOOL-021 rev 3 …: OK"); moved from encedo-hem-c-api (requirements/REQ-TOOL-021-init-device.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-AUTH-011", "REQ-AUTH-012", "REQ-SYS-003", "REQ-TEST-007"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli", "ARCHITECTURE.md#7-milestones"]
---

# hem-tool init-device — personalise an uninitialised device

`hem-tool init-device` SHALL initialise an uninitialised device through
`ehem_device_init` (REQ-AUTH-011): it runs a check-in first (the init
endpoints demand a set RTC — 403 otherwise), fetches the init challenge,
derives the user key from the given passphrase, signs the init JWT with
the master key, posts it, and prints what the device returned.

- **Inputs:** the user passphrase via the standard `--passphrase` /
  `EHEM_PASSPHRASE` sources (it becomes the device's user password);
  the master secret as a Manager-compatible **24-word BIP39 mnemonic**
  via `--master-words "w1 … w24"` (or `EHEM_MASTER_WORDS`) — or, when
  absent, `--master-generate`, which has the SDK generate the 24 words
  (REQ-AUTH-012) and prints them **once** with a keep-this warning (the
  device offers no way to rotate `masterkey` later); `--master-secret-hex
  HEX` (32 bytes) remains only as an escape hatch for scripted,
  non-Manager use (rev 2, user decision 2026-10-07); the `cfg`
  fields as flags: `--user`, `--email`, `--hostname`, `--ip A.B.C.D/N`
  (required); `--storage-mode N`, `--disk0-size N`, `--dnsd`,
  `--no-trusted-ts`, `--no-trusted-backend`, `--no-allow-keysearch`,
  `--origin`, `--gen-csr`, `--ctx N` with the Manager's defaults
  (`dnsd` false, `origin` "*", `ctx` 0, the three trust booleans true,
  `gen_csr` false).
- **URL:** a wiped device has no HTTPS; the command is used as
  `hem-tool --url http://<host> init-device …`, exactly like
  `tls-recover`.
- **Output:** `instanceid`, `reboot_required`, the PEM `csr` when
  `--gen-csr` was given (also written to `--csr-out FILE` if asked), and
  the next steps: reboot when required (`--reboot` does it and waits),
  then a passphrase login. Neither the output nor the help points at
  `recovery` / `tls-recover` (rev 3, user decision 2026-10-10 — the
  2026-10-10 wipes kept the device's trusted HTTPS certificate, so a
  standing "restore HTTPS" step was wrong). The returned bearer
  (`sub="U"`, scope `system:config`) is not printed.
- **Exit codes:** 0 = initialised; 1 = device/login failure; 2 =
  usage/environment (missing passphrase/master secret/required cfg);
  3 = device already initialised (406); 4 = RTC still unset after the
  check-in (403); 5 = `cfg` rejected (400 — a field failed validation).
- **Auth class:** none — the init endpoints take no bearer; the
  passphrase is input data, not a credential, and the help says so.

**Rationale:** the SDK should be able to initialise a wiped device (user
decision 2026-08-07) and the tool is the living usage documentation;
without this command the only init path is the Manager's web UI.

**Acceptance criteria:**
- [x] RESOLVED (user decision 2026-10-07, rev 2): master-secret format =
      Manager-compatible BIP39 (24 English words). Reason: the Manager
      consumes the words after init — its settings-page master
      passphrase prompt (`build.js:6819-6829`: change storage / user
      data / password, manual firmware, manual wipeout) derives the same
      key and logs in with scope `system:config`; raw hex would cut a
      tool-initialised device off from that. Hex stays only as the
      scripted escape hatch. The derivation lives in the SDK
      (REQ-AUTH-012), not in the tool.
- [x] Unit (hem-tool-core, fake transport): the full sequence (check-in
      legs → GET init → POST init) with the Manager-default `cfg`;
      required-flag omissions → exit 2 with zero traffic; 406 → exit 3;
      403 after check-in → exit 4; 400 → exit 5; the master secret never
      appears in output except the one `--master-generate` print.
      *(STEP-M10-040, 2026-10-09: tests/unit/test_init_tool.c, five
      cases; the defaults are the Manager's — ip 192.168.7.1/24,
      storage_mode 81, disk0 8388608, build.js:4189-4201.)*
- [x] **Attended-only** (REQ-TEST-007): no live CTest. Evidence to record
      here: date, device, the init result (instanceid, reboot_required),
      and that a subsequent `hem-tool status` + passphrase login worked.
      *(2026-10-10, attended by the user, v1.1.0-rc4 release binaries on
      the wiped dev device my.ence.do, FW v1.2.2: Windows — init
      succeeded, then a key was created and text signed (passphrase
      login) and `recovery` found the device healthy under system trust;
      Linux — "everything works". instanceid / reboot_required not
      captured in the reports. The rc4 binary still printed the rev-2
      recovery hint; rev 3 removes it.)*
- [x] Rev 3: neither the init-device output nor its help mentions
      `recovery` / `tls-recover`. *(init_cmd.c "next:" line, registry.c
      help; STEP-M10-068, 2026-10-10; ./dev ci 47/47 gcc + clang.)*
- [x] `--help` documents every cfg flag with its default and the
      irreversibility of the master secret. *(registry.c OPT_INIT +
      entry, STEP-M10-040, 2026-10-09.)*
