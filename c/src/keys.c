/*
 * keys.c — hem-tool `keys` subcommands + the protected-key classifier.
 *
 * implements: REQ-TOOL-004, REQ-TOOL-005, REQ-TOOL-006, REQ-TOOL-009
 *
 * Public-API-only (include/ehem/), so the same code drives the real device from
 * main() and the fake transport from the unit test.
 */
#include "keys.h"
#include "tool_auth.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "ehem/auth.h"
#include "ehem/keymgmt.h"

/* -------------------------------------------------------------------------- */
/* protected-key classifier (REQ-TOOL-005)                                    */
/* -------------------------------------------------------------------------- */

/* Case-insensitive substring test: does `hay` contain `ndl_lower` (which is
 * already lowercase)? */
static bool ci_contains(const char *hay, const char *ndl_lower)
{
    size_t nlen = strlen(ndl_lower);
    size_t i;

    if (nlen == 0) {
        return true;
    }
    for (i = 0; hay[i] != '\0'; i++) {
        size_t k = 0;
        while (k < nlen && hay[i + k] != '\0' &&
               (char)tolower((unsigned char)hay[i + k]) == ndl_lower[k]) {
            k++;
        }
        if (k == nlen) {
            return true;
        }
    }
    return false;
}

bool hem_key_is_protected(const char *label)
{
    if (label == NULL) {
        return false;
    }
    /* Exact device TLS material. */
    if (strcmp(label, "TLS PrivateKey") == 0 ||
        strcmp(label, "TLS Certificate") == 0) {
        return true;
    }
    /* Paired phone authenticators (any case, any position). */
    return ci_contains(label, "(android)") || ci_contains(label, "(iphone)");
}

/* -------------------------------------------------------------------------- */
/* keys list (REQ-TOOL-004)                                                   */
/* -------------------------------------------------------------------------- */

/* Print one key line: "<indent><kid>  '<label>'  (<type>)<suffix>". */
static void fprint_key(FILE *f, const char *indent, const ehem_key_entry *e,
                       const char *suffix)
{
    fprintf(f, "%s%s  '%s'  (%s)%s\n",
            indent, e->kid, (e->label != NULL) ? e->label : "", e->type,
            (suffix != NULL) ? suffix : "");
}

/* Print the last-error detail recorded on the context. */
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

int hem_keys_list_run(ehem_ctx *ctx, const hem_keys_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    ehem_key_page *page = NULL;
    ehem_rc rc;
    size_t i;
    unsigned long protected_count = 0;


    /* Login is lazy (no traffic); the list call performs the auth exchange. */
    rc = hem_tool_login(ctx, o->passphrase, o->mobile, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_KEYS_USAGE : HEM_KEYS_RUNTIME;
    }

    rc = ehem_key_list_all(ctx, &page);
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "keys list");
        return hem_tool_auth_exit(rc, err, HEM_KEYS_RUNTIME);
    }

    for (i = 0; i < page->listed; i++) {
        const ehem_key_entry *e = &page->entries[i];
        bool prot = hem_key_is_protected(e->label);
        if (prot) {
            protected_count++;
        }
        fprint_key(out, "  ", e, prot ? "  [PROTECTED]" : "");
    }
    fprintf(out, "\n%lu key(s), %lu protected\n",
            (unsigned long)page->listed, protected_count);

    ehem_key_page_free(page);
    return HEM_KEYS_OK;
}

/* -------------------------------------------------------------------------- */
/* keys pub (REQ-TOOL-007)                                                    */
/* -------------------------------------------------------------------------- */

/* keys.h tags the whole file; the pub-specific point is here.
 * implements: REQ-TOOL-007 */

/* True iff `s` is exactly 32 hex chars (a wire-format kid). Tool-side copy —
 * hem-tool-core is public-API-only, so it cannot borrow the SDK's internal
 * validator; the SDK re-validates anyway (defense in depth). */
