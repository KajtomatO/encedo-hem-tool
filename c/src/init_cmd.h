/*
 * init_cmd.h — hem-tool `init-device` (personalise an uninitialised device).
 *
 * implements: REQ-TOOL-021
 *
 * hem-tool-core: the CLI and the unit tests drive one code path, using ONLY
 * the public SDK API. ATTENDED-ONLY (REQ-TEST-007): no live test; the
 * attended init is the second act of the M10 gate.
 */
#ifndef HEM_TOOL_INIT_CMD_H
#define HEM_TOOL_INIT_CMD_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ehem/ehem.h"

/* Exit codes (REQ-TOOL-021). */
enum {
    HEM_INIT_OK       = 0,  /* initialised */
    HEM_INIT_RUNTIME  = 1,  /* device / cloud / login failure */
    HEM_INIT_USAGE    = 2,  /* missing passphrase / master secret / cfg field */
    HEM_INIT_ALREADY  = 3,  /* device already initialised (406) */
    HEM_INIT_RTC      = 4,  /* RTC still unset after the check-in (403) */
    HEM_INIT_CFG      = 5   /* cfg rejected by the device (400) */
};

/* The Manager's defaults (build.js:4189-4201, dev-device config). */
#define HEM_INIT_DEFAULT_IP           "192.168.7.1/24"
#define HEM_INIT_DEFAULT_STORAGE_MODE 81          /* 0x51, the first layout choice */
#define HEM_INIT_DEFAULT_DISK0_SIZE   8388608LL   /* 4 x 2 MiB slider units */

/* --reboot poll defaults (~3 minutes total). */
#define HEM_INIT_POLL_ATTEMPTS 90u
#define HEM_INIT_POLL_DELAY_MS 2000u

typedef struct {
    const char *passphrase;     /* REQUIRED: becomes the device's user password */
    /* Exactly one master-secret source: */
    const char *master_words;   /* --master-words / EHEM_MASTER_WORDS (24 BIP39 words) */
    const char *master_hex;     /* --master-secret-hex (64 hex chars; scripted escape hatch) */
    int         master_generate;/* --master-generate: the SDK makes the 24 words, printed ONCE */
    /* cfg — required: */
    const char *user;
    const char *email;
    const char *hostname;
    /* cfg — optional (NULL/0 → the Manager's default): */
    const char *ip;             /* "A.B.C.D/prefix" → HEM_INIT_DEFAULT_IP */
    int         storage_mode;   /* → HEM_INIT_DEFAULT_STORAGE_MODE */
    int64_t     disk0_size;     /* bytes → HEM_INIT_DEFAULT_DISK0_SIZE */
    const char *origin;         /* → "*" */
    int         dnsd;
    int         no_trusted_ts;
    int         no_trusted_backend;
    int         no_allow_keysearch;
    int         gen_csr;
    int         ctx_id;
    /* output / follow-up: */
    const char *csr_out;        /* --csr-out FILE: write the PEM CSR here too */
    int         reboot;         /* --reboot: reboot when required and wait */
    /* Asked instead of `reboot` when the device reports reboot_required
     * (init-wizard, REQ-TOOL-028): nonzero = reboot now and wait. NULL (the
     * init-device case) = `reboot` decides. */
    int       (*confirm_reboot)(void *arg);
    void       *confirm_arg;
    unsigned    poll_attempts;  /* 0 → HEM_INIT_POLL_ATTEMPTS */
    unsigned    poll_delay_ms;  /* VERBATIM (0 = none) */
    FILE       *out;            /* report (NULL → stdout) */
    FILE       *err;            /* diagnostics (NULL → stderr) */
} hem_init_opts;

/*
 * `hem-tool init-device ...` (REQ-TOOL-021):
 *   1. validate inputs (zero traffic on a usage error);
 *   2. resolve the master secret (words → ehem_master_secret_from_mnemonic;
 *      hex; or generate + print the words ONCE with a keep-this warning);
 *   3. check-in — the init endpoints demand a set RTC (warn-and-continue:
 *      a 403 from the init itself is reported as HEM_INIT_RTC);
 *   4. ehem_device_init; print instanceid / reboot_required / csr / genuine;
 *   5. with --reboot and reboot_required: reboot and poll until the device
 *      answers again (seen-down-then-back).
 */
int hem_init_device_run(ehem_ctx *ctx, const hem_init_opts *o);

#endif /* HEM_TOOL_INIT_CMD_H */
