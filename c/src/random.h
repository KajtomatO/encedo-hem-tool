/*
 * random.h — hem-tool `random` subcommand.
 *
 * implements: REQ-TOOL-010 (`random <N>` — device hardware-RNG bytes via the
 *             REQ-OPS-002 encrypt-IV harvest; transient EHEMTEST AES key
 *             when --kid is not given)
 *
 * Lives in hem-tool-core (like keys.c / sign.c) so the CLI and the unit
 * tests drive one code path, using ONLY the public SDK API.
 */
#ifndef HEM_RANDOM_H
#define HEM_RANDOM_H

#include <stdbool.h>
#include <stdio.h>

#include "ehem/ehem.h"

/* Exit codes (the shared tool convention). */
enum {
    HEM_RANDOM_OK      = 0,  /* N bytes written */
    HEM_RANDOM_RUNTIME = 1,  /* login / device failure, or cleanup failure */
    HEM_RANDOM_USAGE   = 2   /* argument / environment error, no I/O done */
};

/* Bounds for N (arg-parse rule; 4096 = 256 round-trips worst case). */
#define HEM_RANDOM_N_MAX 4096

typedef struct {
    const char *passphrase;  /* login passphrase; NULL → HEM_RANDOM_USAGE */
    bool        mobile;       /* --mobile: push-confirm auth (REQ-TOOL-018) */
    const char *count_arg;   /* N as the CLI string; parsed/validated here so
                              * the unit tests cover the bounds (1..4096) */
    const char *kid;         /* existing AES key; NULL → create a transient
                              * "EHEMTEST hem-tool random" AES-128 key,
                              * harvest, and delete it (delete runs on the
                              * failure paths too) */
    int         raw;         /* nonzero → raw bytes only; else lowercase hex
                              * + newline */
    FILE       *out;         /* bytes/hex (NULL → stdout) */
    FILE       *err;         /* diagnostics (NULL → stderr) */
} hem_random_opts;

/*
 * `hem-tool random <N>` (REQ-TOOL-010): read N bytes of device hardware RNG
 * via ehem_random (REQ-OPS-002) and write them to o->out. The SDK never
 * creates keys — the transient-key orchestration (create → harvest →
 * delete) lives here; a delete failure after a created key is
 * HEM_RANDOM_RUNTIME with the leftover kid named on o->err.
 */
int hem_random_run(ehem_ctx *ctx, const hem_random_opts *o);

#endif /* HEM_RANDOM_H */
