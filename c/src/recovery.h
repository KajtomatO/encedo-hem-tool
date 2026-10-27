/*
 * recovery.h — hem-tool `recovery`: diagnose TLS/certificate trouble and run
 * the matching remediation (REQ-TOOL-023).
 *
 * implements: REQ-TOOL-023
 *
 * hem-tool-core: the CLI and the unit tests drive one code path, using ONLY
 * the public SDK API and the other recovery blocks (cert_install.c,
 * recover.c). The caller supplies the contexts, because the diagnosis needs
 * three TLS postures on the same device:
 *   ctx_https    the device URL under the configured trust, automatic
 *                check-in recovery OFF (the probe must surface the verdict);
 *   ctx_insecure the same URL with TLS verification off — the install leg
 *                when the certificate has expired;
 *   ctx_http     the http:// form of the URL — the HTTPS-down path.
 */
#ifndef HEM_TOOL_RECOVERY_H
#define HEM_TOOL_RECOVERY_H

#include <stdbool.h>
#include <stdio.h>

#include "ehem/ehem.h"

/* Exit codes (REQ-TOOL-023). */
enum {
    HEM_RECOVERY_OK          = 0,  /* recovered, or healthy */
    HEM_RECOVERY_RUNTIME     = 1,  /* login / device / cloud failure */
    HEM_RECOVERY_USAGE       = 2,  /* missing passphrase, --mobile, environment */
    HEM_RECOVERY_NO_RENEWAL  = 3,  /* expired cert, the cloud delivered no chain (one attempt) */
    HEM_RECOVERY_UNREACHABLE = 4,  /* neither https nor http answers, or a bounded wait ran out */
    HEM_RECOVERY_TLS_OTHER   = 5   /* a TLS failure that is not an expired certificate */
};

/* Post-reboot poll defaults (~2 minutes total). */
#define HEM_RECOVERY_POLL_ATTEMPTS 60u
#define HEM_RECOVERY_POLL_DELAY_MS 2000u

typedef struct {
    const char *passphrase;    /* login passphrase; NULL → HEM_RECOVERY_USAGE */
    bool        mobile;        /* --mobile → HEM_RECOVERY_USAGE (passphrase-only) */
    ehem_ctx   *ctx_https;     /* required: configured trust, no_auto_checkin */
    ehem_ctx   *ctx_insecure;  /* for the expired-certificate path */
    ehem_ctx   *ctx_http;      /* for the HTTPS-down path */
    const char *register_url;  /* provisioning endpoint; NULL → SDK default */
    unsigned    poll_attempts; /* 0 → HEM_RECOVERY_POLL_ATTEMPTS */
    unsigned    poll_delay_ms; /* VERBATIM (0 = none) */
    FILE       *out;           /* progress (NULL → stdout) */
    FILE       *err;           /* diagnostics (NULL → stderr) */
} hem_recovery_opts;

/*
 * `hem-tool recovery` (REQ-TOOL-023): probe → classify → remediate.
 *   1. healthy        → one check-in (RTC / clock drift), exit 0;
 *   2. expired cert   → ONE check-in (relaxed TLS); no chain → exit 3;
 *                       chain → cert-install (insecure leg) → verify under
 *                       the configured trust → exit 0;
 *   3. HTTPS down     → http answers with https:false → tls-recover
 *                       (provisioning cloud, REBOOTS) → exit 0;
 *   4. unreachable    → neither scheme answers → exit 4;
 *   5. other TLS fail → http answers with https:true → exit 5, nothing
 *                       changed.
 */
int hem_recovery_run(const hem_recovery_opts *o);

#endif /* HEM_TOOL_RECOVERY_H */
