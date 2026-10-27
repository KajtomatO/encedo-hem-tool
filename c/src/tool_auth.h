/*
 * tool_auth.h — the ONE login chokepoint for every bearer-needing hem-tool
 * subcommand: passphrase vs mobile push confirmation (--mobile).
 *
 * implements: REQ-TOOL-018
 *
 * Mode selection: `mobile` wins over a passphrase from the ENVIRONMENT
 * (hem.env is commonly auto-sourced); the explicit `--mobile --passphrase`
 * conflict is a usage error rejected by main.c's argument parsing before
 * this helper ever runs. Neither credential → EHEM_ERR_ARG with a message
 * on `err` (callers map it to their USAGE exit).
 *
 * Mobile outcomes: the push happens at the FIRST bearer acquisition (the
 * SDK's lazy ensure-token), so rejected/timeout surface on the command's
 * OPERATION, not on login. Commands route their operation-failure exits
 * through hem_tool_auth_exit() so those outcomes keep ONE tool-wide exit
 * vocabulary on every subcommand (13/14 — above every per-command code;
 * `ext login` keeps its older documented 3/4/5). "Nothing paired" is NOT
 * separately detectable in pure mobile mode — the push goes to nobody and
 * the confirm expires (user decision 2026-08-06: timeout + hint, no
 * pre-check) — so the timeout message points at `hem-tool ext list`.
 */
#ifndef HEM_TOOL_AUTH_H
#define HEM_TOOL_AUTH_H

#include <stdbool.h>
#include <stdio.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "registry.h"

/* Tool-wide exit codes for the mobile confirmation outcomes. Deliberately
 * ABOVE every per-command exit vocabulary (commands use 0..9). */
enum {
    HEM_TOOL_EXIT_CONFIRM_TIMEOUT = 13,  /* push not answered in time */
    HEM_TOOL_EXIT_REJECTED        = 14   /* rejected on the phone */
};

/*
 * Enter the selected auth mode on `ctx` (lazy — no network happens here):
 * mobile → ehem_login_mobile(); else passphrase → ehem_login(); neither →
 * EHEM_ERR_ARG. Prints a diagnostic to `err` (NULL → stderr) on any
 * failure. In mobile mode a non-NULL passphrase is deliberately ignored.
 */
ehem_rc hem_tool_login(ehem_ctx *ctx, const char *passphrase, bool mobile,
                       FILE *err);

/*
 * Map an OPERATION-failure `rc` to the command's exit code: the mobile
 * confirmation outcomes get their distinct tool-wide codes (with a message
 * on `err`, NULL → stderr); anything else returns `fallback` unchanged
 * (the caller has already printed its own diagnostic).
 */
int hem_tool_auth_exit(ehem_rc rc, FILE *err, int fallback);

/*
 * The registry's auth class, enforced before any traffic (REQ-TOOL-019 rev 2):
 * a PASSPHRASE_ONLY command invoked with --mobile is a usage error — the
 * device accepts those operations only from a passphrase session (token sub
 * "U", or "M" for config writes; a mobile bearer carries sub=base64(kid)).
 * Prints the reason to `err` (NULL → stderr) and returns 2; otherwise 0.
 * `cmd` may be NULL (unknown command → nothing to check, 0).
 */
int hem_tool_check_auth_class(const hem_command *cmd, bool mobile, FILE *err);

#endif /* HEM_TOOL_AUTH_H */