bool hem_tool_kid_ok(const char *s)
{
    size_t i;
    if (s == NULL) {
        return false;
    }
    for (i = 0; i < 32; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return s[32] == '\0';
}

/* Emit `raw` to `f` as padded std base64 (RFC 4648). Local, dependency-free —
 * the public SDK API hands back decoded bytes and offers no encoder. */
void hem_tool_fprint_b64(FILE *f, const uint8_t *raw, size_t len)
{
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i;
    for (i = 0; i + 2 < len; i += 3) {
        uint32_t v = ((uint32_t)raw[i] << 16) | ((uint32_t)raw[i + 1] << 8) |
                     raw[i + 2];
        fprintf(f, "%c%c%c%c", T[(v >> 18) & 63], T[(v >> 12) & 63],
                T[(v >> 6) & 63], T[v & 63]);
    }
    if (len - i == 1) {
        uint32_t v = (uint32_t)raw[i] << 16;
        fprintf(f, "%c%c==", T[(v >> 18) & 63], T[(v >> 12) & 63]);
    } else if (len - i == 2) {
        uint32_t v = ((uint32_t)raw[i] << 16) | ((uint32_t)raw[i + 1] << 8);
        fprintf(f, "%c%c%c=", T[(v >> 18) & 63], T[(v >> 12) & 63],
                T[(v >> 6) & 63]);
    }
}

void hem_tool_fprint_hex(FILE *f, const uint8_t *raw, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        fprintf(f, "%02x", raw[i]);
    }
}

int hem_keys_pub_run(ehem_ctx *ctx, const hem_keys_pub_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    ehem_key_details *d = NULL;
    ehem_key_type_info info;
    const uint8_t *material;
    size_t material_len;
    const char *material_name;
    ehem_rc rc;

    if (!hem_tool_kid_ok(o->kid)) {
        fprintf(err, "error: 'keys pub' needs a key id "
                     "(exactly 32 hex chars)\n");
        return HEM_KEYS_USAGE;
    }

    rc = hem_tool_login(ctx, o->passphrase, o->mobile, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_KEYS_USAGE : HEM_KEYS_RUNTIME;
    }

    rc = ehem_key_get(ctx, o->kid, &d);
    if (rc == EHEM_ERR_NOT_FOUND) {
        fprintf(err, "error: key not found: %s\n", o->kid);
        return HEM_KEYS_RUNTIME;
    }
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "keys pub");
        return hem_tool_auth_exit(rc, err, HEM_KEYS_RUNTIME);
    }

    if (d->pubkey != NULL) {
        material = d->pubkey;
        material_len = d->pubkey_len;
        material_name = "pubkey";
    } else if (d->der != NULL) {
        material = d->der;
        material_len = d->der_len;
        material_name = "der";
    } else {
        material = NULL;
        material_len = 0;
        material_name = NULL;
    }

    if (o->format == HEM_KEYS_PUB_RAW) {
        /* Pipeline mode: o->out carries the material bytes and NOTHING else. */
        if (material != NULL) {
            fwrite(material, 1, material_len, out);
        } else {
            fprintf(err, "note: %s has no public material (symmetric key)\n",
                    o->kid);
        }
        ehem_key_details_free(d);
        return HEM_KEYS_OK;
    }

    (void)ehem_key_type_parse(d->type, &info);    /* NULLs already excluded */
    fprintf(out, "kid:      %s\n", o->kid);
    fprintf(out, "type:     %s\n", d->type);
    fprintf(out, "family:   %s\n", ehem_key_family_str(info.family));
    if (info.modes != 0) {
        fprintf(out, "modes:    %s%s%s\n",
                (info.modes & EHEM_KEY_MODE_EXDSA) ? "ExDSA" : "",
                (info.modes == (EHEM_KEY_MODE_EXDSA | EHEM_KEY_MODE_ECDH))
                    ? "," : "",
                (info.modes & EHEM_KEY_MODE_ECDH) ? "ECDH" : "");
    }
    if (d->updated != 0) {
        fprintf(out, "updated:  %lld\n", (long long)d->updated);
    }
    if (material != NULL) {
        fprintf(out, "%s:%s", material_name,
                strcmp(material_name, "der") == 0 ? "      " : "   ");
        if (o->format == HEM_KEYS_PUB_HEX) {
            hem_tool_fprint_hex(out, material, material_len);
        } else {
            hem_tool_fprint_b64(out, material, material_len);
        }
        fprintf(out, "\n");
    } else {
        fprintf(out, "material: (none - symmetric key exports no public "
                     "material)\n");
    }

    ehem_key_details_free(d);
    return HEM_KEYS_OK;
}

/* -------------------------------------------------------------------------- */
/* keys gen (REQ-TOOL-009)                                                    */
/* -------------------------------------------------------------------------- */

