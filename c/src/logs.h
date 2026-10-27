/*
 * logs.h — hem-tool `logs` subcommands (audit-log access).
 *
 * implements: REQ-TOOL-012 (`logs list` / `logs get <id>` / `logs key`)
 *
 * hem-tool-core: the CLI, the unit tests, and the live demos drive one code
 * path, using ONLY the public SDK API. Local signature verification stays
 * OUT of the tool (the shim is not public API); `logs key` prints the
 * material for external verifiers.
 */
#ifndef HEM_TOOL_LOGS_H
#define HEM_TOOL_LOGS_H

#include <stdbool.h>
#include <stdio.h>

#include "ehem/ehem.h"

enum {
    HEM_LOGS_OK      = 0,
    HEM_LOGS_RUNTIME = 1,   /* login / device failure (message printed) */
    HEM_LOGS_USAGE   = 2    /* missing passphrase / bad arguments */
};

typedef struct {
    const char *passphrase;   /* login passphrase; NULL → HEM_LOGS_USAGE */
    bool        mobile;       /* --mobile: push-confirm auth (REQ-TOOL-018) */
    FILE       *out;          /* payload output (NULL → stdout) */
    FILE       *err;          /* diagnostics (NULL → stderr) */
} hem_logs_opts;

/*
 * `hem-tool logs list` (REQ-TOOL-012): walk ALL listing pages (advancing by
 * each page's returned count) and print one file id per line on o->out;
 * a "total: N" footer goes to o->err so stdout stays machine-consumable.
 * On an EPA device (route absent → NOT_FOUND) prints an explanatory message
 * and returns HEM_LOGS_RUNTIME.
 */
int hem_logs_list_run(ehem_ctx *ctx, const hem_logs_opts *o);

/*
 * `hem-tool logs get <id> [--out FILE]` (REQ-TOOL-012): download one log
 * file and write its bytes VERBATIM to `out_file` (binary mode) or, when
 * out_file is NULL, to o->out (put into binary mode on Windows — the sign
 * discipline). Unknown id → HEM_LOGS_RUNTIME.
 */
int hem_logs_get_run(ehem_ctx *ctx, const hem_logs_opts *o, const char *id,
                     const char *out_file);

/*
 * `hem-tool logs key` (REQ-TOOL-012): print the log-signing material as
 * three labeled base64 lines (key / nonce / nonce_signed) on o->out.
 */
int hem_logs_key_run(ehem_ctx *ctx, const hem_logs_opts *o);

#endif /* HEM_TOOL_LOGS_H */
