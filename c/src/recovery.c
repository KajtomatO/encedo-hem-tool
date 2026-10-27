/*
 * recovery.c — hem-tool `recovery` (see recovery.h).
 *
 * implements: REQ-TOOL-023
 *
 * The decision tree is the record of every TLS/certificate incident so far:
 * expired certificate (M1 gate 2026-07-16, again 2026-10-06 — check-in, then
 * cert-install under --insecure, verify under system trust), TLS material lost
 * after the 2026-07-22 wipe (tls-recover via the provisioning cloud), RTC
 * unset after a cold boot and the ~8 % clock drift (a check-in). The cloud is
 * tried ONCE per run (user decision 2026-10-07).
 */
#include "recovery.h"

#include <stdio.h>
#include <string.h>

#include "ehem/auth.h"
#include "ehem/system.h"
#include "cert_install.h"
#include "recover.h"

static void report(FILE *err, ehem_ctx *ctx, ehem_rc rc, const char *what)
{
    const ehem_error *e = ehem_last_error(ctx);
    fprintf(err, "error: %s: %s", what, ehem_rc_str(rc));
    if (e->http_status != 0) {
        fprintf(err, " (HTTP %ld)", e->http_status);
    }
    if (e->message[0] != '\0') {
        fprintf(err, " - %s", e->message);
    }
    fputc('\n', err);
}

/* Case 1: the device answers under the configured trust. One check-in sets
 * the RTC after a cold boot and resyncs the drifting clock; nothing else. */
static int healthy(const hem_recovery_opts *o, FILE *out, FILE *err, int https_flag)
{
    ehem_checkin_info *ci = NULL;
    ehem_rc rc;

    /* https_flag: 1 = reports on, 0 = reports off, -1 = not reported (the
     * healthy dev device omits the field over HTTPS). */
    fprintf(out, "healthy: the device answers under the configured trust%s\n",
            https_flag == 0 ? " (it reports https: off - plain-http device URL?)" : "");
    fprintf(out, "check-in: syncing the device clock...\n");
    rc = ehem_system_checkin(o->ctx_https, &ci);
    ehem_checkin_result_free(ci);
    if (rc != EHEM_OK) {
        report(err, o->ctx_https, rc, "check-in");
        return HEM_RECOVERY_RUNTIME;
    }
    fprintf(out, "nothing to recover\n");
    return HEM_RECOVERY_OK;
}

/* Case 2: the certificate has expired. One check-in asks the cloud for the
 * renewal; delivered → cert-install (insecure leg) → verify under trust. */
static int expired(const hem_recovery_opts *o, FILE *out, FILE *err,
                   const char *detail)
{
    ehem_checkin_info *ci = NULL;
    ehem_status_info *st = NULL;
    hem_cert_install_opts co;
    ehem_rc rc;
    int ci_rc;

    fprintf(out, "diagnosis: the device certificate has EXPIRED (%s)\n", detail);
    if (o->ctx_insecure == NULL) {
        fprintf(err, "error: no insecure-leg context available\n");
        return HEM_RECOVERY_USAGE;
    }
    fprintf(out, "check-in (relaxed TLS): asking the cloud for a renewed "
                 "certificate - one attempt...\n");
    rc = ehem_system_checkin(o->ctx_insecure, &ci);
    if (rc != EHEM_OK) {
        report(err, o->ctx_insecure, rc, "check-in");
        return HEM_RECOVERY_RUNTIME;
    }
    if (ci->newcrt_chain == NULL) {
        ehem_checkin_result_free(ci);
        fprintf(out, "the cloud has not issued a renewal yet: the check-in "
                     "delivered no certificate chain. The cloud is tried once "
                     "per run - rerun `hem-tool recovery` later (the 2026-10-07 "
                     "renewal arrived ~45 min after the first post-expiry "
                     "check-in). Until then: --insecure for urgent calls.\n");
        return HEM_RECOVERY_NO_RENEWAL;
    }
    ehem_checkin_result_free(ci);

    fprintf(out, "renewal delivered - installing it (cert-install, insecure "
                 "leg; REBOOTS the device)...\n");
    memset(&co, 0, sizeof co);
    co.passphrase    = o->passphrase;
    co.insecure      = 1;
    co.poll_attempts = o->poll_attempts;
    co.poll_delay_ms = o->poll_delay_ms;
    co.out           = out;
    co.err           = err;
    ci_rc = hem_cert_install_run(o->ctx_insecure, &co);
    if (ci_rc != HEM_CERT_OK) {
        fprintf(err, "error: cert-install failed (exit %d) - see above\n", ci_rc);
        return HEM_RECOVERY_RUNTIME;
    }

    /* The proof: a status under the configured trust. */
    rc = ehem_system_status(o->ctx_https, &st);
    if (rc != EHEM_OK) {
        report(err, o->ctx_https, rc, "verify");
        fprintf(err, "error: the device still does not verify under the "
                     "configured trust after the install\n");
        return HEM_RECOVERY_RUNTIME;
    }
    ehem_system_status_free(st);
    fprintf(out, "recovered: the device serves a trusted certificate again\n");
    return HEM_RECOVERY_OK;
}

