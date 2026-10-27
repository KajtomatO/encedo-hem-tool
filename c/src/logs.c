/*
 * logs.c — hem-tool `logs list` / `logs get` / `logs key`.
 *
 * implements: REQ-TOOL-012
 */
#include "logs.h"
#include "tool_auth.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "ehem/auth.h"
#include "ehem/logger.h"
#include "keys.h"    /* hem_tool_fprint_b64 + the shared report idiom */

/* Print SDK failure detail (mirrors keys.c's report). */
static void lreport(FILE *err, ehem_ctx *ctx, ehem_rc rc, const char *what)
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

static int logs_login(ehem_ctx *ctx, const hem_logs_opts *o, FILE *err)
{
    ehem_rc rc;
    rc = hem_tool_login(ctx, o->passphrase, o->mobile, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_LOGS_USAGE : HEM_LOGS_RUNTIME;
    }
    return HEM_LOGS_OK;
}

/* NOT_FOUND on the PPA-only routes usually means an EPA device. */
static int epa_or_runtime(FILE *err, ehem_ctx *ctx, ehem_rc rc,
                          const char *what)
{
    if (rc == EHEM_ERR_NOT_FOUND) {
        fprintf(err, "error: %s: not available on this device (EPA build "
                     "without a microSD, or nothing to fetch)\n", what);
    } else {
        lreport(err, ctx, rc, what);
    }
    return hem_tool_auth_exit(rc, err, HEM_LOGS_RUNTIME);
}

int hem_logs_list_run(ehem_ctx *ctx, const hem_logs_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    size_t offset = 0;
    int64_t total = 0;
    size_t i;
    int ret;

    ret = logs_login(ctx, o, err);
    if (ret != HEM_LOGS_OK) {
        return ret;
    }

    do {
        ehem_logger_page *page = NULL;
        ehem_rc rc = ehem_logger_list(ctx, offset, &page);
        if (rc != EHEM_OK) {
            return epa_or_runtime(err, ctx, rc, "logs list");
        }
        for (i = 0; i < page->count; i++) {
            fprintf(out, "%s\n", page->ids[i]);
        }
        total = page->total;
        if (page->count == 0) {
            ehem_logger_page_free(page);
            break;                       /* defensive: no progress → stop */
        }
        offset += page->count;
        ehem_logger_page_free(page);
    } while ((int64_t)offset < total);

    fprintf(err, "total: %lld\n", (long long)total);
    return HEM_LOGS_OK;
}

int hem_logs_get_run(ehem_ctx *ctx, const hem_logs_opts *o, const char *id,
                     const char *out_file)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    uint8_t *data = NULL;
    size_t len = 0;
    FILE *f;
    int ret;
    ehem_rc rc;

    if (id == NULL || id[0] == '\0') {
        fprintf(err, "error: logs get needs a log file id (see logs list)\n");
        return HEM_LOGS_USAGE;
    }
    ret = logs_login(ctx, o, err);
    if (ret != HEM_LOGS_OK) {
        return ret;
    }

    rc = ehem_logger_get(ctx, id, &data, &len);
    if (rc == EHEM_ERR_ARG) {
        lreport(err, ctx, rc, "logs get");
        return HEM_LOGS_USAGE;
    }
    if (rc != EHEM_OK) {
        return epa_or_runtime(err, ctx, rc, "logs get");
    }

    if (out_file != NULL) {
        f = fopen(out_file, "wb");
        if (f == NULL) {
            fprintf(err, "error: cannot open '%s' for writing\n", out_file);
            ehem_logger_file_free(data);
            return HEM_LOGS_RUNTIME;
        }
    } else {
        f = out;
#ifdef _WIN32
        _setmode(_fileno(f), _O_BINARY);   /* bytes verbatim, as sign --raw */
#endif
    }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        fprintf(err, "error: short write%s%s\n",
                out_file != NULL ? " to " : "",
                out_file != NULL ? out_file : "");
        if (out_file != NULL) {
            fclose(f);
        }
        ehem_logger_file_free(data);
        return HEM_LOGS_RUNTIME;
    }
    if (out_file != NULL) {
        fclose(f);
        fprintf(err, "wrote %lu bytes to %s\n", (unsigned long)len, out_file);
    }
    ehem_logger_file_free(data);
    return HEM_LOGS_OK;
}

int hem_logs_key_run(ehem_ctx *ctx, const hem_logs_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    ehem_logger_key_info *info = NULL;
    int ret;
    ehem_rc rc;

    ret = logs_login(ctx, o, err);
    if (ret != HEM_LOGS_OK) {
        return ret;
    }
    rc = ehem_logger_key(ctx, &info);
    if (rc != EHEM_OK) {
        return epa_or_runtime(err, ctx, rc, "logs key");
    }

    fprintf(out, "key:          ");
    hem_tool_fprint_b64(out, info->key, EHEM_LOGGER_KEY_SIZE);
    fprintf(out, "\nnonce:        ");
    hem_tool_fprint_b64(out, info->nonce, EHEM_LOGGER_NONCE_SIZE);
    fprintf(out, "\nnonce_signed: ");
    hem_tool_fprint_b64(out, info->nonce_signed, EHEM_LOGGER_SIG_SIZE);
    fprintf(out, "\n");
    fprintf(err, "Ed25519 log-signing key + freshly signed nonce; verify "
                 "externally to confirm the device holds the private half\n");

    ehem_logger_key_free(info);
    return HEM_LOGS_OK;
}
