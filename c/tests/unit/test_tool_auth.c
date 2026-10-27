/*
 * test_tool_auth.c — the shared hem-tool login chokepoint (--mobile,
 * REQ-TOOL-018) through hem-tool-core with the fake transport.
 *
 * verifies: REQ-TOOL-018 (missing credentials → ARG with zero traffic;
 *           passphrase/mobile routing both lazy; the mobile-outcome exit
 *           mapping 13/14 incl. the timeout hint; a representative command
 *           — keys list --mobile — approved / rejected / timeout against a
 *           scripted broker+device, with mobile outranking an
 *           environment-style passphrase; ext pair --mobile rejected with
 *           the sub="U" reason and zero traffic)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "keys.h"
#include "ext_cmd.h"
#include "tool_auth.h"
#include "proto_auth.h"       /* internal: clock/KDF seams */
#include "proto_ext.h"        /* internal: poll-interval seam */
#include "ejwt.h"
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define B64_32 "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="

static int64_t g_now;
static int64_t test_now_fn(void) { return g_now; }

static void set_now(int64_t now)
{
    g_now = now;
    ehem_auth_test_set_clock(test_now_fn);
    ehem_auth_test_set_kdf_iters(1000);
}

static ehem_ctx *ctx_with(ehem_transport *fake, long confirm_timeout_ms)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    opts.confirm_timeout_ms = confirm_timeout_ms;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    return ctx;
}

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

/* Script one full mobile bearer acquisition: broker session → device
 * ext/request → broker event/new → event/check(authreply) → device
 * ext/token (the test_ext_tool.c sequence). */
static void push_mobile_acquisition(ehem_transport *fake)
{
    char payload[64], seg[128], reply[512];
    int m = snprintf(payload, sizeof payload, "{\"exp\":%lld}",
                     (long long)9000000000LL);
    size_t sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m,
                                   seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreq\":\"a.b.c\",\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"eventid\":\"EV-1\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreply\":\"ar.j.w\"}"), 0);
    snprintf(reply, sizeof reply, "{\"token\":\"hdr.%s.sig\"}", seg);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, reply),
                     0);
}

/* Same up to the push, then the phone denies / never answers. */
static void push_mobile_until_event(ehem_transport *fake, const char *ev)
{
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreq\":\"a.b.c\",\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, ev), 0);
}

/* --- the chokepoint itself ------------------------------------------------ */

static void test_login_missing_credentials(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake, 0);
    FILE *err = tmpfile();
    assert_non_null(err);

    assert_int_equal(hem_tool_login(ctx, NULL, false, err), EHEM_ERR_ARG);
    assert_int_equal(fake_transport_request_count(fake), 0);
    char *msg = slurp(err);
    assert_non_null(strstr(msg, "--mobile"));
    free(msg);

    fclose(err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_login_routing_is_lazy(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake, 0);

    /* Both modes are lazy — entering them produces zero traffic. */
    assert_int_equal(hem_tool_login(ctx, EJWT_FX_PASSPHRASE, false, NULL),
                     EHEM_OK);
    assert_int_equal(fake_transport_request_count(fake), 0);
    assert_int_equal(hem_tool_login(ctx, NULL, true, NULL), EHEM_OK);
    assert_int_equal(fake_transport_request_count(fake), 0);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_exit_mapper(void **state)
{
    (void)state;
    FILE *err = tmpfile();
    assert_non_null(err);

    assert_int_equal(hem_tool_auth_exit(EHEM_ERR_CONFIRM_TIMEOUT, err, 1),
                     HEM_TOOL_EXIT_CONFIRM_TIMEOUT);
    assert_int_equal(hem_tool_auth_exit(EHEM_ERR_USER_REJECTED, err, 1),
                     HEM_TOOL_EXIT_REJECTED);
    /* Anything else passes the command's own code through. */
    assert_int_equal(hem_tool_auth_exit(EHEM_ERR_DEVICE, err, 7), 7);
    assert_int_equal(hem_tool_auth_exit(EHEM_ERR_NETWORK, err, 1), 1);

    /* The timeout hint points at the pairing check (user decision
     * 2026-08-06: no-pairing is NOT separately detectable in pure mobile
     * mode — the push goes to nobody and the confirm expires). */
    char *msg = slurp(err);
    assert_non_null(strstr(msg, "ext list"));
    free(msg);
    fclose(err);
}

/* --- representative command: keys list --mobile --------------------------- */

static void test_keys_list_mobile_outcomes(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_ext_test_set_poll_interval(1);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake, 2);   /* 2 ms confirmation timeout */
    FILE *out = tmpfile();
    assert_non_null(out);

    hem_keys_opts ko;
    memset(&ko, 0, sizeof ko);
    ko.mobile = true;
    /* An environment-style passphrase is present but OUTRANKED by mobile:
     * the scripted responses only match the broker flow, so success proves
     * the mobile path ran (REQ-TOOL-018 precedence). */
    ko.passphrase = EJWT_FX_PASSPHRASE;
    ko.out = out;
    ko.err = out;

    /* Approved on the phone → the command proceeds normally. */
    push_mobile_acquisition(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"offset\":0,\"total\":0,\"listed\":0,\"list\":[]}"), 0);
    assert_int_equal(hem_keys_list_run(ctx, &ko), HEM_KEYS_OK);

    /* Rejected on the phone → tool-wide exit 14. */
    push_mobile_until_event(fake, "{\"eventid\":\"EV-2\"}");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"deny\":true}"), 0);
    assert_int_equal(hem_keys_list_run(ctx, &ko), HEM_TOOL_EXIT_REJECTED);

    /* Never answered (timeout 2 ms / interval 1 ms → 3 polls) → 13 + hint. */
    push_mobile_until_event(fake, "{\"eventid\":\"EV-3\"}");
    for (int i = 0; i < 3; i++) {
        assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202,
                                                      NULL), 0);
    }
    assert_int_equal(hem_keys_list_run(ctx, &ko),
                     HEM_TOOL_EXIT_CONFIRM_TIMEOUT);
    char *printed = slurp(out);
    assert_non_null(strstr(printed, "ext list"));
    free(printed);

    fclose(out);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* --- the registry class guard (REQ-TOOL-019 rev 2) ----------------------- */

