/*
 * ext_cmd.c — hem-tool `ext` family (see ext_cmd.h).
 *
 * implements: REQ-TOOL-016
 */
#define _POSIX_C_SOURCE 199309L   /* nanosleep / struct timespec under -std=c99 */

#include "ext_cmd.h"
#include "tool_auth.h"

#include <stdlib.h>
#include <string.h>

#include "ehem/auth.h"
#include "ehem/keymgmt.h"
#include "ehem/system.h"

#include "keys.h"     /* hem_key_is_protected, hem_tool_fprint_b64 */
#include "vendor/qrcodegen/qrcodegen.h"

#ifdef _WIN32
#  include <windows.h>            /* Sleep() — MinGW has no POSIX nanosleep; WriteConsoleW */
#  include <io.h>                 /* _fileno, _get_osfhandle */
#else
#  include <time.h>               /* nanosleep / struct timespec */
#endif

static void sleep_ms(unsigned ms)
{
    if (ms == 0) {
        return;
    }
#ifdef _WIN32
    Sleep(ms);
#else
    {
        struct timespec ts;
        ts.tv_sec  = (time_t)(ms / 1000u);
        ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
        (void)nanosleep(&ts, NULL);
    }
#endif
}

static FILE *or_stdout(FILE *f) { return f != NULL ? f : stdout; }
static FILE *or_stderr(FILE *f) { return f != NULL ? f : stderr; }

/* -------------------------------------------------------------------------- */
/* terminal QR                                                                */
/* -------------------------------------------------------------------------- */

/*
 * UTF-8 half-block rendering, two module rows per terminal line, 4-module
 * quiet zone. INVERTED for the common dark-background terminal: a DARK QR
 * module prints as background (space), a light module as a block — the
 * quiet zone comes out bright, which is what the scanner needs. On a
 * light-background terminal, scan apps still cope (QR orientation/polarity
 * detection); `--no-qr` prints the payload for anything exotic.
 *
 * Windows: a console decodes the bytes a program writes in its OUTPUT code
 * page — OEM 437/852/…, never UTF-8 by default — so the three-byte
 * half-blocks came out as three mojibake letters each ("a mess of chars":
 * the user's v1.2.0-rc3 Windows test, 2026-10-20). When `out` IS a console
 * the rows go out as UTF-16 through WriteConsoleW, which no code page
 * touches and which the legacy raster fonts render too (the three blocks
 * exist in every OEM font). Redirected output — a file, a pipe, the unit
 * test's tmpfile — stays the UTF-8 bytes, like `--no-qr`.
 */

/* Cell index: bit 1 = top module dark, bit 0 = bottom module dark. Inverted
 * rendering — a LIGHT half is the lit one. */
static const char *const k_cell_utf8[4] = {
    "\xe2\x96\x88",   /* 0: both light  → █ */
    "\xe2\x96\x80",   /* 1: bottom dark → ▀ */
    "\xe2\x96\x84",   /* 2: top dark    → ▄ */
    " "               /* 3: both dark   → background */
};

#ifdef _WIN32
static const wchar_t k_cell_utf16[4] = { 0x2588, 0x2580, 0x2584, L' ' };

/* The console behind `out`, or NULL when it is a file or a pipe. */
static HANDLE console_of(FILE *out)
{
    intptr_t fh = _get_osfhandle(_fileno(out));
    HANDLE h = (HANDLE)fh;
    DWORD mode;

    if (fh == -1 || h == NULL || !GetConsoleMode(h, &mode)) {
        return NULL;
    }
    return h;
}
#endif

int hem_ext_qr_render(const char *text, FILE *out)
{
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    int size, x, y;
#ifdef _WIN32
    HANDLE con;
    wchar_t wline[96];            /* ≤ 80 cells + "\r\n" */
    DWORD n, written;
#endif

    if (text == NULL || out == NULL) {
        return -1;
    }
    if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW,
                              qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true)) {
        return -1;
    }
    size = qrcodegen_getSize(qr);
    if (size + 8 > 80) {
        /* Wider than a standard terminal — an overflowing QR wraps into
         * unscannable noise; the caller prints the payload instead. The
         * observed pairing payloads land around version 10-13 (≤ 77 cols). */
        return -1;
    }

