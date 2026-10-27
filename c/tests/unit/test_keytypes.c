/*
 * test_keytypes.c — hem-tool's key-type documentation (hem-tool-core
 * keytypes.c): one table behind `keys gen -h` and `keys types [TYPE]`.
 *
 * verifies: REQ-TOOL-027 (exactly 23 rows; each classifies through the SDK's
 *           ehem_key_type_parse to its declared, distinct family; the purpose
 *           groups agree with the classifier's signability and public-key
 *           facts; the --mode default is keys.h's constant for exactly the
 *           NIST-P/K rows; every row appears in the rendered `keys gen` help
 *           and in `keys types`; `keys types ED25519` is one entry; an
 *           unknown TYPE exits 2 naming it and the valid names; `keys types`
 *           is registered with auth class none)
 *
 * The offline / forced-mismatch smoke of the real command line is in
 * check_tool_startup.cmake (tool_startup_cli).
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/keymgmt.h"
#include "keys.h"
#include "keytypes.h"
#include "registry.h"

static char *slurp(FILE *f)
{
    long n;
    char *buf;
    assert_int_equal(fseek(f, 0, SEEK_END), 0);
    n = ftell(f);
    assert_true(n >= 0);
    rewind(f);
    buf = malloc((size_t)n + 1);
    assert_non_null(buf);
    assert_int_equal(fread(buf, 1, (size_t)n, f), (size_t)n);
    buf[n] = '\0';
    return buf;
}

/* `name` appears in `text` as a whole token (not as a prefix of another type,
 * e.g. ED448 inside "ED4480"). */
static int has_token(const char *text, const char *name)
{
    size_t len = strlen(name);
    const char *p = text;
    while ((p = strstr(p, name)) != NULL) {
        char before = (p == text) ? ' ' : p[-1];
        char after = p[len];
        if ((before == ' ' || before == '\n') &&
            (after == ' ' || after == '\n' || after == '\0')) {
            return 1;
        }
        p += len;
    }
    return 0;
}

static char *run_types(const char *type, int expect_rc, char **err_out)
{
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    char *s;
    assert_non_null(out);
    assert_non_null(err);
    assert_int_equal(hem_keys_types_run(type, out, err), expect_rc);
    s = slurp(out);
    *err_out = slurp(err);
    fclose(out);
    fclose(err);
    return s;
}

/* The table: 23 rows, each classified by the SDK to its declared family,
 * all distinct and known. */
static void test_rows_classify(void **state)
{
    (void)state;
    size_t n = 0, i, j;
    const hem_keytype *t = hem_keytypes(&n);

    assert_non_null(t);
    assert_int_equal(n, 23);
    for (i = 0; i < n; i++) {
        ehem_key_type_info info;
        assert_int_equal(ehem_key_type_parse(t[i].name, &info), EHEM_OK);
        assert_int_not_equal(info.family, EHEM_KEY_FAMILY_UNKNOWN);
        assert_int_equal(info.family, t[i].family);
        assert_string_equal(ehem_key_family_str(info.family), t[i].name);
        assert_ptr_equal(hem_keytype_find(t[i].name), &t[i]);
        for (j = 0; j < i; j++) {
            assert_int_not_equal(t[i].family, t[j].family);
        }
    }
    assert_null(hem_keytype_find("ed25519"));    /* case-sensitive, as on the wire */
    assert_null(hem_keytype_find("CERT"));       /* a container, not creatable */
    assert_null(hem_keytype_find(NULL));
}

/* The purpose groups agree with the SDK's REQ-KEY-006 facts: a signing group's
 * types have a signature size, the others none; symmetric groups (AES, HMAC)
 * export no public material, every other group does. */
static void test_groups_match_classifier(void **state)
{
    (void)state;
    size_t n = 0, i;
    const hem_keytype *t = hem_keytypes(&n);

    for (i = 0; i < n; i++) {
        ehem_key_type_info info;
        int symmetric = (t[i].group == HEM_KT_AES || t[i].group == HEM_KT_HMAC);
        assert_int_equal(ehem_key_type_parse(t[i].name, &info), EHEM_OK);
        assert_int_equal(hem_keytype_group_signs(t[i].group) != 0,
                         info.sig_max_len > 0);
        assert_int_equal(symmetric, info.pubkey_len == 0);
        if (hem_keytype_group_agrees(t[i].group)) {
            assert_true(info.pubkey_len > 0);        /* agreement needs a public half */
        }
    }
    assert_true(hem_keytype_group_agrees(HEM_KT_NIST));
    assert_true(hem_keytype_group_agrees(HEM_KT_MONTGOMERY));
    assert_false(hem_keytype_group_agrees(HEM_KT_EDWARDS));
    assert_false(hem_keytype_group_signs(HEM_KT_GROUP_COUNT));   /* out of range */
}

