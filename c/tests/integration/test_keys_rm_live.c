/*
 * test_keys_rm_live.c — live `hem-tool keys rm` against the real dev HEM.
 *
 * verifies: REQ-TOOL-006 (create EHEMTEST keys → keys rm --label-prefix removes
 *           them → verified gone via list), driving the shared hem-tool-core
 *           hem_keys_rm_run() exactly as the CLI does. Strictly confined to the
 *           reserved EHEMTEST prefix (REQ-TEST-003) — no protected key is ever a
 *           target here (they are neither TLS nor phone labels).
 *
 * Links hem-tool-core (the shared tool code, public API), like the disruptive
 * cert-install test. Skipped (exit 77) unless EHEM_TEST_URL + EHEM_TEST_PASSPHRASE.
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
#include "ehem/keymgmt.h"
#include "keys.h"
#include "integration_env.h"
#include "ehem_test_keys.h"

typedef struct rm_state {
    ehem_ctx        *ctx;
    ehem_test_keyreg reg;
} rm_state;

static int setup(void **state)
{
    rm_state *s = calloc(1, sizeof *s);
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
    rm_state *s = *state;
    if (s != NULL) {
        ehem_test_cleanup(s->ctx, &s->reg);   /* NOT_FOUND-tolerant */
        ehem_logout(s->ctx);
        ehem_ctx_destroy(s->ctx);
        free(s);
    }
    return 0;
}

static int in_list(ehem_ctx *ctx, const char *kid)
{
    ehem_key_page *page = NULL;
    size_t i;
    int found = 0;
    assert_int_equal(ehem_key_list_all(ctx, &page), EHEM_OK);
    for (i = 0; i < page->listed; i++) {
        if (strcmp(page->entries[i].kid, kid) == 0) {
            found = 1;
            break;
        }
    }
    ehem_key_page_free(page);
    return found;
}

static void test_rm_label_prefix_removes(void **state)
{
    rm_state *s = *state;
    ehem_key_create_params p;
    char kid_a[EHEM_KID_HEX_SIZE] = {0};
    char kid_b[EHEM_KID_HEX_SIZE] = {0};
    char label_a[32], label_b[32];
    ehem_rc rc;

    ehem_test_label(label_a, sizeof label_a);
    ehem_test_label(label_b, sizeof label_b);

    memset(&p, 0, sizeof p);
    p.type = "ED25519";

    p.label = label_a;
    rc = ehem_key_create(s->ctx, &p, kid_a);
    if (rc != EHEM_OK) {
        fail_msg("create A failed: %s (%s)", ehem_rc_str(rc),
                 ehem_last_error(s->ctx)->message);
    }
    ehem_test_track(&s->reg, kid_a);

    p.label = label_b;
    rc = ehem_key_create(s->ctx, &p, kid_b);
    if (rc != EHEM_OK) {
        fail_msg("create B failed: %s (%s)", ehem_rc_str(rc),
                 ehem_last_error(s->ctx)->message);
    }
    ehem_test_track(&s->reg, kid_b);

    assert_true(in_list(s->ctx, kid_a));
    assert_true(in_list(s->ctx, kid_b));

    /* Drive the exact tool code: keys rm --label-prefix EHEMTEST --yes. The
     * EHEMTEST keys are non-protected, so --yes deletes them with no prompt. */
    const char *prefixes[] = { EHEM_TEST_LABEL_PREFIX };
    FILE *out = tmpfile();
    FILE *errf = tmpfile();
    assert_non_null(out);
    assert_non_null(errf);
    hem_keys_rm_opts ko;
    memset(&ko, 0, sizeof ko);
    ko.passphrase   = ehem_test_passphrase();
    ko.prefixes     = prefixes;
    ko.prefix_count = 1;
    ko.assume_yes   = 1;
    ko.out = out;
    ko.err = errf;
    ko.in  = NULL;   /* no prompt is reached (--yes, no protected targets) */

    assert_int_equal(hem_keys_rm_run(s->ctx, &ko), HEM_KEYS_OK);

    char buf[4096];
    rewind(out);
    size_t n = fread(buf, 1, sizeof buf - 1, out);
    buf[n] = '\0';
    printf("[test_keys_rm_live] tool output:\n%s\n", buf);

    /* Both EHEMTEST keys are gone from the repository. */
    assert_false(in_list(s->ctx, kid_a));
    assert_false(in_list(s->ctx, kid_b));

    fclose(out);
    fclose(errf);
}

int main(void)
{
    ehem_require_test_url();
    if (ehem_test_passphrase() == NULL) {
        fprintf(stderr, "EHEM_TEST_PASSPHRASE not set — skipping live keys rm test\n");
        return EHEM_SKIP_EXIT;
    }

    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_rm_label_prefix_removes, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
