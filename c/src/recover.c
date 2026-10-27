/*
 * recover.c — hem-tool `tls-recover`.
 *
 * implements: REQ-TOOL-015
 */
#define _POSIX_C_SOURCE 199309L   /* nanosleep / struct timespec under -std=c99 */

#include "recover.h"
#include "tool_auth.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>            /* Sleep() — MinGW has no POSIX nanosleep */
#else
#  include <time.h>               /* nanosleep / struct timespec */
#endif

#include "ehem/auth.h"
#include "ehem/system.h"

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

static void rreport(FILE *err, ehem_ctx *ctx, ehem_rc rc, const char *what)
{
    const ehem_error *e = ehem_last_error(ctx);
    fprintf(err, "error: %s: %s\n", what, ehem_rc_str(rc));
    if (e != NULL && e->message != NULL && e->message[0] != '\0') {
        fprintf(err, "  detail: %s\n", e->message);
    }
    if (e != NULL && e->http_status != 0) {
        fprintf(err, "  http status: %ld\n", e->http_status);
    }
    if (e != NULL && e->device_payload != NULL) {
        fprintf(err, "  payload: %s\n", e->device_payload);
    }
}

/* Read the status `https` flag: 1 = serving HTTPS, 0 = not, -1 = status
 * unavailable / flag absent. */
static int https_state(ehem_ctx *ctx)
{
    ehem_status_info *st = NULL;
    int state = -1;
    if (ehem_system_status(ctx, &st) == EHEM_OK) {
        state = (st->has_https && st->https) ? 1 : 0;
        ehem_system_status_free(st);
    }
    return state;
}

int hem_tls_recover_run(ehem_ctx *ctx, const hem_recover_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    unsigned attempts = (o->poll_attempts != 0) ? o->poll_attempts
                                                : HEM_RECOVER_POLL_ATTEMPTS;
    ehem_cert_install_info *info = NULL;
    ehem_rc rc;
    unsigned i;

    /* Fail fast BEFORE any probe: recovery MUTATES the device, so a run
     * that cannot possibly authenticate should produce zero traffic. */
    if (!o->mobile && (o->passphrase == NULL || o->passphrase[0] == '\0')) {
        fprintf(err, "error: no passphrase - pass --passphrase / set "
                     "EHEM_PASSPHRASE, or use --mobile\n");
        return HEM_RECOVER_USAGE;
    }

    /* 1. Skip-if-healthy: recovery is for a device that LOST its TLS end. */
    if (!o->force && https_state(ctx) == 1) {
        fprintf(out, "nothing to recover: the device already serves HTTPS "
                     "(certificate renewals: hem-tool cert-install)\n");
        return HEM_RECOVER_OK;
    }

    /* 2. Clock sync: after a cold boot the RTC is unset and login would fail.
     * Best-effort — the RTC may already be fine. */
    {
        ehem_checkin_info *ci = NULL;
        fprintf(out, "check-in: syncing the device clock...\n");
        rc = ehem_system_checkin(ctx, &ci);
        ehem_checkin_result_free(ci);
        if (rc != EHEM_OK) {
            fprintf(err, "warning: check-in failed (%s) - continuing, the "
                         "clock may already be set\n", ehem_rc_str(rc));
        }
    }

    /* 3. The recovery ceremony (REQ-SYS-013). */
    rc = hem_tool_login(ctx, o->passphrase, o->mobile, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_RECOVER_USAGE : HEM_RECOVER_RUNTIME;
    }
    fprintf(out, "recovering: attestation -> provisioning cloud -> "
                 "install...\n");
    rc = ehem_tls_recover(ctx, o->register_url, &info);
    if (rc == EHEM_ERR_PROTOCOL) {
        rreport(err, ctx, rc, "tls-recover");
        return HEM_RECOVER_NO_BUNDLE;
    }
    if (rc != EHEM_OK) {
        rreport(err, ctx, rc, "tls-recover");
        return hem_tool_auth_exit(rc, err, HEM_RECOVER_RUNTIME);
    }
    fprintf(out, "installed: updated=%s reboot_required=%s\n",
            info->updated ? "yes" : "no",
            info->reboot_required ? "yes" : "no");

    /* 4. Reboot so httpsd starts with the new material, then wait for the
     * device to come back SERVING HTTPS — keyed on the flag, not on mere
     * reachability (the old instance keeps answering for ~2 s). */
    if (info->reboot_required) {
        ehem_cert_install_free(info);
        rc = ehem_system_reboot(ctx);
        if (rc != EHEM_OK) {
            rreport(err, ctx, rc, "reboot");
            return hem_tool_auth_exit(rc, err, HEM_RECOVER_RUNTIME);
        }
        fprintf(out, "rebooting; waiting for the device to return with "
                     "HTTPS...\n");
        for (i = 0; i < attempts; i++) {
            /* poll_delay_ms is VERBATIM (0 = no sleep) — the cert-install
             * convention; the CLI passes the 2 s default explicitly. */
            sleep_ms(o->poll_delay_ms);
            if (https_state(ctx) == 1) {
                fprintf(out, "recovered: the device serves HTTPS again\n");
                return HEM_RECOVER_OK;
            }
        }
        fprintf(err, "error: device did not return serving HTTPS within the "
                     "wait - check it and re-run\n");
        return HEM_RECOVER_TIMEOUT;
    }
    ehem_cert_install_free(info);

    /* No reboot required (unusual) — verify in place. */
    if (https_state(ctx) == 1) {
        fprintf(out, "recovered: the device serves HTTPS\n");
        return HEM_RECOVER_OK;
    }
    fprintf(err, "error: install reported success but the device does not "
                 "serve HTTPS\n");
    return HEM_RECOVER_TIMEOUT;
}
