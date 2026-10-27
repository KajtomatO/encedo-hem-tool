/*
 * selftest.h — hem-tool `selftest` subcommand.
 *
 * implements: REQ-TOOL-013 (run the device battery; the exit code carries
 * the device's own health verdict so scripts can gate on it)
 */
#ifndef HEM_TOOL_SELFTEST_H
#define HEM_TOOL_SELFTEST_H

#include <stdbool.h>
#include <stdio.h>

#include "ehem/ehem.h"

enum {
    HEM_SELFTEST_OK        = 0,  /* reachable AND fls_state == 0 */
    HEM_SELFTEST_RUNTIME   = 1,  /* login / device error (message printed) */
    HEM_SELFTEST_USAGE     = 2,  /* missing passphrase */
    HEM_SELFTEST_FAILSTATE = 3   /* the battery ran but fls_state != 0 —
                                    distinct from unreachable, as cert-install's
                                    multi-code convention */
};

typedef struct {
    const char *passphrase;   /* login passphrase; NULL → HEM_SELFTEST_USAGE */
    bool        mobile;       /* --mobile: push-confirm auth (REQ-TOOL-018) */
    FILE       *out;          /* report (NULL → stdout) */
    FILE       *err;          /* diagnostics (NULL → stderr) */
} hem_selftest_opts;

/*
 * `hem-tool selftest` (REQ-TOOL-013): run ehem_system_selftest and print the
 * verdict (PASS/FAIL + fls_state), the battery timestamps, kat_busy /
 * se_state when present, and the repo_stats block — the only place key-slot
 * exhaustion is visible. Warns on o->err that the call re-runs the battery
 * on-device (do not poll in a loop). Exit code carries the verdict (enum
 * above).
 */
int hem_selftest_run(ehem_ctx *ctx, const hem_selftest_opts *o);

#endif /* HEM_TOOL_SELFTEST_H */
