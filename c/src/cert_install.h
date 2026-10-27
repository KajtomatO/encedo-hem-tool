/*
 * cert_install.h — hem-tool `cert-install` orchestration.
 *
 * implements: REQ-TOOL-003
 *
 * Automates the manual remediation proven on 2026-07-16 for devices whose
 * firmware (v1.2.2) acknowledges but never installs the check-in certificate
 * (REQ-SYS-003 root cause): harvest the cloud-delivered chain (REQ-SYS-006),
 * skip if the device already serves it, otherwise authenticate, install
 * (REQ-SYS-004), reboot (REQ-SYS-005), wait for the device, and verify.
 *
 * The orchestration is factored out of main() so it can be driven offline
 * through the fake transport (a caller-supplied ehem_ctx) in a unit test — it
 * uses ONLY the public SDK API, exactly like the rest of the tool.
 */
#ifndef HEM_CERT_INSTALL_H
#define HEM_CERT_INSTALL_H

#include <stdbool.h>
#include <stdio.h>

#include "ehem/ehem.h"

/*
 * Exit codes — one per outcome so a script can branch on the failure mode
 * (REQ-TOOL-003). 0 covers both "installed" and "already current".
 */
enum {
    HEM_CERT_OK             = 0,  /* installed, or already current */
    HEM_CERT_NO_PASSPHRASE  = 2,  /* install needed but no passphrase supplied */
    HEM_CERT_NO_CHAIN       = 3,  /* no chain delivered AND no current serial —
                                   * nothing to install and nothing to compare
                                   * (a device up to date returns OK, not this) */
    HEM_CERT_CHECKIN_FAILED = 4,  /* the check-in handshake itself failed */
    HEM_CERT_PARSE_FAILED   = 5,  /* harvested chain would not parse */
    HEM_CERT_AUTH_FAILED    = 6,  /* login / authorization rejected */
    HEM_CERT_INSTALL_FAILED = 7,  /* the device rejected the certificate */
    HEM_CERT_REBOOT_FAILED  = 8,  /* the reboot request failed */
    HEM_CERT_DEVICE_TIMEOUT = 9   /* the device never returned after reboot */
};

/* Post-reboot poll defaults used by the CLI (~2 minutes total). */
#define HEM_CERT_DEFAULT_POLL_ATTEMPTS 24u
#define HEM_CERT_DEFAULT_POLL_DELAY_MS 5000u

typedef struct {
    const char *passphrase;   /* --passphrase / EHEM_PASSPHRASE; NULL if none */
    bool        mobile;       /* --mobile: push-confirm auth (REQ-TOOL-018) */
    int         force;        /* reinstall even if the device is already current */
    int         insecure;     /* ctx is in insecure TLS mode — verify can't prove */
    unsigned    poll_attempts; /* max post-reboot status polls (0 → default) */
    unsigned    poll_delay_ms; /* delay between polls, VERBATIM (0 = no sleep) */
    FILE       *out;          /* progress + summary (NULL → stdout) */
    FILE       *err;          /* diagnostics (NULL → stderr) */
} hem_cert_install_opts;

/*
 * Run the cert-install sequence on `ctx` (already created with the caller's
 * connection + TLS options). Returns one of the HEM_CERT_* exit codes; all
 * progress and errors are written to o->out / o->err.
 */
int hem_cert_install_run(ehem_ctx *ctx, const hem_cert_install_opts *o);

#endif /* HEM_CERT_INSTALL_H */