/* The three exact mode literals the device matches by strcmp (api_keymgmt.c). */
static bool mode_literal_ok(const char *m)
{
    return strcmp(m, "ECDH") == 0 ||
           strcmp(m, "ExDSA") == 0 ||
           strcmp(m, "ECDH,ExDSA") == 0;
}

/* A NIST-P/K family's device default (ECDH-only) cannot sign — so the tool
 * defaults these to HEM_KEYS_GEN_NIST_MODE when --mode is omitted (python
 * OQ-19); every other family sends no mode. */
const char *hem_keys_gen_default_mode(ehem_key_family f)
{
    if (f == EHEM_KEY_FAMILY_SECP256R1 || f == EHEM_KEY_FAMILY_SECP384R1 ||
        f == EHEM_KEY_FAMILY_SECP521R1 || f == EHEM_KEY_FAMILY_SECP256K1) {
        return HEM_KEYS_GEN_NIST_MODE;
    }
    return NULL;
}

int hem_keys_gen_run(ehem_ctx *ctx, const hem_keys_gen_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    ehem_key_create_params p;
    ehem_key_type_info info;
    const char *mode;
    char kid[EHEM_KID_HEX_SIZE] = {0};
    ehem_rc rc;

    if (o->type == NULL || o->type[0] == '\0') {
        fprintf(err, "error: 'keys gen' needs a key type "
                     "(e.g. ED25519, SECP256R1, AES256 - all of them: "
                     "`hem-tool keys types`)\n");
        return HEM_KEYS_USAGE;
    }
    if (o->label == NULL || o->label[0] == '\0') {
        fprintf(err, "error: 'keys gen' needs --label\n");
        return HEM_KEYS_USAGE;
    }
    if (o->mode != NULL && !mode_literal_ok(o->mode)) {
        fprintf(err, "error: --mode must be one of ECDH, ExDSA, ECDH,ExDSA "
                     "(got '%s')\n", o->mode);
        return HEM_KEYS_USAGE;
    }

    /* Pick the effective mode. Explicit --mode wins. Otherwise a NIST-P/K key
     * gets ECDH,ExDSA (so it can sign — the device default ECDH-only cannot),
     * every other family gets no mode (the device ignores it). */
    mode = o->mode;
    if (mode == NULL) {
        (void)ehem_key_type_parse(o->type, &info);
        mode = hem_keys_gen_default_mode(info.family);
        if (mode != NULL) {
            fprintf(err, "note: defaulting --mode to %s for %s "
                         "(the device default is ECDH-only and cannot sign; "
                         "pass --mode to override)\n", mode, o->type);
        }
    }

    rc = hem_tool_login(ctx, o->passphrase, o->mobile, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_KEYS_USAGE : HEM_KEYS_RUNTIME;
    }

    memset(&p, 0, sizeof p);
    p.type = o->type;
    p.label = o->label;
    p.mode = mode;
    if (o->descr != NULL && o->descr[0] != '\0') {
        p.descr = (const uint8_t *)o->descr;
        p.descr_len = strlen(o->descr);
    }

    rc = ehem_key_create(ctx, &p, kid);
    if (rc == EHEM_ERR_ARG) {
        /* Client-side validation (label/descr bound) — a usage error. */
        const ehem_error *e = ehem_last_error(ctx);
        fprintf(err, "error: %s\n",
                (e != NULL && e->message != NULL && e->message[0] != '\0')
                    ? e->message
                    : "invalid key parameters");
        return HEM_KEYS_USAGE;
    }
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "keys gen");
        return hem_tool_auth_exit(rc, err, HEM_KEYS_RUNTIME);
    }

    fprintf(out, "%s\n", kid);
    return HEM_KEYS_OK;
}

/* -------------------------------------------------------------------------- */
/* keys rm (REQ-TOOL-006)                                                     */
/* -------------------------------------------------------------------------- */

static bool startswith(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Read one line from `f` into `buf` (cap), stripping the trailing newline.
 * Returns false on EOF with no line read. */
static bool read_line(FILE *f, char *buf, size_t cap)
{
    size_t n;
    if (fgets(buf, (int)cap, f) == NULL) {
        buf[0] = '\0';
        return false;
    }
    n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }
    return true;
}