#ifdef _WIN32
    con = console_of(out);
    if (con != NULL) {
        fflush(out);              /* the stdio text above lands first */
    }
#endif

    /* Rows -4 .. size+3 (quiet zone), two per line. Module reads outside
     * the symbol return false (light) from qrcodegen_getModule. */
    for (y = -4; y < size + 4; y += 2) {
#ifdef _WIN32
        n = 0;
#endif
        for (x = -4; x < size + 4; x++) {
            bool top    = qrcodegen_getModule(qr, x, y);
            bool bottom = (y + 1 < size + 4)
                              ? qrcodegen_getModule(qr, x, y + 1) : false;
            int cell = (top ? 2 : 0) | (bottom ? 1 : 0);
#ifdef _WIN32
            if (con != NULL) {
                wline[n++] = k_cell_utf16[cell];
                continue;
            }
#endif
            fputs(k_cell_utf8[cell], out);
        }
#ifdef _WIN32
        if (con != NULL) {
            wline[n++] = L'\r';
            wline[n++] = L'\n';
            if (!WriteConsoleW(con, wline, n, &written, NULL)) {
                return -1;        /* the caller prints the payload instead */
            }
            continue;
        }
#endif
        fputc('\n', out);
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* ext pair                                                                   */
/* -------------------------------------------------------------------------- */

int hem_ext_pair_run(ehem_ctx *ctx, const hem_ext_pair_opts *o)
{
    FILE *out, *err;
    unsigned attempts, delay_ms, i;
    ehem_config_info *cfg = NULL;
    ehem_checkin_info *ci = NULL;
    char *epk = NULL, *payload = NULL;
    ehem_ext_init_info *init = NULL;
    ehem_notify_register_info *reg = NULL;
    ehem_notify_pairing_reply *pr = NULL;
    ehem_ext_validate_info *val = NULL;
    ehem_rc rc;
    int ret = HEM_EXT_RUNTIME;

    if (ctx == NULL || o == NULL) {
        return HEM_EXT_USAGE;
    }
    out = or_stdout(o->out);
    err = or_stderr(o->err);
    if (o->mobile) {
        /* implements: REQ-TOOL-018 — pairing is passphrase-ONLY: the device
         * demands sub="U" for the pairing trio (REQ-AUTH-006), and mobile
         * bearers carry sub=base64(kid) (REQ-AUTH-007). */
        fprintf(err, "error: ext pair cannot use --mobile - the device "
                     "accepts pairing changes only from a passphrase login "
                     "(sub=\"U\"); pass --passphrase / set EHEM_PASSPHRASE\n");
        return HEM_EXT_USAGE;
    }
    if (o->passphrase == NULL) {
        fprintf(err, "error: ext pair needs a passphrase "
                     "(--passphrase / EHEM_PASSPHRASE)\n");
        return HEM_EXT_USAGE;
    }
    attempts = (o->poll_attempts != 0) ? o->poll_attempts
                                       : HEM_EXT_PAIR_POLL_ATTEMPTS;
    delay_ms = o->poll_delay_ms;
    if (o->poll_attempts == 0 && delay_ms == 0) {
        delay_ms = HEM_EXT_PAIR_POLL_DELAY_MS;
    }

    /* Clock sync first — the device clock drifts (KNOWN-ISSUES) and every
     * JWT in the flow is time-checked. Best effort. */
    if (ehem_system_checkin(ctx, &ci) != EHEM_OK) {
        fprintf(err, "warning: check-in failed (%s) - continuing\n",
                ehem_last_error(ctx)->message);
    }
    ehem_checkin_result_free(ci);

    /* Passphrase-only by design (mobile rejected above) — still routed
     * through the shared chokepoint (REQ-TOOL-018). */
    if (hem_tool_login(ctx, o->passphrase, false, err) != EHEM_OK ||
        ehem_system_config(ctx, &cfg) != EHEM_OK) {
        fprintf(err, "error: login/config failed: %s\n",
                ehem_last_error(ctx)->message);
        goto done;
    }
    if (cfg->eid == NULL) {
        fprintf(err, "error: device config carries no eid\n");
        goto done;
    }

    /* Broker session MUST be eid-bound for registration (REQ-AUTH-008). */
    rc = ehem_notify_session(ctx, o->notify_url, cfg->eid, &epk);
    if (rc == EHEM_OK) {
        rc = ehem_ext_init(ctx, epk, &init);
    }
    if (rc == EHEM_OK) {
        rc = ehem_notify_register_init(ctx, o->notify_url, epk, init->eid,
                                       init->request, &reg);
    }
    if (rc != EHEM_OK) {
        fprintf(err, "error: registration setup failed: %s\n",
                ehem_last_error(ctx)->message);
        goto done;
    }

    /* The payload the phone scans is the SDK's — `hash` is base64(SHA-256(
     * request)), which the Encedo Auth app checks against the request the
     * link serves (REQ-AUTH-008). */
    rc = ehem_ext_qr_payload(reg->link, init->request, cfg->user, cfg->email,
                             cfg->hostname, &payload);
    if (rc != EHEM_OK) {
        fprintf(err, "error: QR payload: %s\n", ehem_rc_str(rc));
        goto done;
    }

    fprintf(out, "Scan this with the Encedo app to pair "
                 "(waiting up to %u s):\n\n",
            attempts * (delay_ms / 1000u));
    if (o->no_qr || hem_ext_qr_render(payload, out) != 0) {
        if (!o->no_qr) {
            fprintf(err, "warning: QR rendering failed - payload follows\n");
        }
        fprintf(out, "%s\n", payload);
    }
    fprintf(out, "\nlink: %s\n", reg->link);

    /* Wait for the phone. */
    for (i = 0; i < attempts; i++) {
        rc = ehem_notify_register_check(ctx, o->notify_url, reg->rid, &pr);
        if (rc != EHEM_OK) {
            fprintf(err, "error: register/check failed: %s\n",
                    ehem_last_error(ctx)->message);
            goto done;
        }
        if (!pr->pending) {
            break;
        }
        ehem_notify_pairing_reply_free(pr);
        pr = NULL;
        sleep_ms(delay_ms);
    }
    if (pr == NULL || pr->pending) {
        fprintf(err, "error: QR not scanned in time - run ext pair again\n");
        ret = HEM_EXT_TIMEOUT;
        goto done;
    }

    /* Finish: device import, then tell the broker (and thus the phone). */
    rc = ehem_ext_validate(ctx, pr->pid, pr->reply, &val);
    if (rc != EHEM_OK) {
        fprintf(err, "error: ext/validate failed: %s\n",
                ehem_last_error(ctx)->message);
        if (ehem_last_error(ctx)->http_status == 406) {
            ret = HEM_EXT_REFUSED;
        }
        goto done;
    }
    rc = ehem_notify_register_finalise(ctx, o->notify_url, reg->rid,
                                       val->kid, val->code);
    if (rc != EHEM_OK) {
        fprintf(err, "error: register/finalise failed: %s (the device HAS "
                     "imported the key - remove kid %s with `keys rm` "
                     "before retrying)\n",
                ehem_last_error(ctx)->message, val->kid);
        goto done;
    }

    fprintf(out, "\nPaired. kid: %s\n", val->kid);
    ret = HEM_EXT_OK;

done:
    ehem_ext_validate_free(val);
    ehem_notify_pairing_reply_free(pr);
    ehem_notify_register_info_free(reg);
    ehem_ext_init_free(init);
    ehem_notify_string_free(epk);
    ehem_notify_string_free(payload);
    ehem_system_config_free(cfg);
    return ret;
}

/* -------------------------------------------------------------------------- */
/* ext list                                                                   */
/* -------------------------------------------------------------------------- */

int hem_ext_list_run(ehem_ctx *ctx, const char *passphrase, bool mobile,
                     FILE *out, FILE *err_in)
{
    FILE *err = or_stderr(err_in);
    ehem_key_page *page = NULL;
    size_t i, shown = 0;
    ehem_rc rc;

    out = or_stdout(out);
    if (ctx == NULL) {
        return HEM_EXT_USAGE;
    }
    rc = hem_tool_login(ctx, passphrase, mobile, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_EXT_USAGE : HEM_EXT_RUNTIME;
    }
    rc = ehem_key_search_all(ctx, (const uint8_t *)"EXTAID", 6,
                             EHEM_KEY_SEARCH_PREFIX, &page);
    if (rc != EHEM_OK) {
        fprintf(err, "error: %s\n", ehem_last_error(ctx)->message);
        return hem_tool_auth_exit(rc, err, HEM_EXT_RUNTIME);
    }

    for (i = 0; i < page->listed; i++) {
        const ehem_key_entry *e = &page->entries[i];
        fprintf(out, "%s  %-32s%s\n", e->kid,
                e->label != NULL ? e->label : "(no label)",
                (e->label != NULL && hem_key_is_protected(e->label))
                    ? "  [PROTECTED]" : "");
        if (e->descr != NULL && e->descr_len > 6) {
            fprintf(out, "  pid: ");
            hem_tool_fprint_b64(out, e->descr + 6, e->descr_len - 6);
            fputc('\n', out);
        }
        shown++;
    }
    fprintf(out, "%zu paired authenticator%s\n", shown,
            shown == 1 ? "" : "s");
    ehem_key_page_free(page);
    return HEM_EXT_OK;
}

/* -------------------------------------------------------------------------- */
/* ext login                                                                  */
/* -------------------------------------------------------------------------- */

int hem_ext_login_run(ehem_ctx *ctx, const hem_ext_login_opts *o)
{
    FILE *out, *err;
    const char *scope;
    ehem_rc rc;

    if (ctx == NULL || o == NULL) {
        return HEM_EXT_USAGE;
    }
    out = or_stdout(o->out);
    err = or_stderr(o->err);
    scope = (o->scope != NULL) ? o->scope : "system:config";

    /* Optional pre-check: with a passphrase we can see whether ANY
     * authenticator is paired — without one, an unanswerable push simply
     * times out (the broker pushes to nobody). */
    if (o->passphrase != NULL) {
        ehem_key_page *page = NULL;
        if (hem_tool_login(ctx, o->passphrase, false, err) == EHEM_OK &&
            ehem_key_search_all(ctx, (const uint8_t *)"EXTAID", 6,
                                EHEM_KEY_SEARCH_PREFIX, &page) == EHEM_OK) {
            size_t n = page->listed;
            ehem_key_page_free(page);
            if (n == 0) {
                fprintf(err, "error: no authenticator is paired - run "
                             "`hem-tool ext pair` first\n");
                return HEM_EXT_NOAUTH;
            }
        }
        /* Pre-check trouble is not fatal — fall through to the real flow. */
    }

    if (ehem_login_mobile(ctx) != EHEM_OK) {
        fprintf(err, "error: %s\n", ehem_last_error(ctx)->message);
        return HEM_EXT_RUNTIME;
    }

    fprintf(out, "Push sent - approve scope \"%s\" on your phone...\n",
            scope);

    if (strcmp(scope, "system:config") == 0) {
        /* The demo binding: the confirmation gates a real config read. */
        ehem_config_info *cfg = NULL;
        rc = ehem_system_config(ctx, &cfg);
        if (rc == EHEM_OK) {
            fprintf(out, "Approved. hostname: %s  user: %s\n",
                    cfg->hostname != NULL ? cfg->hostname : "?",
                    cfg->user != NULL ? cfg->user : "?");
            ehem_system_config_free(cfg);
        }
    } else {
        /* Arbitrary scope: drive the confirm engine directly and report
         * the cached-bearer outcome. */
        ehem_ext_confirm *confirm = NULL;
        long timeout = (o->timeout_ms > 0) ? o->timeout_ms : 60000L;
        rc = ehem_ext_confirm_begin(ctx, NULL, scope, NULL, o->note,
                                    &confirm);
        if (rc == EHEM_OK) {
            rc = ehem_ext_confirm_wait(ctx, confirm, timeout);
            ehem_ext_confirm_cancel(confirm);
        }
        if (rc == EHEM_OK) {
            fprintf(out, "Approved. Bearer for \"%s\" acquired and cached.\n",
                    scope);
        }
    }

    if (rc == EHEM_OK) {
        return HEM_EXT_OK;
    }
    fprintf(err, "error: %s\n", ehem_last_error(ctx)->message);
    if (rc == EHEM_ERR_USER_REJECTED) {
        fprintf(out, "Rejected on the phone.\n");
        return HEM_EXT_REFUSED;
    }
    if (rc == EHEM_ERR_CONFIRM_TIMEOUT) {
        fprintf(out, "No answer on the phone in time.\n");
        return HEM_EXT_TIMEOUT;
    }
    return HEM_EXT_RUNTIME;
}