int hem_recovery_run(const hem_recovery_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    ehem_status_info *st = NULL;
    char detail[512];
    int https_flag;
    ehem_rc rc;

    /* Fail fast, zero traffic. */
    if (o->mobile) {
        fprintf(err, "error: recovery cannot use --mobile - its install legs "
                     "are config writes the device accepts only from a "
                     "passphrase session (token sub=\"U\" or \"M\"); pass "
                     "--passphrase / set EHEM_PASSPHRASE\n");
        return HEM_RECOVERY_USAGE;
    }
    if (o->passphrase == NULL || o->passphrase[0] == '\0') {
        fprintf(err, "error: no passphrase - pass --passphrase / set "
                     "EHEM_PASSPHRASE\n");
        return HEM_RECOVERY_USAGE;
    }
    if (o->ctx_https == NULL) {
        fprintf(err, "error: no device context\n");
        return HEM_RECOVERY_USAGE;
    }

    /* 1. The probe under the configured trust (auto check-in off, so the
     *    verdict surfaces instead of being silently repaired). */
    fprintf(out, "probing the device under the configured trust...\n");
    rc = ehem_system_status(o->ctx_https, &st);
    if (rc == EHEM_OK) {
        https_flag = st->has_https ? (st->https ? 1 : 0) : -1;
        ehem_system_status_free(st);
        return healthy(o, out, err, https_flag);
    }
    snprintf(detail, sizeof detail, "%s", ehem_last_error(o->ctx_https)->message);

    /* 2. Expired certificate — the public REQ-NET-005 classification. */
    if (ehem_last_error(o->ctx_https)->tls_expired) {
        return expired(o, out, err, detail);
    }

    /* 3/4/5. Not expired: look at the device over plain http. */
    fprintf(out, "https probe failed (%s: %s) - looking over http://...\n",
            ehem_rc_str(rc), detail);
    if (o->ctx_http == NULL) {
        fprintf(err, "error: no http:// context available\n");
        return HEM_RECOVERY_USAGE;
    }
    st = NULL;
    rc = ehem_system_status(o->ctx_http, &st);
    if (rc != EHEM_OK) {
        fprintf(err, "error: the device answers neither over https nor over "
                     "http (%s) - unreachable: check power/cabling and "
                     "power-cycle it (the known sustained-load stall needs "
                     "that), then rerun\n", ehem_last_error(o->ctx_http)->message);
        return HEM_RECOVERY_UNREACHABLE;
    }
    https_flag = st->has_https ? (st->https ? 1 : 0) : -1;
    ehem_system_status_free(st);

    if (https_flag != 0) {
        /* Reported up, or not reported at all: never run the destructive
         * tls-recover on a guess. */
        fprintf(err, "error: the device answers over http and %s, but the "
                     "https probe failed for a reason that is not an expired "
                     "certificate (%s). Not auto-recoverable: check the "
                     "hostname and the trust store (--cacert FILE), inspect "
                     "with `hem-tool --insecure status`, or - only if you KNOW "
                     "the TLS material is gone - run `hem-tool --url http://"
                     "<host> tls-recover` yourself. Nothing was changed.\n",
                https_flag == 1 ? "reports HTTPS up"
                                : "does not report its HTTPS state",
                detail);
        return HEM_RECOVERY_TLS_OTHER;
    }

    /* Case 3: TLS material lost — the post-wipe state. */
    fprintf(out, "diagnosis: HTTPS is down (the device reports https: off) - "
                 "TLS material lost; running tls-recover (provisioning cloud, "
                 "REBOOTS the device)...\n");
    {
        hem_recover_opts ro;
        int rr;
        memset(&ro, 0, sizeof ro);
        ro.passphrase    = o->passphrase;
        ro.register_url  = o->register_url;
        ro.force         = 1;                  /* we have just seen https off */
        ro.poll_attempts = o->poll_attempts;
        ro.poll_delay_ms = o->poll_delay_ms;
        ro.out           = out;
        ro.err           = err;
        rr = hem_tls_recover_run(o->ctx_http, &ro);
        switch (rr) {
        case HEM_RECOVER_OK:      return HEM_RECOVERY_OK;
        case HEM_RECOVER_USAGE:   return HEM_RECOVERY_USAGE;
        case HEM_RECOVER_TIMEOUT: return HEM_RECOVERY_UNREACHABLE;
        default:                  return HEM_RECOVERY_RUNTIME;
        }
    }
}