/* Trim surrounding whitespace and lowercase in place. */
static void trim_lower(char *s)
{
    char *p = s;
    size_t n;
    size_t i;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
    for (i = 0; s[i] != '\0'; i++) {
        s[i] = (char)tolower((unsigned char)s[i]);
    }
}

/* Bulk prompt for the regular batch: only a trimmed/lowercased "y" proceeds. */
static bool confirm_bulk(FILE *in, FILE *out, size_t count)
{
    char line[64];
    fprintf(out, "\ndelete %lu regular key(s)? [y/N] ", (unsigned long)count);
    fflush(out);
    if (!read_line(in, line, sizeof line)) {
        return false;                    /* EOF → no */
    }
    trim_lower(line);
    return strcmp(line, "y") == 0;
}

/* Per-key protected prompt: ONLY the literal uppercase "YES" proceeds. */
static bool confirm_protected(FILE *in, FILE *out, const ehem_key_entry *e)
{
    char line[64];
    fprintf(out,
            "\nABOUT TO DELETE PROTECTED DEVICE KEY:\n"
            "  kid:   %s\n  label: '%s'\n  type:  %s\n"
            "This may render the device unreachable or break phone pairing.\n"
            "type 'YES' (uppercase) to confirm, anything else aborts: ",
            e->kid, (e->label != NULL) ? e->label : "", e->type);
    fflush(out);
    if (!read_line(in, line, sizeof line)) {
        return false;                    /* EOF → abort */
    }
    return strcmp(line, "YES") == 0;
}

