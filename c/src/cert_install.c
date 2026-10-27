/*
 * cert_install.c — implementation of hem-tool `cert-install` (REQ-TOOL-003).
 *
 * Uses only the public SDK API (include/ehem/), so the same code drives the
 * real device from main() and the fake transport from the unit test. The
 * sequence and its failure modes are documented in cert_install.h.
 */
#define _POSIX_C_SOURCE 199309L   /* nanosleep / struct timespec under -std=c99 */

#include "cert_install.h"
#include "tool_auth.h"

#include <string.h>

#include "ehem/auth.h"
#include "ehem/system.h"

#ifdef _WIN32
#  include <windows.h>            /* Sleep() — MinGW has no POSIX nanosleep */
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

/* Print the last-error detail recorded on the context. */
static void report(FILE *err, ehem_ctx *ctx, ehem_rc rc, const char *what)
{
    const ehem_error *e = ehem_last_error(ctx);
    fprintf(err, "error: %s: %s\n", what, ehem_rc_str(rc));
    if (e != NULL) {
        if (e->message != NULL && e->message[0] != '\0') {
            fprintf(err, "  detail: %s\n", e->message);
        }
        if (e->device_payload != NULL) {
            fprintf(err, "  device: %s\n", e->device_payload);
        }
    }
}

