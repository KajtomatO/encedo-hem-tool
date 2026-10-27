/*
 * keytypes.c — the creatable key types and their documentation (see
 * keytypes.h).
 *
 * implements: REQ-TOOL-027
 */
#include "keytypes.h"

#include <string.h>

#include "keys.h"        /* hem_keys_gen_default_mode (REQ-TOOL-009) */

/* The 23 types fw v1.2.2 creates (api_keymgmt.c:866-963, REQ-TEST-004). */
static const hem_keytype TYPES[] = {
    { "SECP256R1",  EHEM_KEY_FAMILY_SECP256R1,     HEM_KT_NIST },
    { "SECP384R1",  EHEM_KEY_FAMILY_SECP384R1,     HEM_KT_NIST },
    { "SECP521R1",  EHEM_KEY_FAMILY_SECP521R1,     HEM_KT_NIST },
    { "SECP256K1",  EHEM_KEY_FAMILY_SECP256K1,     HEM_KT_NIST },
    { "ED25519",    EHEM_KEY_FAMILY_ED25519,       HEM_KT_EDWARDS },
    { "ED448",      EHEM_KEY_FAMILY_ED448,         HEM_KT_EDWARDS },
    { "CURVE25519", EHEM_KEY_FAMILY_CURVE25519,    HEM_KT_MONTGOMERY },
    { "CURVE448",   EHEM_KEY_FAMILY_CURVE448,      HEM_KT_MONTGOMERY },
    { "AES128",     EHEM_KEY_FAMILY_AES128,        HEM_KT_AES },
    { "AES192",     EHEM_KEY_FAMILY_AES192,        HEM_KT_AES },
    { "AES256",     EHEM_KEY_FAMILY_AES256,        HEM_KT_AES },
    { "SHA2-256",   EHEM_KEY_FAMILY_HMAC_SHA2_256, HEM_KT_HMAC },
    { "SHA2-384",   EHEM_KEY_FAMILY_HMAC_SHA2_384, HEM_KT_HMAC },
    { "SHA2-512",   EHEM_KEY_FAMILY_HMAC_SHA2_512, HEM_KT_HMAC },
    { "SHA3-256",   EHEM_KEY_FAMILY_HMAC_SHA3_256, HEM_KT_HMAC },
    { "SHA3-384",   EHEM_KEY_FAMILY_HMAC_SHA3_384, HEM_KT_HMAC },
    { "SHA3-512",   EHEM_KEY_FAMILY_HMAC_SHA3_512, HEM_KT_HMAC },
    { "MLKEM512",   EHEM_KEY_FAMILY_MLKEM512,      HEM_KT_MLKEM },
    { "MLKEM768",   EHEM_KEY_FAMILY_MLKEM768,      HEM_KT_MLKEM },
    { "MLKEM1024",  EHEM_KEY_FAMILY_MLKEM1024,     HEM_KT_MLKEM },
    { "MLDSA44",    EHEM_KEY_FAMILY_MLDSA44,       HEM_KT_MLDSA },
    { "MLDSA65",    EHEM_KEY_FAMILY_MLDSA65,       HEM_KT_MLDSA },
    { "MLDSA87",    EHEM_KEY_FAMILY_MLDSA87,       HEM_KT_MLDSA },
};
#define TYPE_COUNT (sizeof TYPES / sizeof TYPES[0])

/* Per purpose group: the words, what it can do, which commands use it. */
typedef struct {
    const char *title;     /* block header in `keys types` */
    const char *purpose;   /* what the keys are for */
    const char *short_tag; /* the `keys gen -h` group label */
    int         signs;
    int         agrees;
    const char *use;       /* the matching hem-tool commands / SDK calls */
} group_info;

static const group_info GROUPS[HEM_KT_GROUP_COUNT] = {
    { "NIST-P/K curves", "sign (ECDSA) and key agreement (ECDH)",
      "sign + agreement", 1, 1,
      "hem-tool sign, keys pub; key agreement: SDK ehem_ecdh" },
    { "Edwards curves", "sign (EdDSA)",
      "sign (EdDSA)", 1, 0,
      "hem-tool sign, keys pub" },
    { "Montgomery curves", "key agreement (X25519 / X448)",
      "key agreement", 0, 1,
      "hem-tool keys pub; key agreement: SDK ehem_ecdh" },
    { "AES", "encrypt / decrypt and key wrapping",
      "encrypt / wrap", 0, 0,
      "hem-tool random --kid; SDK ehem_encrypt / _decrypt, ehem_wrap / _unwrap" },
    { "HMAC", "message authentication (MAC)",
      "MAC (HMAC)", 0, 0,
      "SDK ehem_hmac / ehem_hmac_verify" },
    { "ML-KEM", "post-quantum key encapsulation (KEM)",
      "KEM (ML-KEM)", 0, 0,
      "hem-tool keys pub; SDK ehem_mlkem_encaps / ehem_mlkem_decaps" },
    { "ML-DSA", "post-quantum signature",
      "PQC signature", 1, 0,
      "hem-tool keys pub; SDK ehem_mldsa_sign / ehem_mldsa_verify" },
};

