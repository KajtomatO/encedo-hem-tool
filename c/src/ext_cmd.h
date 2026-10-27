/*
 * ext_cmd.h — hem-tool `ext` family: pair (terminal QR), list, login.
 *
 * implements: REQ-TOOL-016
 *
 * hem-tool-core: the CLI, the unit tests, and the attended live run drive
 * one code path, using ONLY the public SDK API. `ext pair` is the
 * registration orchestrator (the phone scans a QR rendered right in the
 * terminal — vendored qrcodegen, never a web service); `ext list` shows
 * paired authenticators; `ext login` demonstrates the blocking mobile
 * login with script-visible exit codes for each outcome.
 */
#ifndef HEM_TOOL_EXT_CMD_H
#define HEM_TOOL_EXT_CMD_H

#include <stdbool.h>
#include <stdio.h>

#include "ehem/ehem.h"

/* Exit codes (shared across the family; the cert-install convention). */
enum {
    HEM_EXT_OK       = 0,   /* success / approved */
    HEM_EXT_RUNTIME  = 1,   /* device / cloud / login failure */
    HEM_EXT_USAGE    = 2,   /* missing passphrase or bad arguments */
    HEM_EXT_TIMEOUT  = 3,   /* pair: QR never scanned; login: push unanswered */
    HEM_EXT_REFUSED  = 4,   /* pair: device 406 (slots full / already paired);
                             * login: rejected on the phone */
    HEM_EXT_NOAUTH   = 5    /* login: pre-check found no paired authenticator
                             * (only detectable when a passphrase is available) */
};

/* Pairing poll defaults — the tester's cadence (12 × 5 s ≈ one minute). */
#define HEM_EXT_PAIR_POLL_ATTEMPTS 12u
#define HEM_EXT_PAIR_POLL_DELAY_MS 5000u

typedef struct {
    const char *passphrase;    /* pairing needs sub="U"; NULL → USAGE */
    bool        mobile;       /* --mobile: push-confirm auth (REQ-TOOL-018) */
    const char *notify_url;    /* broker base; NULL → SDK default */
    int         no_qr;         /* print the QR payload JSON instead */
    unsigned    poll_attempts; /* register/check polls (0 → default) */
    unsigned    poll_delay_ms; /* delay between polls, VERBATIM (0 = none) */
    FILE       *out;           /* progress (NULL → stdout) */
    FILE       *err;           /* diagnostics (NULL → stderr) */
} hem_ext_pair_opts;

/*
 * `hem-tool ext pair` (REQ-TOOL-016): check-in (clock; warn-and-continue) →
 * login → config (eid + the user/email/hostname the QR payload carries) →
 * broker session (POST with eid — the broker binds registrations to an
 * eid-bound session, REQ-AUTH-008 finding) → device ext/init → broker
 * register/init → the SDK's ehem_ext_qr_payload() (`{link, hash, user,
 * email, hostname}`, hash = base64(SHA-256(request)) — the phone checks it
 * against the request the link serves) rendered as UTF-8 half-block cells
 * — a Windows console receives the same rows as UTF-16 through the console
 * API, its OEM code page never sees them — (or printed, --no-qr) → poll
 * register/check until the phone answers →
 * device ext/validate → broker register/finalise → report the new kid.
 */
int hem_ext_pair_run(ehem_ctx *ctx, const hem_ext_pair_opts *o);

/*
 * `hem-tool ext list`: every key whose descriptor starts with "EXTAID" —
 * kid, label ([PROTECTED] per the keys-list policy: real phones label
 * themselves "(iPhone)"/"(Android)"), and the pid (standard base64 of the
 * 32-byte descriptor suffix). Needs a bearer (keymgmt:search) — passphrase
 * or --mobile (REQ-TOOL-018).
 */
int hem_ext_list_run(ehem_ctx *ctx, const char *passphrase, bool mobile,
                     FILE *out, FILE *err);

typedef struct {
    const char *scope;         /* NULL → "system:config" */
    const char *note;          /* free text for the phone UI; NULL → none */
    const char *passphrase;    /* OPTIONAL: enables the no-pairing pre-check */
    long        timeout_ms;    /* confirmation timeout (0 → 60 000) */
    FILE       *out;
    FILE       *err;
} hem_ext_login_opts;

/*
 * `hem-tool ext login`: the blocking mobile-login demo. ehem_login_mobile
 * → one authenticated call for `scope` (a config GET when the scope is
 * system:config, else a bare acquisition via the same binding) → report.
 * The confirmation timeout comes from the context's confirm_timeout_ms
 * option (the CLI's --timeout). Exit codes make the outcome script-visible:
 * OK approved, REFUSED rejected, TIMEOUT unanswered, NOAUTH when the
 * optional passphrase pre-check finds zero pairings, RUNTIME otherwise.
 */
int hem_ext_login_run(ehem_ctx *ctx, const hem_ext_login_opts *o);

/*
 * Render `text` as a QR code in UTF-8 half-block cells (two module rows per
 * terminal line, 4-module quiet zone). When `out` is a Windows console the
 * rows are written as UTF-16 through WriteConsoleW instead — the console's
 * OEM code page turned the UTF-8 bytes into mojibake (v1.2.0-rc3 finding);
 * a file or pipe gets the UTF-8 bytes on every platform. Returns 0 on
 * success, -1 when the text does not fit a QR code or the console write
 * fails (caller falls back to printing the payload). Exposed for the unit
 * test (which exercises the byte path).
 */
int hem_ext_qr_render(const char *text, FILE *out);

#endif /* HEM_TOOL_EXT_CMD_H */
