/*
 * keytypes.h — the device's creatable key types, as hem-tool documents them:
 * ONE table behind `keys gen -h` (the list, grouped by purpose) and
 * `keys types [TYPE]` (one block per family group, or one entry).
 *
 * implements: REQ-TOOL-027
 *
 * The table holds only what the SDK cannot tell: the type string, the family
 * it must classify to, and the purpose group. Sizes and signability are read
 * from the SDK's REQ-KEY-006 classifier (ehem_key_type_parse) at render time,
 * and the --mode default from keys.h (REQ-TOOL-009) — no copies; the unit
 * test (test_keytypes.c) checks the rows against the classifier.
 */
#ifndef HEM_KEYTYPES_H
#define HEM_KEYTYPES_H

#include <stddef.h>
#include <stdio.h>

#include "ehem/keymgmt.h"

/* What a key is for (REQ-TOOL-027); the listing order. */
typedef enum {
    HEM_KT_NIST = 0,      /* NIST-P/K curves: sign (ECDSA) + key agreement (ECDH) */
    HEM_KT_EDWARDS,       /* Ed25519 / Ed448: sign (EdDSA) */
    HEM_KT_MONTGOMERY,    /* X25519 / X448: key agreement */
    HEM_KT_AES,           /* encrypt / wrap */
    HEM_KT_HMAC,          /* MAC */
    HEM_KT_MLKEM,         /* post-quantum KEM */
    HEM_KT_MLDSA,         /* post-quantum signature */
    HEM_KT_GROUP_COUNT
} hem_keytype_group;

typedef struct {
    const char       *name;     /* the device type string, verbatim */
    ehem_key_family   family;   /* what ehem_key_type_parse(name) yields */
    hem_keytype_group group;
} hem_keytype;

/* The 23 types fw v1.2.2 creates (REQ-TEST-004), in listing order. */
const hem_keytype *hem_keytypes(size_t *count);

/* The row whose name equals `name` exactly (types are case-sensitive on the
 * wire), or NULL. */
const hem_keytype *hem_keytype_find(const char *name);

/* Purpose-group facts: does the group sign / agree (the unit test checks
 * these against the classifier's sizes). */
int hem_keytype_group_signs(hem_keytype_group g);
int hem_keytype_group_agrees(hem_keytype_group g);

/* The `keys gen -h` block: every type, grouped by purpose, with the pointer
 * at `keys types`. */
void hem_keytypes_print_gen_list(FILE *f);

/* Exit code of `keys types` for an unknown TYPE (usage). */
#define HEM_KEYTYPES_UNKNOWN 2

/*
 * `hem-tool keys types [TYPE]`: NULL → the whole table (one block per
 * group); a type name → that entry only; an unknown name → a message naming
 * it and the valid names on `err`, nothing on `out`, HEM_KEYTYPES_UNKNOWN.
 * Local documentation — no SDK initialisation, no traffic. Returns 0 on
 * success.
 */
int hem_keys_types_run(const char *type, FILE *out, FILE *err);

#endif /* HEM_KEYTYPES_H */