/* --mobile on a PASSPHRASE_ONLY command is refused before any traffic, with
 * the sub="U" reason (and "M" for the config-writing recovery blocks);
 * bearer commands and no-auth commands pass; unknown commands pass. */
static void test_auth_class_guard(void **state)
{
    (void)state;
    FILE *err = tmpfile();
    assert_non_null(err);

    assert_int_equal(hem_tool_check_auth_class(hem_registry_find("cert-install", NULL), true, err), 2);
    assert_int_equal(hem_tool_check_auth_class(hem_registry_find("tls-recover", NULL), true, err), 2);
    assert_int_equal(hem_tool_check_auth_class(hem_registry_find("ext", "pair"), true, err), 2);
    char *msg = slurp(err);
    assert_non_null(strstr(msg, "cert-install cannot use --mobile"));
    assert_non_null(strstr(msg, "sub=\"U\" or \"M\""));
    assert_non_null(strstr(msg, "ext pair cannot use --mobile"));
    free(msg);

    assert_int_equal(hem_tool_check_auth_class(hem_registry_find("cert-install", NULL), false, err), 0);
    assert_int_equal(hem_tool_check_auth_class(hem_registry_find("keys", "list"), true, err), 0);
    assert_int_equal(hem_tool_check_auth_class(hem_registry_find("reboot", NULL), true, err), 0);
    assert_int_equal(hem_tool_check_auth_class(hem_registry_find("status", NULL), true, err), 0);
    assert_int_equal(hem_tool_check_auth_class(NULL, true, err), 0);
    fclose(err);
}

/* --- ext pair stays passphrase-only --------------------------------------- */

static void test_ext_pair_mobile_rejected(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake, 0);
    FILE *err = tmpfile();
    assert_non_null(err);

    hem_ext_pair_opts po;
    memset(&po, 0, sizeof po);
    po.mobile = true;
    po.passphrase = EJWT_FX_PASSPHRASE;   /* even with one available */
    po.err = err;
    po.out = err;

    assert_int_equal(hem_ext_pair_run(ctx, &po), HEM_EXT_USAGE);
    assert_int_equal(fake_transport_request_count(fake), 0);
    char *msg = slurp(err);
    assert_non_null(strstr(msg, "sub=\"U\""));
    free(msg);

    fclose(err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_login_missing_credentials),
        cmocka_unit_test(test_login_routing_is_lazy),
        cmocka_unit_test(test_exit_mapper),
        cmocka_unit_test(test_keys_list_mobile_outcomes),
        cmocka_unit_test(test_ext_pair_mobile_rejected),
        cmocka_unit_test(test_auth_class_guard),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
