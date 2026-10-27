/*
 * random.c — hem-tool `random` subcommand: device hardware-RNG bytes.
 *
 * implements: REQ-TOOL-010
 *
 * Public-API-only (include/ehem/), so the same code drives the real device
 * from main() and the fake transport from the unit test.
 */
#include "random.h"
#include "tool_auth.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "ehem/auth.h"
#include "ehem/crypto.h"
#include "ehem/keymgmt.h"

#include "keys.h"   /* hem_tool_kid_ok, hem_tool_fprint_hex, report style */

/* Print the last-error detail recorded on the context (mirrors sign.c). */
static void report(FILE *err, ehem_ctx *ctx, ehem_rc rc, const char *what)
{
    const ehem_error *e = ehem_last_error(ctx);
    fprintf(err, "error: %s: %s\n", what, ehem_rc_str(rc));
    if (e != NULL) {
        if (e->message != NULL && e->message[0] != '\0') {
            fprintf(err, "  detail: %s\n", e->message);
        }
        if (e->http_status != 0) {
            fprintf(err, "  http status: %ld\n", e->http_status);
        }
        if (e->device_payload != NULL) {
            fprintf(err, "  device: %s\n", e->device_payload);
        }
    }
}

/* Parse N: decimal digits only, 1..HEM_RANDOM_N_MAX. Returns 0 on failure. */
static long parse_count(const char *s)
{
    long v = 0;
    if (s == NULL || *s == '\0') {
        return 0;
    }
    for (; *s != '\0'; s++) {
        if (!isdigit((unsigned char)*s)) {
            return 0;
        }
        v = v * 10 + (*s - '0');
        if (v > HEM_RANDOM_N_MAX) {
            return -1;
        }
    }
    return v;
}

int hem_random_run(ehem_ctx *ctx, const hem_random_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    static uint8_t buf[HEM_RANDOM_N_MAX];
    char created_kid[EHEM_KID_HEX_SIZE];
    const char *kid = o->kid;
    long n;
    ehem_rc rc;
    ehem_rc del_rc = EHEM_OK;

    n = parse_count(o->count_arg);
    if (n <= 0) {
        fprintf(err, "error: 'random' needs a byte count N (1..%d)\n",
                HEM_RANDOM_N_MAX);
        return HEM_RANDOM_USAGE;
    }
    if (kid != NULL && !hem_tool_kid_ok(kid)) {
        fprintf(err, "error: --kid must be exactly 32 hex chars\n");
        return HEM_RANDOM_USAGE;
    }

    rc = hem_tool_login(ctx, o->passphrase, o->mobile, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_RANDOM_USAGE : HEM_RANDOM_RUNTIME;
    }

    /* No --kid: transient EHEMTEST AES-128 key (REQ-OPS-002 keeps key
     * creation OUT of the SDK; the tool owns the lifecycle). */
    if (kid == NULL) {
        ehem_key_create_params p;
        memset(&p, 0, sizeof p);
        p.type = "AES128";
        p.label = "EHEMTEST hem-tool random";
        rc = ehem_key_create(ctx, &p, created_kid);
        if (rc != EHEM_OK) {
            report(err, ctx, rc, "random (transient key create)");
            return hem_tool_auth_exit(rc, err, HEM_RANDOM_RUNTIME);
        }
        kid = created_kid;
    }

    rc = ehem_random(ctx, kid, buf, (size_t)n);

    /* The delete runs whether the harvest succeeded or not. */
    if (kid == created_kid) {
        del_rc = ehem_key_delete(ctx, created_kid);
        if (del_rc != EHEM_OK) {
            fprintf(err, "warning: could not delete the transient key %s - "
                         "remove it with 'hem-tool keys rm'\n", created_kid);
        }
    }

    if (rc != EHEM_OK) {
        report(err, ctx, rc, "random");
        return hem_tool_auth_exit(rc, err, HEM_RANDOM_RUNTIME);
    }

    if (o->raw) {
#ifdef _WIN32
        _setmode(_fileno(out), _O_BINARY);
#endif
        fwrite(buf, 1, (size_t)n, out);
    } else {
        hem_tool_fprint_hex(out, buf, (size_t)n);
        fprintf(out, "\n");
    }
    memset(buf, 0, (size_t)n);   /* entropy handed over; keep no copy */

    return (del_rc == EHEM_OK) ? HEM_RANDOM_OK : HEM_RANDOM_RUNTIME;
}
