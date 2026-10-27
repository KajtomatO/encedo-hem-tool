/*
 * sign.c — hem-tool `sign` subcommand: sign a file/stdin message with a
 * device key.
 *
 * implements: REQ-TOOL-008
 *
 * Public-API-only (include/ehem/), so the same code drives the real device
 * from main() and the fake transport from the unit test.
 */
#include "sign.h"
#include "tool_auth.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "ehem/auth.h"
#include "ehem/crypto.h"
#include "ehem/keymgmt.h"

#include "keys.h"   /* hem_tool_kid_ok, hem_tool_fprint_b64/hex, exit codes */

/* Print the last-error detail recorded on the context (mirrors keys.c). */
static void report(FILE *err, ehem_ctx *ctx, ehem_rc rc, const char *what)
{
    const ehem_error *e = ehem_last_error(ctx);
    fprintf(err, "error: %s: %s\n", what, ehem_rc_str(rc));
    if (e != NULL) {
        if (e->message != NULL && e->message[0] != '\0') {
            fprintf(err, "  detail: %s\n", e->message);
        }
        if (e->http_status != 0) {
            fprintf(err, "  http status: %ld\n", e->http_status);
        }
        if (e->device_payload != NULL) {
            fprintf(err, "  device: %s\n", e->device_payload);
        }
    }
}

/* The canonical selector per signing family (REQ-TOOL-008); NULL for
 * families the exdsa endpoint cannot sign with. */
static const char *default_alg(ehem_key_family family)
{
    switch (family) {
    case EHEM_KEY_FAMILY_SECP256R1:
    case EHEM_KEY_FAMILY_SECP256K1: return EHEM_SIGN_ALG_SHA256_ECDSA;
    case EHEM_KEY_FAMILY_SECP384R1: return EHEM_SIGN_ALG_SHA384_ECDSA;
    case EHEM_KEY_FAMILY_SECP521R1: return EHEM_SIGN_ALG_SHA512_ECDSA;
    case EHEM_KEY_FAMILY_ED25519:   return EHEM_SIGN_ALG_ED25519;
    case EHEM_KEY_FAMILY_ED448:     return EHEM_SIGN_ALG_ED448;
    default:                        return NULL;
    }
}

/*
 * Read the whole message from `f` into buf (cap EHEM_SIGN_MSG_MAX + 1 so an
 * oversized input is detectable). Returns the byte count.
 */
static size_t read_message(FILE *f, uint8_t *buf, size_t cap)
{
    size_t n = 0;
    while (n < cap) {
        size_t got = fread(buf + n, 1, cap - n, f);
        if (got == 0) {
            break;
        }
        n += got;
    }
    return n;
}

int hem_sign_run(ehem_ctx *ctx, const hem_sign_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    static uint8_t msg[EHEM_SIGN_MSG_MAX + 1];
    size_t msg_len;
    const char *alg = o->alg;
    ehem_signature *sig = NULL;
    ehem_rc rc;

    if (!hem_tool_kid_ok(o->kid)) {
        fprintf(err, "error: 'sign' needs a key id (exactly 32 hex chars)\n");
        return HEM_SIGN_USAGE;
    }
    if (o->sigctx != NULL && strlen(o->sigctx) > EHEM_SIGN_SIG_CTX_MAX) {
        fprintf(err, "error: --sigctx must be at most %d bytes\n",
                EHEM_SIGN_SIG_CTX_MAX);
        return HEM_SIGN_USAGE;
    }

    /* Read the message BEFORE any network traffic so input errors stay
     * cheap. Binary-safe: file opened "rb"; stdin switched to binary on
     * Windows so CRLF translation cannot corrupt the bytes being signed. */
    if (o->in_path != NULL) {
        FILE *f = fopen(o->in_path, "rb");
        if (f == NULL) {
            fprintf(err, "error: cannot read '%s'\n", o->in_path);
            return HEM_SIGN_USAGE;
        }
        msg_len = read_message(f, msg, sizeof msg);
        fclose(f);
    } else {
        FILE *f = (o->in != NULL) ? o->in : stdin;
#ifdef _WIN32
        _setmode(_fileno(f), _O_BINARY);
#endif
        msg_len = read_message(f, msg, sizeof msg);
    }
    if (msg_len == 0) {
        fprintf(err, "error: empty message - the device rejects a "
                     "zero-length input\n");
        return HEM_SIGN_USAGE;
    }
    if (msg_len > EHEM_SIGN_MSG_MAX) {
        fprintf(err, "error: message exceeds the device cap of %d bytes\n",
                EHEM_SIGN_MSG_MAX);
        return HEM_SIGN_USAGE;
    }

    rc = hem_tool_login(ctx, o->passphrase, o->mobile, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_SIGN_USAGE : HEM_SIGN_RUNTIME;
    }

    /*
     * --alg omitted: one ehem_key_get + REQ-KEY-006 classification picks the
     * family's canonical selector. The get rides the same keymgmt:use:<kid>
     * token the sign needs (REQ-OPS-001 shared cache) — one extra request,
     * no extra login.
     */
    if (alg == NULL) {
        ehem_key_details *d = NULL;
        ehem_key_type_info info;

        rc = ehem_key_get(ctx, o->kid, &d);
        if (rc == EHEM_ERR_NOT_FOUND) {
            fprintf(err, "error: key not found: %s\n", o->kid);
            return HEM_SIGN_RUNTIME;
        }
        if (rc != EHEM_OK) {
            report(err, ctx, rc, "sign (key-type lookup)");
            return hem_tool_auth_exit(rc, err, HEM_SIGN_RUNTIME);
        }
        (void)ehem_key_type_parse(d->type, &info);
        alg = default_alg(info.family);
        if (alg == NULL) {
            fprintf(err, "error: key type '%s' has no signing algorithm - "
                         "pass --alg explicitly if this is wrong\n", d->type);
            ehem_key_details_free(d);
            return HEM_SIGN_RUNTIME;
        }
        fprintf(err, "note: using %s (from key type %s)\n", alg, d->type);
        ehem_key_details_free(d);   /* alg is a static selector literal */
    }

    rc = ehem_sign(ctx, o->kid, alg, msg, msg_len,
                   (const uint8_t *)o->sigctx,
                   (o->sigctx != NULL) ? strlen(o->sigctx) : 0, &sig);
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "sign");
        return hem_tool_auth_exit(rc, err, HEM_SIGN_RUNTIME);
    }

    switch (o->format) {
    case HEM_SIGN_OUT_RAW:
#ifdef _WIN32
        _setmode(_fileno(out), _O_BINARY);
#endif
        fwrite(sig->sig, 1, sig->sig_len, out);
        break;
    case HEM_SIGN_OUT_HEX:
        hem_tool_fprint_hex(out, sig->sig, sig->sig_len);
        fprintf(out, "\n");
        break;
    case HEM_SIGN_OUT_B64:
    default:
        hem_tool_fprint_b64(out, sig->sig, sig->sig_len);
        fprintf(out, "\n");
        break;
    }

    ehem_signature_free(sig);
    return HEM_SIGN_OK;
}
