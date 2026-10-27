---
id: REQ-TOOL-028
title: hem-tool init-wizard — attended, prompt-driven device initialisation over the init-device flow
status: approved
priority: should
revision: 6   # 2026-10-27 (STEP-T1-010): moved from encedo-hem-c-api at 1.2.0 (user decision 2026-10-27: hem-tool is its own deliverable; this repository holds the shared specification) — text unchanged; `status` is now the specification lifecycle (approved), the C implementation's state is the TRACE matrix's. Rev 5 = 2026-10-27 (STEP-M12-140, the M12 gate): record only — the attended criterion met on the published `v1.2.0-rc4` assets (the user: the wizard end to end on the second unit on Linux and Windows, echo hidden and restored, then `status` + login — "all is working"); every criterion closed. No meaning change. Rev 4 = 2026-10-19 (STEP-M12-135 done): re-verified — the rev 3 criterion checked (test_wizard, green on all 10 legs of CI run 39320511927 and in both release jobs' unit suites, run 39320626797 / `v1.2.0-rc2`); verified again. No meaning change. Rev 3 = 2026-10-19 (STEP-M12-135, §6.2 approved in chat): `hostname` defaults to `my.ence.do` — REQ-TOOL-017's product convention (user decision 2026-10-19 after the rc1 test); `user` and `email` keep no default; a unit criterion added; needs-reverify until test_wizard is green again. Rev 2 = 2026-10-13 (STEP-M12-050): OPEN criterion (environment pre-fill) settled — only --url / EHEM_URL; unit criteria recorded. No meaning change — approved status kept
source: user note 2026-10-10 ("initialisation creator" — hand-hold the user through init; NOT designed then); shape decided by the user 2026-10-12 (M12 planning: a SEPARATE wizard command, `init-device` stays purely scripted; explains and prompts for every field with its default, passphrase hidden and twice, generates the 24 words and confirms the backup by re-typing a few, summary behind a typed confirmation, then the existing init flow incl. the reboot; needs a shared no-echo prompt helper); ARCHITECTURE.md §11 M12 part E (command name settled at decomposition: `init-wizard`); REQ-TOOL-021 (the flow it drives), REQ-AUTH-012 (the words), REQ-TEST-007 (attended-only); approved 2026-10-12 (M12 decomposition, user: "approve"); moved from encedo-hem-c-api (requirements/REQ-TOOL-028-init-wizard.md at v1.2.0) by user decision 2026-10-27
depends_on: ["REQ-TOOL-021", "REQ-AUTH-012", "REQ-TEST-007", "REQ-TOOL-020"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#3-the-cli", "ARCHITECTURE.md#4-testing-policy", "ARCHITECTURE.md#7-milestones"]
---

# hem-tool init-wizard

hem-tool SHALL provide `init-wizard`, an interactive command that
collects every `init-device` input through explained prompts and then
runs the **same** init flow (`hem_init_device_run`, REQ-TOOL-021) —
`init-device` itself stays scripted and unchanged.

- **Flow:**
  1. intro — what initialisation does, that the device must be
     uninitialised and reached over `http://`, and that the master
     secret cannot be rotated later;
  2. one prompt per `cfg` field with its explanation and the Manager's
     default in brackets, Enter = default (`user` and `email` have no
     default and re-prompt when empty; `hostname` defaults to
     `my.ence.do` — the product convention behind REQ-TOOL-017's default
     URL, one definition for both (rev 3, user decision 2026-10-19);
     `ip A.B.C.D/N`,
     `storage-mode`, `disk0-size`, `origin`, `dnsd`, the three trust
     booleans, `gen-csr`, `ctx`);
  3. the passphrase, **hidden**, typed twice, re-prompted on mismatch
     (the device validates its content, as for `init-device`);
  4. the master secret: *generate 24 words* (default) or *type an
     existing mnemonic*; generated words are shown **once**, then the
     backup is confirmed by re-typing the words at three randomly
     chosen positions (a wrong word → the words are shown again, retry);
  5. a summary of every field (passphrase masked, words not shown)
     behind a typed confirmation — the hostname, as `wipe-device` does —
     then the init flow, asking whether to reboot now when the device
     requires it;
  6. the `init-device` result and next steps (as REQ-TOOL-021 prints
     them; no `recovery` hints — rev 3 of that REQ applies).
- **Exit codes:** 0–5 as `init-device` (same verdicts, same mapping);
  **6** = cancelled by the operator at any prompt (zero traffic until
  the typed confirmation).
- **No-echo prompt helper** in hem-tool-core (new to the tool): termios
  `ECHO` off / `SetConsoleMode` without `ENABLE_ECHO_INPUT`, restored on
  every return path including Ctrl-C where the platform allows; when
  stdin is not a terminal the helper reads a plain line (so the unit
  test can script the dialogue) and says so once on stderr.
- **Auth class:** none (the passphrase is input data, REQ-TOOL-021);
  registered in the no-credentials group; top-level help ≤ 45 lines
  (43 today + `keys types` + `init-wizard` = 45).
- **Attended-only** (REQ-TEST-007): no live CTest; the M12 gate sitting
  runs the wizard on the second unit and records the outcome here.

**Rationale:** `init-device` has 18 flags and a master secret whose loss
is unrecoverable — a scripted interface for an operator who does this
once. The wizard puts the explanations where the decisions are made and
makes the backup of the 24 words a step that cannot be skipped. Keeping
`init-device` untouched keeps automation stable (user decision
2026-10-12).

**Acceptance criteria:**
- [x] Unit (hem-tool-core, scripted stdin/stdout/stderr, fake transport):
      a full run accepting every default produces exactly the init POST
      body that `init-device` produces for the equivalent flags
      (byte-compare through the fake transport); a passphrase mismatch
      re-prompts; a wrong backup word re-prompts after re-showing the
      words; cancel at the summary → exit 6 and zero transport I/O;
      typing an existing mnemonic skips generation.
      *(2026-10-13, STEP-M12-050: `tests/unit/test_wizard.c` — the wizard
      (`src/tools/hem-tool/wizard.{h,c}`) builds a `hem_init_opts` and calls
      `hem_init_device_run`, so the body is init-device's by construction;
      the test still compares the POST bytes with a separate
      `hem_init_device_run` run (pinned clock + KDF seam) for: all defaults
      with a typed mnemonic, the generated-words path (test seam: fixed words
      + positions 2/13/24), and explicit answers vs the equivalent flags
      (ip, storage-mode 83, disk0, origin, dnsd, no trusted-backend,
      gen-csr, ctx 5 — after rejected values 99 / abc / maybe / -1 were
      re-asked). Empty and mismatched passphrases re-ask; a wrong backup
      word re-shows the words (case-insensitive compare); a wrong hostname
      or the end of input at any of six points → exit 6, zero requests; an
      invalid typed mnemonic re-asks without quoting it; GET-init 406 →
      exit 3; the reboot question: yes → reboot + wait, no → the hint.
      CLI on the real `main.c` (`tool_startup_cli`): empty stdin → exit 6.)*
- [x] Unit (rev 3): the hostname prompt reads `hostname [my.ence.do]:` and
      Enter puts `my.ence.do` into the POST body (byte-compare with
      `init-device --hostname my.ence.do`); `user` and `email` still
      re-prompt when empty; a typed hostname is taken over the default.
      *(2026-10-19, STEP-M12-135: `wizard.c` asks `hostname [my.ence.do]:`
      with `HEM_TOOL_DEFAULT_HOSTNAME` from `registry.h` — the one
      definition behind `HEM_TOOL_DEFAULT_URL`. test_wizard: the
      all-defaults runs press Enter at hostname and the POST body still
      byte-equals init-device's with `--hostname my.ence.do`; the prompt
      text is asserted, `user: ` and `email: ` carry no bracketed default;
      test_required_fields_reprompt — an empty user and an empty email are
      re-asked once each, "hostname is required" never appears, the summary
      and the typed confirmation show my.ence.do, cancelling there → exit 6
      with zero requests; the explicit run types dev.ence.do over the
      default and confirms it. Green on all 10 legs of CI run 39320511927
      (8dd915f) and in both release jobs' unit suites (run 39320626797,
      `v1.2.0-rc2`).)*