const hem_keytype *hem_keytypes(size_t *count)
{
    if (count != NULL) {
        *count = TYPE_COUNT;
    }
    return TYPES;
}

const hem_keytype *hem_keytype_find(const char *name)
{
    size_t i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < TYPE_COUNT; i++) {
        if (strcmp(TYPES[i].name, name) == 0) {
            return &TYPES[i];
        }
    }
    return NULL;
}

int hem_keytype_group_signs(hem_keytype_group g)
{
    return ((unsigned)g < (unsigned)HEM_KT_GROUP_COUNT) ? GROUPS[g].signs : 0;
}

int hem_keytype_group_agrees(hem_keytype_group g)
{
    return ((unsigned)g < (unsigned)HEM_KT_GROUP_COUNT) ? GROUPS[g].agrees : 0;
}

/* Every type, one line per group (`keys gen -h`, the unknown-TYPE message). */
static void print_grouped(FILE *f)
{
    int g;
    size_t i;
    for (g = 0; g < HEM_KT_GROUP_COUNT; g++) {
        fprintf(f, "  %-17s", GROUPS[g].short_tag);
        for (i = 0; i < TYPE_COUNT; i++) {
            if ((int)TYPES[i].group == g) {
                fprintf(f, " %s", TYPES[i].name);
            }
        }
        fputc('\n', f);
    }
}

void hem_keytypes_print_gen_list(FILE *f)
{
    fprintf(f, "\nkey types (what each is for: `hem-tool keys types [TYPE]`):\n");
    print_grouped(f);
}

/* The facts line of one type — sizes from the SDK's classifier (REQ-KEY-006),
 * never copied here. `with_name` prefixes the type name (the table view). */
static void print_facts(FILE *f, const hem_keytype *t, int with_name)
{
    ehem_key_type_info info;

    (void)ehem_key_type_parse(t->name, &info);
    if (with_name) {
        fprintf(f, "  %-11s ", t->name);
    } else {
        fputs("  ", f);
    }
    if (info.pubkey_len == 0) {
        fprintf(f, "secret key - no public material");
    } else {
        fprintf(f, "public key %lu B", (unsigned long)info.pubkey_len);
    }
    if (info.sig_max_len > 0) {
        fprintf(f, ", signature %s%lu B%s", info.sig_der ? "up to " : "",
                (unsigned long)info.sig_max_len, info.sig_der ? " (DER)" : "");
    }
    fputc('\n', f);
}

/* The group's trailing notes: the keys gen --mode default where there is one
 * (REQ-TOOL-009's constant), then the matching commands. */
static void print_group_notes(FILE *f, hem_keytype_group g,
                              const hem_keytype *sample)
{
    const char *mode = hem_keys_gen_default_mode(sample->family);
    if (mode != NULL) {
        fprintf(f, "  keys gen default: --mode %s (the device's own default, "
                   "ECDH, cannot sign)\n", mode);
    }
    fprintf(f, "  use: %s\n", GROUPS[g].use);
}

static void print_all(FILE *out)
{
    int g;
    size_t i;
    for (g = 0; g < HEM_KT_GROUP_COUNT; g++) {
        const hem_keytype *sample = NULL;
        if (g > 0) {
            fputc('\n', out);
        }
        fprintf(out, "%s - %s\n", GROUPS[g].title, GROUPS[g].purpose);
        for (i = 0; i < TYPE_COUNT; i++) {
            if ((int)TYPES[i].group == g) {
                print_facts(out, &TYPES[i], 1);
                if (sample == NULL) {
                    sample = &TYPES[i];
                }
            }
        }
        if (sample != NULL) {
            print_group_notes(out, (hem_keytype_group)g, sample);
        }
    }
}

int hem_keys_types_run(const char *type, FILE *out, FILE *err)
{
    const hem_keytype *t;

    if (out == NULL) {
        out = stdout;
    }
    if (err == NULL) {
        err = stderr;
    }
    if (type == NULL) {
        print_all(out);
        return 0;
    }
    t = hem_keytype_find(type);
    if (t == NULL) {
        fprintf(err, "error: unknown key type '%s' - the device creates "
                     "(case-sensitive):\n", type);
        print_grouped(err);
        return HEM_KEYTYPES_UNKNOWN;
    }
    fprintf(out, "%s - %s: %s\n", t->name, GROUPS[t->group].title,
            GROUPS[t->group].purpose);
    print_facts(out, t, 0);
    print_group_notes(out, t->group, t);
    return 0;
}
