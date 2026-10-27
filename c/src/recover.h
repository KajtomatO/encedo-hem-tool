/*
 * recover.h — hem-tool `tls-recover` (one-command HTTPS restoration).
 *
 * implements: REQ-TOOL-015
 *
 * hem-tool-core: the CLI, the unit tests, and the live demo drive one code
 * path, using ONLY the public SDK API. Run it against the device's http://
 * URL — the state a TLS-wiped device is in.
 */
#ifndef HEM_TOOL_RECOVER_H
#define HEM_TOOL_RECOVER_H

#include <stdbool.h>
#include <stdio.h>

#include "ehem/ehem.h"

/* Exit codes (the cert-install multi-code convention). */
enum {
    HEM_RECOVER_OK        = 0,  /* recovered, or nothing to do */
    HEM_RECOVER_RUNTIME   = 1,  /* login / device / cloud failure */
    HEM_RECOVER_USAGE     = 2,  /* missing passphrase / environment */
    HEM_RECOVER_NO_BUNDLE = 3,  /* the cloud delivered no usable bundle */
    HEM_RECOVER_TIMEOUT   = 4   /* device did not return serving HTTPS */
};

/* Post-reboot poll defaults (~2 minutes total). */
#define HEM_RECOVER_POLL_ATTEMPTS 60u
#define HEM_RECOVER_POLL_DELAY_MS 2000u

typedef struct {
    const char *passphrase;    /* login passphrase; NULL → HEM_RECOVER_USAGE */
    bool        mobile;       /* --mobile: push-confirm auth (REQ-TOOL-018) */
    const char *register_url;  /* provisioning endpoint; NULL → SDK default */
    int         force;         /* recover even when status reports https up */
    unsigned    poll_attempts; /* post-reboot polls (0 → default) */
    unsigned    poll_delay_ms; /* delay between polls, VERBATIM (0 = none) */
    FILE       *out;           /* progress (NULL → stdout) */
    FILE       *err;           /* diagnostics (NULL → stderr) */
} hem_recover_opts;

/*
 * `hem-tool tls-recover [--force]` (REQ-TOOL-015):
 *   1. status — device already serving HTTPS → "nothing to recover"
 *      exit 0 (unless force);
 *   2. check-in (RTC sync; warn-and-continue on failure);
 *   3. login + ehem_tls_recover (REQ-SYS-013);
 *   4. reboot when required, then poll status until it reports
 *      `https: true` (keyed on the flag flipping, so the firmware's ~2 s
 *      keep-serving window cannot fake success);
 *   5. report. DISRUPTIVE: installs key material and reboots.
 */
int hem_tls_recover_run(ehem_ctx *ctx, const hem_recover_opts *o);

#endif /* HEM_TOOL_RECOVER_H */
