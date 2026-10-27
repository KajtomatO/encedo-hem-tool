/*
 * selftest.c — hem-tool `selftest`.
 *
 * implements: REQ-TOOL-013
 */
#include "selftest.h"
#include "tool_auth.h"

#include <time.h>

#include "ehem/auth.h"
#include "ehem/system.h"

static void sreport(FILE *err, ehem_ctx *ctx, ehem_rc rc, const char *what)
{
    const ehem_error *e = ehem_last_error(ctx);
    fprintf(err, "error: %s: %s\n", what, ehem_rc_str(rc));
    if (e != NULL && e->message != NULL && e->message[0] != '\0') {
        fprintf(err, "  detail: %s\n", e->message);
    }
    if (e != NULL && e->http_status != 0) {
        fprintf(err, "  http status: %ld\n", e->http_status);
    }
}

/* Render a unix timestamp as UTC "YYYY-MM-DD HH:MM:SSZ" ("-" when 0). */
static void fprint_ts(FILE *f, const char *name, int64_t ts)
{
    if (ts <= 0) {
        fprintf(f, "  %-20s -\n", name);
        return;
    }
    {
        /* Plain gmtime(): the CLI is single-threaded, and it avoids the
         * strict-C99 visibility gymnastics of gmtime_r/gmtime_s. */
        time_t t = (time_t)ts;
        struct tm *tmv = gmtime(&t);
        char buf[32];
        if (tmv == NULL) {
            fprintf(f, "  %-20s (unrepresentable)\n", name);
            return;
        }
        strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%SZ", tmv);
        fprintf(f, "  %-20s %s\n", name, buf);
    }
}

int hem_selftest_run(ehem_ctx *ctx, const hem_selftest_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    ehem_selftest_info *info = NULL;
    ehem_rc rc;
    int ret;

    rc = hem_tool_login(ctx, o->passphrase, o->mobile, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_SELFTEST_USAGE : HEM_SELFTEST_RUNTIME;
    }

    fprintf(err, "note: selftest re-runs the device's test battery on every "
                 "call - avoid tight polling\n");
    rc = ehem_system_selftest(ctx, &info);
    if (rc != EHEM_OK) {
        sreport(err, ctx, rc, "selftest");
        return hem_tool_auth_exit(rc, err, HEM_SELFTEST_RUNTIME);
    }

    fprintf(out, "selftest: %s (fls_state %lld)\n",
            info->fls_state == 0 ? "PASS" : "FAIL",
            (long long)info->fls_state);
    fprint_ts(out, "run at", info->selftest_ts);
    fprint_ts(out, "previous run", info->last_selftest_ts);
    fprint_ts(out, "last entropy test", info->last_entropytest_ts);
    fprint_ts(out, "last KAT pass", info->last_kat_ts);
    if (info->kat_busy) {
        fprintf(out, "  %-20s yes\n", "KAT running");
    }
    if (info->se_state >= 0) {
        fprintf(out, "  %-20s %lld\n", "secure enclave", (long long)info->se_state);
    }
    if (info->repo_total >= 0) {
        fprintf(out, "key repository:\n");
        fprintf(out, "  %-20s %lld\n", "keys", (long long)info->repo_total);
        fprintf(out, "  %-20s %lld\n", "deleted slots", (long long)info->repo_deleted);
        fprintf(out, "  %-20s %lld\n", "invalid slots", (long long)info->repo_invalid);
        fprintf(out, "  %-20s %lld\n", "fragmented", (long long)info->repo_fragmented);
        fprintf(out, "  %-20s %lld\n", "free slots", (long long)info->repo_freeslots);
    }

    ret = (info->fls_state == 0) ? HEM_SELFTEST_OK : HEM_SELFTEST_FAILSTATE;
    ehem_selftest_free(info);
    return ret;
}
