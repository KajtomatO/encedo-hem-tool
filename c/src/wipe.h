/*
 * wipe.h — hem-tool `wipe-device` (factory reset with an unbypassable
 * confirmation).
 *
 * implements: REQ-TOOL-022
 *
 * hem-tool-core: the CLI and the unit tests drive one code path, using ONLY
 * the public SDK API. ATTENDED-ONLY (REQ-TEST-007): there is no live test;
 * the attended run is the M10 gate.
 */
#ifndef HEM_TOOL_WIPE_H
#define HEM_TOOL_WIPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "ehem/ehem.h"

/* Exit codes (the reboot / tls-recover convention). */
enum {
    HEM_WIPE_OK       = 0,  /* wipe accepted (and, with --wait, the device came back) */
    HEM_WIPE_RUNTIME  = 1,  /* login / device failure */
    HEM_WIPE_USAGE    = 2,  /* missing passphrase, --mobile, environment */
    HEM_WIPE_DECLINED = 3,  /* the confirmation was not the hostname */
    HEM_WIPE_TIMEOUT  = 4   /* --wait: the device did not answer again */
};

/* --wait poll defaults (~3 minutes total). */
#define HEM_WIPE_POLL_ATTEMPTS 90u
#define HEM_WIPE_POLL_DELAY_MS 2000u

typedef struct {
    const char *passphrase;    /* login passphrase; NULL → HEM_WIPE_USAGE */
    bool        mobile;        /* --mobile → HEM_WIPE_USAGE (passphrase-only) */
    int         wait_back;     /* --wait: poll until the device answers again */
    unsigned    poll_attempts; /* 0 → HEM_WIPE_POLL_ATTEMPTS */
    unsigned    poll_delay_ms; /* delay between polls, VERBATIM (0 = none) */
    ehem_ctx   *probe;         /* --wait: a context on the device's http:// URL
                                * (HTTPS dies with the TLS material); NULL →
                                * poll through `ctx` */
    FILE       *in;            /* confirmation input (NULL → stdin) */
    FILE       *out;           /* report + prompt (NULL → stdout) */
    FILE       *err;           /* diagnostics (NULL → stderr) */
} hem_wipe_opts;

/*
 * `hem-tool wipe-device [--wait]` (REQ-TOOL-022):
 *   1. login (lazy) and read the device identity (ehem_system_config:
 *      hostname, devid, instanceid, user) — printed for the operator;
 *   2. the confirmation: the operator must type the device's HOSTNAME
 *      exactly; anything else aborts with HEM_WIPE_DECLINED and zero
 *      writes. There is deliberately no --yes;
 *   3. ehem_system_wipeout (REQ-SYS-014) — the device answers, then erases
 *      its configuration and restarts ~2 s later;
 *   4. with --wait: poll status (over `probe`, the http:// context) until
 *      the device has been seen DOWN and then answers again; report that it
 *      is uninitialised and point at init-device / recovery.
 */
int hem_wipe_device_run(ehem_ctx *ctx, const hem_wipe_opts *o);

/*
 * The http:// form of a device URL for the post-wipe probe: "https://" is
 * replaced by "http://", anything else is copied as is. Returns false when
 * `cap` is too small.
 */
bool hem_wipe_http_url(const char *url, char *out, size_t cap);

#endif /* HEM_TOOL_WIPE_H */