int hem_keys_rm_run(ehem_ctx *ctx, const hem_keys_rm_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    FILE *in  = (o->in  != NULL) ? o->in  : stdin;
    ehem_key_page *page = NULL;
    const ehem_key_entry **regular = NULL;
    const ehem_key_entry **prot_exact = NULL;
    const ehem_key_entry **prot_partial = NULL;
    size_t nreg = 0, nexact = 0, npartial = 0;
    size_t reg_fail = 0, prot_fail = 0, prot_skip = 0;
    size_t n, i;
    ehem_rc rc;
    int exit_code = HEM_KEYS_OK;

    /* Selection: exactly one of --all / --label-prefix. */
    if (o->all && o->prefix_count > 0) {
        fprintf(err, "error: --all and --label-prefix are mutually exclusive\n");
        return HEM_KEYS_USAGE;
    }
    if (!o->all && o->prefix_count == 0) {
        fprintf(err, "error: keys rm needs --all or one or more "
                     "--label-prefix PREFIX\n");
        return HEM_KEYS_USAGE;
    }

    rc = hem_tool_login(ctx, o->passphrase, o->mobile, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_KEYS_USAGE : HEM_KEYS_RUNTIME;
    }
    rc = ehem_key_list_all(ctx, &page);
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "keys list");
        return hem_tool_auth_exit(rc, err, HEM_KEYS_RUNTIME);
    }

    n = page->listed;
    if (n > 0) {
        regular      = malloc(n * sizeof *regular);
        prot_exact   = malloc(n * sizeof *prot_exact);
        prot_partial = malloc(n * sizeof *prot_partial);
        if (regular == NULL || prot_exact == NULL || prot_partial == NULL) {
            free(regular);
            free(prot_exact);
            free(prot_partial);
            ehem_key_page_free(page);
            fprintf(err, "error: out of memory\n");
            return HEM_KEYS_RUNTIME;
        }
    }

    /* Partition (REQ-TOOL-006 §1/§4). A protected key is a target only when a
     * prefix equals its label EXACTLY; a prefix-only hit is warned + skipped.
     * Under --all, protected keys are silently excluded. */
    for (i = 0; i < n; i++) {
        const ehem_key_entry *e = &page->entries[i];
        bool prot = hem_key_is_protected(e->label);
        if (o->all) {
            if (!prot) {
                regular[nreg++] = e;
            }
        } else {
            bool hit = false, exact = false;
            size_t k;
            if (e->label != NULL) {
                for (k = 0; k < o->prefix_count; k++) {
                    if (startswith(e->label, o->prefixes[k])) {
                        hit = true;
                        if (strcmp(e->label, o->prefixes[k]) == 0) {
                            exact = true;
                        }
                    }
                }
            }
            if (!hit) {
                continue;
            }
            if (!prot) {
                regular[nreg++] = e;
            } else if (exact) {
                prot_exact[nexact++] = e;
            } else {
                prot_partial[npartial++] = e;
            }
        }
    }

    /* Partition report (REQ-TOOL-006 §2). */
    fprintf(out, "%lu key(s) total - ", (unsigned long)n);
    if (o->all) {
        fprintf(out, "ALL keys (excluding protected device keys)\n");
    } else {
        fprintf(out, "keys matching label prefix(es):");
        for (i = 0; i < o->prefix_count; i++) {
            fprintf(out, " '%s'", o->prefixes[i]);
        }
        fprintf(out, "\n");
    }
    fprintf(out,
            "  regular targets:   %lu\n"
            "  protected targets: %lu  (require per-key confirmation)\n"
            "  protected skipped: %lu  (partial-match only)\n",
            (unsigned long)nreg, (unsigned long)nexact, (unsigned long)npartial);

    if (npartial > 0) {
        fprintf(out,
                "\nWARNING: these PROTECTED keys partially match a prefix and are\n"
                "SKIPPED. To remove one, use --label-prefix with its EXACT label:\n");
        for (i = 0; i < npartial; i++) {
            fprint_key(out, "  ! ", prot_partial[i], "");
        }
    }
    if (nreg > 0) {
        fprintf(out, "\nregular keys to delete:\n");
        for (i = 0; i < nreg; i++) {
            fprint_key(out, "  ", regular[i], "");
        }
    }
    if (nexact > 0) {
        fprintf(out, "\nPROTECTED device keys queued for deletion "
                     "(per-key confirmation):\n");
        for (i = 0; i < nexact; i++) {
            fprint_key(out, "  * ", prot_exact[i], "");
        }
    }

    if (nreg == 0 && nexact == 0) {
        fprintf(out, "\nnothing to do\n");
        goto cleanup;
    }
    if (o->dry_run) {
        fprintf(out, "\ndry-run: no keys deleted\n");
        goto cleanup;
    }

    /* Regular deletions behind ONE bulk prompt (--yes skips it). */
    if (nreg > 0) {
        if (!o->assume_yes && !confirm_bulk(in, out, nreg)) {
            fprintf(out, "aborted regular deletion\n");
            if (nexact == 0) {
                exit_code = HEM_KEYS_RUNTIME;   /* user abort, nothing else to do */
                goto cleanup;
            }
            nreg = 0;                           /* fall through to protected keys */
        }
        for (i = 0; i < nreg; i++) {
            const ehem_key_entry *e = regular[i];
            rc = ehem_key_delete(ctx, e->kid);
            if (rc == EHEM_OK) {
                fprintf(out, "deleted %s  '%s'\n",
                        e->kid, (e->label != NULL) ? e->label : "");
            } else {
                reg_fail++;
                fprintf(err, "FAILED  %s  '%s': %s\n",
                        e->kid, (e->label != NULL) ? e->label : "",
                        ehem_rc_str(rc));
            }
        }
    }

    /* Protected deletions: ALWAYS interactive; --yes never applies. */
    for (i = 0; i < nexact; i++) {
        const ehem_key_entry *e = prot_exact[i];
        if (!confirm_protected(in, out, e)) {
            prot_skip++;
            fprintf(out, "skipped %s  '%s'\n",
                    e->kid, (e->label != NULL) ? e->label : "");
            continue;
        }
        rc = ehem_key_delete(ctx, e->kid);
        if (rc == EHEM_OK) {
            fprintf(out, "deleted PROTECTED %s  '%s'\n",
                    e->kid, (e->label != NULL) ? e->label : "");
        } else {
            prot_fail++;
            fprintf(err, "FAILED  PROTECTED %s  '%s': %s\n",
                    e->kid, (e->label != NULL) ? e->label : "", ehem_rc_str(rc));
        }
    }

    fprintf(out, "\ndone: %lu deleted, %lu failed, %lu protected skipped\n",
            (unsigned long)(nreg - reg_fail + nexact - prot_fail - prot_skip),
            (unsigned long)(reg_fail + prot_fail),
            (unsigned long)prot_skip);
    if (reg_fail > 0 || prot_fail > 0) {
        exit_code = HEM_KEYS_RUNTIME;
    }

cleanup:
    free(regular);
    free(prot_exact);
    free(prot_partial);
    ehem_key_page_free(page);
    return exit_code;
}

/* -------------------------------------------------------------------------- */
/* keys update (REQ-TOOL-011)                                                 */
/* -------------------------------------------------------------------------- */

