/*
 * test_random_live.c — live device hardware RNG: ehem_random on an EHEMTEST
 * AES key (two calls differ) and the hem-tool `random` transient-key flow
 * (output shape, run-to-run difference, no leftovers).
 *
 * verifies: REQ-TOOL-010 (live: `random 32` without --kid prints 64 hex
 *           chars, two runs differ, and no EHEMTEST key remains afterwards;
 *           on the way the SDK's REQ-OPS-002 is exercised - two consecutive
 *           ehem_random calls return non-equal output, the harvest riding
 *           AES128-CBC encrypts on the caller's key - verified in the SDK
 *           repository)
 *
 * Drives hem-tool-core (public API only), like test_keys_rm_live.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "ehem/crypto.h"
#include "ehem/keymgmt.h"
#include "integration_env.h"
#include "ehem_test_keys.h"

#include "random.h"

typedef struct rnd_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} rnd_state;

static int setup(void **state)
{
    rnd_state *s = calloc(1, sizeof *s);
    if (s == NULL) {
        return -1;
    }
    if (ehem_test_ctx(&s->ctx) != EHEM_OK ||
        ehem_login(s->ctx, ehem_test_passphrase()) != EHEM_OK) {
        free(s);
        return -1;
    }
    ehem_test_sweep(s->ctx);
    *state = s;
    return 0;
}

static int teardown(void **state)
{
    rnd_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

static void test_sdk_random_two_calls_differ(void **state)
{
    rnd_state *s = *state;
    ehem_key_create_params p;
    char kid[EHEM_KID_HEX_SIZE];
    char label[32];
    uint8_t a[48], b[48];

    memset(&p, 0, sizeof p);
    ehem_test_label(label, sizeof label);
    p.type = "AES256";      /* any AES width serves the AES128-CBC harvest */
    p.label = label;
    assert_int_equal(ehem_key_create(s->ctx, &p, kid), EHEM_OK);
    ehem_test_track(&s->reg, kid);

    assert_int_equal(ehem_random(s->ctx, kid, a, sizeof a), EHEM_OK);
    assert_int_equal(ehem_random(s->ctx, kid, b, sizeof b), EHEM_OK);
    assert_memory_not_equal(a, b, sizeof a);

    /* Delete now (not in teardown): the hygiene test below asserts a clean
     * device, and setup/teardown are group-scoped in this suite. */
    assert_int_equal(ehem_key_delete(s->ctx, kid), EHEM_OK);
    ehem_test_untrack(&s->reg, kid);
}

static void run_tool(rnd_state *s, char *hex_out, size_t cap)
{
    hem_random_opts o;
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    size_t n;

    assert_non_null(out);
    assert_non_null(err);
    memset(&o, 0, sizeof o);
    o.passphrase = ehem_test_passphrase();
    o.count_arg  = "32";
    o.out        = out;
    o.err        = err;

    assert_int_equal(hem_random_run(s->ctx, &o), HEM_RANDOM_OK);

    rewind(out);
    n = fread(hex_out, 1, cap - 1, out);
    hex_out[n] = '\0';
    assert_int_equal((int)n, 65);        /* 64 hex chars + newline */
    for (size_t i = 0; i < 64; i++) {
        assert_true((hex_out[i] >= '0' && hex_out[i] <= '9') ||
                    (hex_out[i] >= 'a' && hex_out[i] <= 'f'));
    }

    fclose(out);
    fclose(err);
}

static void test_tool_transient_key_and_hygiene(void **state)
{
    rnd_state *s = *state;
    char run1[80], run2[80];
    int leftovers;

    run_tool(s, run1, sizeof run1);
    run_tool(s, run2, sizeof run2);
    assert_string_not_equal(run1, run2);

    /* REQ-TEST-003 hygiene: the transient keys are gone — a sweep finds
     * nothing EHEMTEST-labeled to delete. */
    leftovers = ehem_test_sweep(s->ctx);
    assert_int_equal(leftovers, 0);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sdk_random_two_calls_differ),
        cmocka_unit_test(test_tool_transient_key_and_hygiene),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, setup, teardown);
    ehem_global_cleanup();
    return failed;
}