int hem_cert_install_run(ehem_ctx *ctx, const hem_cert_install_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    unsigned attempts = (o->poll_attempts != 0) ? o->poll_attempts
                                                : HEM_CERT_DEFAULT_POLL_ATTEMPTS;
    unsigned delay_ms = o->poll_delay_ms;   /* verbatim — 0 means no sleep */
    ehem_checkin_info      *ci   = NULL;
    ehem_cert_info         *leaf = NULL;
    ehem_cert_install_info *inst = NULL;
    ehem_status_info       *st   = NULL;
    ehem_rc rc;
    int ret;
    unsigned i;

    /* 1. Harvest the cloud-delivered chain via the check-in handshake. */
    fprintf(out, "check-in: asking the Encedo cloud for this device's certificate...\n");
    rc = ehem_system_checkin(ctx, &ci);
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "check-in");
        return HEM_CERT_CHECKIN_FAILED;
    }
    if (ci->newcrt_chain == NULL) {
        /* The broker only delivers a chain when it considers the device's cert
         * stale — it compares the leg-1 `csn` (the device's current serial) to
         * the managed cert. So "no chain, but the device reports a serial" is
         * the broker's own skip-if-current signal: the device is up to date. */
        if (ci->current_serial != NULL) {
            fprintf(out, "already current: the cloud reports no certificate "
                         "update; the device serves serial=%s - nothing to do.\n",
                    ci->current_serial);
            if (o->force) {
                fprintf(out, "  (--force cannot reinstall without a "
                             "cloud-delivered chain)\n");
            }
            ret = HEM_CERT_OK;
        } else {
            fprintf(err, "error: the cloud delivered no certificate and the "
                         "device reports none loaded - nothing to install\n"
                         "  (this device's hostname may be outside the managed "
                         "domain)\n");
            ret = HEM_CERT_NO_CHAIN;
        }
        goto cleanup;
    }

    /* 2. Inspect the harvested leaf (serial + validity for the decision/summary). */
    rc = ehem_cert_inspect(ctx, ci->newcrt_chain, &leaf);
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "reading the delivered certificate");
        ret = HEM_CERT_PARSE_FAILED;
        goto cleanup;
    }
    fprintf(out, "delivered: CN=%s serial=%s\n           valid %s .. %s\n",
            leaf->subject_cn, leaf->serial, leaf->not_before, leaf->not_after);

    /* 3. Skip-if-current: compare the device's serial (leg-1 csn) to the leaf. */
    if (ci->current_serial != NULL) {
        fprintf(out, "device currently serves serial=%s\n", ci->current_serial);
        if (strcmp(ci->current_serial, leaf->serial) == 0 && !o->force) {
            fprintf(out, "already current: the device already serves this "
                         "certificate - nothing to do.\n"
                         "  (pass --force to reinstall it anyway)\n");
            ret = HEM_CERT_OK;
            goto cleanup;
        }
    }

    /* From here on the tool MUTATES device state (install + reboot). The
     * credential check stays HERE, after skip-if-current, so a
     * credential-less run can still exit 0 "already current". */
    if (!o->mobile && (o->passphrase == NULL || o->passphrase[0] == '\0')) {
        fprintf(err, "error: installing needs authentication - "
                     "set EHEM_PASSPHRASE / pass --passphrase, or use "
                     "--mobile\n");
        ret = HEM_CERT_NO_PASSPHRASE;
        goto cleanup;
    }
    fprintf(out, "installing the new certificate and rebooting the device...\n");

    rc = hem_tool_login(ctx, o->passphrase, o->mobile, err);
    if (rc != EHEM_OK) {
        ret = HEM_CERT_AUTH_FAILED;
        goto cleanup;
    }

    /* 4. Install (POST /api/system/config {"tls":{"crt":...}}, scope system:config). */
    rc = ehem_system_config_install_cert(ctx, ci->newcrt_chain, &inst);
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "installing the certificate");
        ret = (rc == EHEM_ERR_AUTH_FAILED || rc == EHEM_ERR_AUTH_EXPIRED ||
               rc == EHEM_ERR_SCOPE_DENIED) ? HEM_CERT_AUTH_FAILED
                                            : HEM_CERT_INSTALL_FAILED;
        ret = hem_tool_auth_exit(rc, err, ret);
        goto cleanup;
    }
    if (!inst->updated) {
        fprintf(err, "error: the device did not store the certificate\n");
        ret = HEM_CERT_INSTALL_FAILED;
        goto cleanup;
    }

    /* 5. Reboot (the firmware loads the TLS cert from flash only at boot). */
    rc = ehem_system_reboot(ctx);
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "reboot");
        ret = HEM_CERT_REBOOT_FAILED;
        goto cleanup;
    }

    /* 6. Poll until the device answers again, tolerating the reboot window. */
    fprintf(out, "waiting for the device to come back...\n");
    rc = EHEM_ERR_UNREACHABLE;
    for (i = 0; i < attempts; i++) {
        if (i > 0) {
            sleep_ms(delay_ms);
        }
        ehem_system_status_free(st);
        st = NULL;
        rc = ehem_system_status(ctx, &st);
        if (rc == EHEM_OK) {
            break;
        }
    }
    if (rc != EHEM_OK) {
        if (o->insecure) {
            fprintf(err, "error: the device did not respond within the timeout "
                         "after reboot\n");
        } else {
            fprintf(err, "error: after reboot the device did not come back with a "
                         "system-trusted certificate\n"
                         "  (the rotation may have failed, or it is still booting)\n");
        }
        ret = HEM_CERT_DEVICE_TIMEOUT;
        goto cleanup;
    }

    /* 7. Verify + summary. A successful status under a verifying TLS mode proves
     *    the rotation end to end; under --insecure it cannot. */
    fprintf(out, "done: certificate rotated.\n");
    if (ci->current_serial != NULL) {
        fprintf(out, "  serial:   %s -> %s\n", ci->current_serial, leaf->serial);
    } else {
        fprintf(out, "  serial:   %s\n", leaf->serial);
    }
    fprintf(out, "  validity: %s .. %s\n", leaf->not_before, leaf->not_after);
    if (o->insecure) {
        fprintf(out, "  verify:   SKIPPED (insecure TLS mode) - reconnect with "
                     "system trust to confirm\n");
    } else {
        fprintf(out, "  verify:   OK - the device now serves a trusted certificate\n");
    }
    ret = HEM_CERT_OK;

cleanup:
    ehem_system_status_free(st);
    ehem_cert_install_free(inst);
    ehem_cert_info_free(leaf);
    ehem_checkin_result_free(ci);
    return ret;
}