/* REQ-TOOL-009's --mode default: keys.h's constant for exactly the NIST-P/K
 * rows — the same function keys gen calls. */
static void test_nist_mode_default(void **state)
{
    (void)state;
    size_t n = 0, i;
    const hem_keytype *t = hem_keytypes(&n);

    assert_string_equal(HEM_KEYS_GEN_NIST_MODE, "ECDH,ExDSA");
    for (i = 0; i < n; i++) {
        const char *mode = hem_keys_gen_default_mode(t[i].family);
        if (t[i].group == HEM_KT_NIST) {
            assert_non_null(mode);
            assert_string_equal(mode, HEM_KEYS_GEN_NIST_MODE);
        } else {
            assert_null(mode);
        }
    }
}

/* `keys gen -h` lists every row and points at `keys types`. */
static void test_gen_help_lists_all(void **state)
{
    (void)state;
    size_t n = 0, i;
    const hem_keytype *t = hem_keytypes(&n);
    FILE *f = tmpfile();
    char *s;

    assert_non_null(f);
    assert_int_equal(hem_help_command(f, "keys", "gen"), 0);
    s = slurp(f);
    fclose(f);
    for (i = 0; i < n; i++) {
        assert_true(has_token(s, t[i].name));
    }
    assert_non_null(strstr(s, "keys types"));
    assert_non_null(strstr(s, HEM_KEYS_GEN_NIST_MODE));
    assert_non_null(strstr(s, "--label"));       /* its own options still there */
    free(s);
}

/* `keys types`: every row, with sizes; ED25519 alone is one entry; NOPE is
 * refused with the valid names, nothing on stdout. */
static void test_keys_types_output(void **state)
{
    (void)state;
    size_t n = 0, i;
    const hem_keytype *t = hem_keytypes(&n);
    char *err;
    char *s = run_types(NULL, 0, &err);

    for (i = 0; i < n; i++) {
        assert_true(has_token(s, t[i].name));
    }
    assert_non_null(strstr(s, "public key 32 B, signature 64 B"));   /* ED25519 */
    assert_non_null(strstr(s, "signature up to 141 B (DER)"));       /* SECP521R1 */
    assert_non_null(strstr(s, "--mode " HEM_KEYS_GEN_NIST_MODE));
    assert_string_equal(err, "");
    free(s);
    free(err);

    s = run_types("ED25519", 0, &err);
    assert_true(has_token(s, "ED25519"));
    assert_non_null(strstr(s, "public key 32 B, signature 64 B"));
    assert_false(has_token(s, "ED448"));
    assert_false(has_token(s, "SECP256R1"));
    assert_null(strstr(s, "--mode"));            /* no default for Edwards */
    assert_string_equal(err, "");
    free(s);
    free(err);

    s = run_types("SECP256K1", 0, &err);
    assert_non_null(strstr(s, "--mode " HEM_KEYS_GEN_NIST_MODE));
    free(s);
    free(err);

    s = run_types("NOPE", HEM_KEYTYPES_UNKNOWN, &err);
    assert_int_equal(HEM_KEYTYPES_UNKNOWN, 2);
    assert_string_equal(s, "");
    assert_non_null(strstr(err, "'NOPE'"));
    for (i = 0; i < n; i++) {
        assert_true(has_token(err, t[i].name));
    }
    free(s);
    free(err);
}

/* Registered: family keys, auth none, main section, full per-command help. */
static void test_registered(void **state)
{
    (void)state;
    const hem_command *c = hem_registry_find("keys", "types");
    FILE *f = tmpfile();
    char *s;

    assert_non_null(c);
    assert_int_equal(c->auth, HEM_AUTH_NONE);
    assert_int_equal(c->section, HEM_SECTION_MAIN);
    assert_non_null(f);
    assert_int_equal(hem_help_command(f, "keys", "types"), 0);
    s = slurp(f);
    fclose(f);
    assert_non_null(strstr(s, "keys types [TYPE]"));
    assert_non_null(strstr(s, "auth:  none"));
    free(s);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rows_classify),
        cmocka_unit_test(test_groups_match_classifier),
        cmocka_unit_test(test_nist_mode_default),
        cmocka_unit_test(test_gen_help_lists_all),
        cmocka_unit_test(test_keys_types_output),
        cmocka_unit_test(test_registered),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
