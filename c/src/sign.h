/*
 * sign.h — hem-tool `sign` subcommand.
 *
 * implements: REQ-TOOL-008 (`sign <kid>` — message from file/stdin, signature
 *             out; --alg optional with a classifier-driven default)
 *
 * Lives in hem-tool-core (like keys.c / cert_install.c) so the CLI and the
 * unit tests drive one code path, using ONLY the public SDK API.
 */
#ifndef HEM_SIGN_H
#define HEM_SIGN_H

#include <stdbool.h>
#include <stdio.h>

#include "ehem/ehem.h"

/* Exit codes (the tool convention shared with the keys subcommands). */
enum {
    HEM_SIGN_OK      = 0,  /* signature produced */
    HEM_SIGN_RUNTIME = 1,  /* login / device / non-signing-family failure */
    HEM_SIGN_USAGE   = 2   /* argument / environment / input error */
};

/* Output format for the signature. */
typedef enum {
    HEM_SIGN_OUT_B64 = 0,   /* padded base64 + newline (default) */
    HEM_SIGN_OUT_HEX,       /* lowercase hex + newline */
    HEM_SIGN_OUT_RAW        /* raw signature bytes ONLY (pipeline use) */
} hem_sign_format;

typedef struct {
    const char     *passphrase;  /* login passphrase; NULL → HEM_SIGN_USAGE */
    bool        mobile;       /* --mobile: push-confirm auth (REQ-TOOL-018) */
    const char     *kid;         /* 32 hex chars; NULL/malformed → USAGE, no I/O */
    const char     *alg;         /* REQ-OPS-001 selector, verbatim; NULL →
                                  * fetch the key type and pick the family's
                                  * canonical selector (REQ-TOOL-008 table) */
    const char     *in_path;     /* message file; NULL → read o->in / stdin */
    const char     *sigctx;      /* optional RFC 8032 context (UTF-8, ≤ 255) */
    hem_sign_format format;
    FILE           *out;         /* signature (NULL → stdout) */
    FILE           *err;         /* diagnostics (NULL → stderr) */
    FILE           *in;          /* message source when in_path is NULL
                                  * (NULL → stdin; tests inject a tmpfile) */
} hem_sign_opts;

/*
 * `hem-tool sign <kid>` (REQ-TOOL-008): read the message (1..2048 bytes,
 * binary-safe), pick the selector (--alg verbatim, else one extra
 * ehem_key_get + REQ-KEY-006 classification riding the same per-KID token
 * as the sign), sign via ehem_sign, and write the signature to o->out in
 * the chosen format (RAW keeps o->out bytes-only; prose goes to o->err).
 * A non-signing family with no --alg is HEM_SIGN_RUNTIME naming the type.
 */
int hem_sign_run(ehem_ctx *ctx, const hem_sign_opts *o);

#endif /* HEM_SIGN_H */