- [x] Unit: after the one display, the 24 words appear in no later
      output (the backup check prints positions only; the summary masks
      them); the passphrase never appears in any output.
      *(`test_generated_words_backup`: two displays (one re-show after the
      wrong word), "bless" exactly twice, no mnemonic word after "backup
      confirmed"; the typed-mnemonic runs print none of its words; every
      run asserts the passphrase in neither stream; backup answers are read
      hidden.)*
- [x] Unit: the no-echo helper's non-tty path reads lines verbatim; on a
      tty the echo state is restored after a normal and an early return
      (attended check on Linux and Windows consoles at the gate; recorded).
      *(`src/tools/hem-tool/prompt.{h,c}` + `tests/unit/test_prompt.c`: the
      non-terminal path verbatim (spaces kept, CRLF removed, a line of exactly
      capacity−1 fits, longer → reported and wiped, end of input), the
      "not a terminal" notice once. On Linux the terminal path is automated
      on a pseudo-terminal: a child process types only once the echo is off,
      the keyboard side sees nothing echoed, the echo is back after the
      normal return and after Ctrl-D, and SIGINT during a hidden read
      restores it while the process still dies of SIGINT (30/30 repeated
      runs). The Windows console branch (`SetConsoleMode` +
      `SetConsoleCtrlHandler`) is syntax-checked by the MinGW cross compiler
      only — its run, and the attended Linux check, are the gate's.)*
- [x] Registry: `init-wizard` with auth class none and full per-command
      help (test_registry_complete); top-level help ≤ 45 lines.
      *(Listed under "commands needing NO credentials" after init-device; the
      top page is exactly 45 lines — test_top_page_shape's cap.)*
- [x] Attended (REQ-TEST-007), recorded here: date, device, the wizard
      run end to end on Linux and on Windows (the console echo-off
      behaviour included), then `status` + passphrase login.
      *(Met — the user, 2026-10-27, on the published `v1.2.0-rc4` assets,
      the second (uninitialised) unit, answering the gate's checklist
      (the wizard end to end on Linux and on Windows, the passphrase
      prompt hiding the typing and restoring the echo, then `status` and
      a passphrase login): "all is working". A one-line report — no
      transcript. The rc1 run of 2026-10-19 (REQ-BUILD-009) had already
      taken the one-file binary's wizard through init, a key, a signature
      and a wipe.)*
- [x] RESOLVED at STEP-M12-050 (2026-10-13) — the step plan's
      recommendation: `--url` / `EHEM_URL` is the wizard's ONLY pre-fill.
      Secrets never come from the environment in this interactive flow:
      `EHEM_PASSPHRASE` and `EHEM_MASTER_WORDS` are ignored with one stderr
      notice ("init-wizard ignores EHEM_PASSPHRASE and EHEM_MASTER_WORDS -
      it asks for both" — hem.env is often sourced), and init-device's input
      flags (`--passphrase`, `--master-*`, `--user`, `--email`, `--hostname`,
      `--ip`, `--origin`, `--storage-mode`, `--disk0-size`, `--dnsd`,
      `--no-trusted-*`, `--no-allow-keysearch`, `--gen-csr`, `--csr-out`,
      `--ctx`, `--reboot`) are refused with exit 2 pointing at init-device —
      a scripted run belongs there (`tool_startup_cli`: `init-wizard --user
      alice` → 2). Connection options (`--url`, `--cacert`, `--insecure`)
      apply as everywhere.