/* Per-key protected prompt for UPDATE: only the literal "YES" proceeds. */
static bool confirm_protected_update(FILE *in, FILE *out,
                                     const ehem_key_entry *e,
                                     const char *new_label)
{
    char line[64];
    fprintf(out,
            "\nABOUT TO RENAME PROTECTED DEVICE KEY:\n"
            "  kid:       %s\n  label:     '%s'\n  new label: '%s'\n"
            "Renaming can remove the protection this key's label provides.\n"
            "type 'YES' (uppercase) to confirm, anything else skips: ",
            e->kid, (e->label != NULL) ? e->label : "", new_label);
    fflush(out);
    if (!read_line(in, line, sizeof line)) {
        return false;                    /* EOF → skip */
    }
    return strcmp(line, "YES") == 0;
}

int hem_keys_update_run(ehem_ctx *ctx, const hem_keys_update_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    FILE *in  = (o->in  != NULL) ? o->in  : stdin;
    ehem_key_page *page = NULL;
    const ehem_key_entry *e = NULL;
    const uint8_t *descr = NULL;
    size_t descr_len = 0;
    size_t i;
    ehem_rc rc;

    if (o->kid == NULL || !hem_tool_kid_ok(o->kid)) {
        fprintf(err, "error: keys update needs a 32-hex-char KID\n");
        return HEM_KEYS_USAGE;
    }
    if (o->label == NULL || o->label[0] == '\0') {
        fprintf(err, "error: keys update needs --label (the firmware rejects "
                     "a label-less update)\n");
        return HEM_KEYS_USAGE;
    }

    rc = hem_tool_login(ctx, o->passphrase, o->mobile, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_KEYS_USAGE : HEM_KEYS_RUNTIME;
    }

    /* The CURRENT label decides the protected classification, and carries the
     * stored descr the tool preserves when --descr is omitted. */
    rc = ehem_key_list_all(ctx, &page);
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "keys list");
        return hem_tool_auth_exit(rc, err, HEM_KEYS_RUNTIME);
    }
    for (i = 0; i < page->listed; i++) {
        if (strcmp(page->entries[i].kid, o->kid) == 0) {
            e = &page->entries[i];
            break;
        }
    }
    if (e == NULL) {
        fprintf(err, "error: no key with kid %s on the device\n", o->kid);
        ehem_key_page_free(page);
        return HEM_KEYS_RUNTIME;
    }

    if (hem_key_is_protected(e->label)) {
        /* --yes deliberately ignored: same ritual as keys rm. */
        if (!confirm_protected_update(in, out, e, o->label)) {
            fprintf(out, "skipped protected key %s ('%s')\n", e->kid,
                    (e->label != NULL) ? e->label : "");
            ehem_key_page_free(page);
            return HEM_KEYS_OK;
        }
    } else if (hem_key_is_protected(o->label)) {
        fprintf(err, "warning: the new label classifies this key as "
                     "PROTECTED for later bulk operations (REQ-TOOL-005)\n");
    }

    if (o->descr != NULL) {
        /* --descr "" explicitly clears (sends no descr → firmware wipes). */
        if (o->descr[0] != '\0') {
            descr = (const uint8_t *)o->descr;
            descr_len = strlen(o->descr);
        }
    } else if (e->descr != NULL && e->descr_len > 0) {
        /* Preserve: the firmware rewrites the whole record, so an omitted
         * descr would CLEAR the stored one (REQ-KEY-007) — re-send it. */
        descr = e->descr;
        descr_len = e->descr_len;
        fprintf(err, "note: preserving the stored descr (%lu bytes); pass "
                     "--descr \"\" to clear it\n", (unsigned long)descr_len);
    }

    rc = ehem_key_update(ctx, o->kid, o->label, descr, descr_len);
    ehem_key_page_free(page);
    e = NULL;
    if (rc == EHEM_ERR_ARG) {
        report(err, ctx, rc, "keys update");
        return HEM_KEYS_USAGE;
    }
    if (rc != EHEM_OK) {
        report(err, ctx, rc, "keys update");
        return hem_tool_auth_exit(rc, err, HEM_KEYS_RUNTIME);
    }
    fprintf(out, "updated %s: label '%s'\n", o->kid, o->label);
    return HEM_KEYS_OK;
}
