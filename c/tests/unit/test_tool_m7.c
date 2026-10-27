/*
 * test_tool_m7.c — the M7 hem-tool subcommands (`keys update`, `logs`,
 * `selftest`) driven offline through the fake transport.
 *
 * verifies: REQ-TOOL-011 (keys update: label/descr rewrite; the stored descr
 *           is PRESERVED when --descr is omitted [whole-record rewrite,
 *           REQ-KEY-007]; protected current-label → literal-YES ritual with
 *           --yes ignored; rename-to-protected warning; unknown kid → 1;
 *           usage → 2),
 *           REQ-TOOL-012 (logs list pagination walk; logs get verbatim bytes
 *           + id guards; logs key three labeled base64 lines),
 *           REQ-TOOL-013 (selftest: PASS → exit 0, fls_state != 0 → exit 3,
 *           device error → exit 1; repo stats rendered)
 *
 * Harness mirrors test_keys.c (tmpfile captures, scripted stdin, internal
 * clock/KDF seams for the auth exchange).
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
#include "logs.h"
#include "selftest.h"
#include "proto_auth.h"
#include "ejwt.h"
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":2000000000,\"lbl\":\"alice\"}"

#define FAST_KDF_ITERS 1000

static int64_t g_now;
static int64_t test_now_fn(void) { return g_now; }

static void set_now(int64_t now)
{
    g_now = now;
    ehem_auth_test_set_clock(test_now_fn);
    ehem_auth_test_set_kdf_iters(FAST_KDF_ITERS);
}

static ehem_ctx *ctx_with(ehem_transport *fake)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    return ctx;
}

static void push_login(ehem_transport *fake)
{
    char payload[96], seg[160], resp[768];
    int m;
    size_t sn;
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"t\"}",
                 (long long)(EJWT_FX_NOW + 100000));
    sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    snprintf(resp, sizeof resp, "{\"token\":\"hdr.%s.sig\"}", seg);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, resp), 0);
}

/* A tmpfile pre-filled with `content` and rewound (scripted stdin). */
static FILE *scripted(const char *content)
{
    FILE *f = tmpfile();
    assert_non_null(f);
    if (content != NULL && content[0] != '\0') {
        fwrite(content, 1, strlen(content), f);
    }
    rewind(f);
    return f;
}

static void slurp(FILE *f, char *buf, size_t cap)
{
    size_t n;
    rewind(f);
    n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
}

#define KID_A "0000000000000000000000000000000a"
#define KID_P "0000000000000000000000000000000b"

/* One list page holding an ordinary key (with a stored descr, base64 "AQID")
 * and a protected key. */
static void push_update_page(ehem_transport *fake)
{
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"offset\":0,\"total\":2,\"listed\":2,\"list\":["
        "{\"kid\":\"" KID_A "\",\"label\":\"it-key\",\"type\":\"ED25519\","
        "\"descr\":\"AQID\"},"
        "{\"kid\":\"" KID_P "\",\"label\":\"TLS PrivateKey\","
        "\"type\":\"PKEY,GENERIC_DER\"}"
        "]}"), 0);
}

/* -------------------------------------------------------------------------- */
/* keys update (REQ-TOOL-011)                                                 */
/* -------------------------------------------------------------------------- */

/* Ordinary key, --descr omitted: the stored descr rides along (preserved),
 * and the note lands on err. Sequence: login(0,1) list(2) upd-login(3,4)
 * update(5). */
static void test_update_preserves_descr(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake);
    push_update_page(fake);
    push_login(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = ctx_with(fake);
    FILE *out = tmpfile(), *errf = tmpfile();
    hem_keys_update_opts uo;
    memset(&uo, 0, sizeof uo);
    uo.passphrase = EJWT_FX_PASSPHRASE;
    uo.kid   = KID_A;
    uo.label = "renamed";
    uo.out = out; uo.err = errf; uo.in = scripted("");

    assert_int_equal(hem_keys_update_run(ctx, &uo), HEM_KEYS_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 6);
    assert_string_equal(fake_transport_request(fake, 5)->path,
                        "/api/keymgmt/update");
    assert_string_equal((const char *)fake_transport_request(fake, 5)->body,
        "{\"kid\":\"" KID_A "\",\"label\":\"renamed\",\"descr\":\"AQID\"}");

    char buf[512];
    slurp(errf, buf, sizeof buf);
    assert_non_null(strstr(buf, "preserving the stored descr"));
    slurp(out, buf, sizeof buf);
    assert_non_null(strstr(buf, "updated " KID_A));

    fclose(out); fclose(errf); fclose(uo.in);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* --descr "" clears explicitly: the body carries NO descr field. */
static void test_update_clear_descr(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake);
    push_update_page(fake);
    push_login(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = ctx_with(fake);
    hem_keys_update_opts uo;
    memset(&uo, 0, sizeof uo);
    uo.passphrase = EJWT_FX_PASSPHRASE;
    uo.kid   = KID_A;
    uo.label = "renamed";
    uo.descr = "";
    uo.out = tmpfile(); uo.err = tmpfile(); uo.in = scripted("");

    assert_int_equal(hem_keys_update_run(ctx, &uo), HEM_KEYS_OK);
    assert_string_equal((const char *)fake_transport_request(fake, 5)->body,
        "{\"kid\":\"" KID_A "\",\"label\":\"renamed\"}");

    fclose(uo.out); fclose(uo.err); fclose(uo.in);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Protected current label: refusal (anything but YES) skips with exit 0 and
 * NO update request; --yes is ignored; the literal YES proceeds. */
static void test_update_protected_ritual(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    /* Leg 1: refusal (with --yes set — must still prompt). */
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake);
    push_update_page(fake);

    ehem_ctx *ctx = ctx_with(fake);
    hem_keys_update_opts uo;
    memset(&uo, 0, sizeof uo);
    uo.passphrase = EJWT_FX_PASSPHRASE;
    uo.kid   = KID_P;
    uo.label = "innocent name";
    uo.assume_yes = 1;                     /* deliberately ignored */
    uo.out = tmpfile(); uo.err = tmpfile(); uo.in = scripted("yes\n");

    assert_int_equal(hem_keys_update_run(ctx, &uo), HEM_KEYS_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 3);  /* no update */
    char buf[512];
    slurp(uo.out, buf, sizeof buf);
    assert_non_null(strstr(buf, "skipped protected key"));
    fclose(uo.out); fclose(uo.err); fclose(uo.in);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);

    /* Leg 2: the literal YES proceeds. */
    fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake);
    push_update_page(fake);
    push_login(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ctx = ctx_with(fake);
    uo.assume_yes = 0;
    uo.out = tmpfile(); uo.err = tmpfile(); uo.in = scripted("YES\n");
    assert_int_equal(hem_keys_update_run(ctx, &uo), HEM_KEYS_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 6);
    assert_string_equal(fake_transport_request(fake, 5)->path,
                        "/api/keymgmt/update");
    fclose(uo.out); fclose(uo.err); fclose(uo.in);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Renaming an ordinary key TO a protected-looking label warns on err. */
static void test_update_rename_to_protected_warns(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake);
    push_update_page(fake);
    push_login(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = ctx_with(fake);
    hem_keys_update_opts uo;
    memset(&uo, 0, sizeof uo);
    uo.passphrase = EJWT_FX_PASSPHRASE;
    uo.kid   = KID_A;
    uo.label = "Pixel 9 (Android)";
    uo.out = tmpfile(); uo.err = tmpfile(); uo.in = scripted("");

    assert_int_equal(hem_keys_update_run(ctx, &uo), HEM_KEYS_OK);
    char buf[512];
    slurp(uo.err, buf, sizeof buf);
    assert_non_null(strstr(buf, "PROTECTED"));

    fclose(uo.out); fclose(uo.err); fclose(uo.in);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Unknown kid → 1; missing kid/label/passphrase → 2 (no I/O for usage). */
static void test_update_errors(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    hem_keys_update_opts uo;
    memset(&uo, 0, sizeof uo);
    uo.out = tmpfile(); uo.err = tmpfile(); uo.in = scripted("");

    uo.passphrase = NULL;
    uo.kid = KID_A;
    uo.label = "l";
    assert_int_equal(hem_keys_update_run(ctx, &uo), HEM_KEYS_USAGE);
    uo.passphrase = EJWT_FX_PASSPHRASE;
    uo.kid = "nope";
    assert_int_equal(hem_keys_update_run(ctx, &uo), HEM_KEYS_USAGE);
    uo.kid = KID_A;
    uo.label = NULL;
    assert_int_equal(hem_keys_update_run(ctx, &uo), HEM_KEYS_USAGE);
    assert_int_equal((int)fake_transport_request_count(fake), 0);

    /* Unknown kid: the walk finds nothing → RUNTIME. */
    push_login(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"offset\":0,\"total\":0,\"listed\":0,\"list\":[]}"), 0);
    uo.label = "l";
    assert_int_equal(hem_keys_update_run(ctx, &uo), HEM_KEYS_RUNTIME);

    fclose(uo.out); fclose(uo.err); fclose(uo.in);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* logs (REQ-TOOL-012)                                                        */
/* -------------------------------------------------------------------------- */

static void test_logs_list_walks_pages(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"total\":4,\"id\":[\"aa11\",\"bb22\"]}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"total\":4,\"id\":[\"cc33\",\"dd44\"]}"), 0);

    ehem_ctx *ctx = ctx_with(fake);
    hem_logs_opts lo;
    memset(&lo, 0, sizeof lo);
    lo.passphrase = EJWT_FX_PASSPHRASE;
    lo.out = tmpfile(); lo.err = tmpfile();

    assert_int_equal(hem_logs_list_run(ctx, &lo), HEM_LOGS_OK);
    assert_string_equal(fake_transport_request(fake, 2)->path,
                        "/api/logger/list/0");
    assert_string_equal(fake_transport_request(fake, 3)->path,
                        "/api/logger/list/2");

    char buf[256];
    slurp(lo.out, buf, sizeof buf);
    assert_string_equal(buf, "aa11\nbb22\ncc33\ndd44\n");
    slurp(lo.err, buf, sizeof buf);
    assert_non_null(strstr(buf, "total: 4"));

    fclose(lo.out); fclose(lo.err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_logs_get_and_key(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    static const char LOG_TEXT[] = "# Encedo nGINE FW v1.2.2\r\n0|abc|0|0\r\n";

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    /* Each run calls ehem_login, which drops the token cache — every run
     * needs its own login exchange queued. */
    push_login(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  LOG_TEXT), 0);
    push_login(fake);
    /* key triple: 32×0x01, 32×0x02, 64×0x03 (std base64). */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"key\":\"AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=\","
        "\"nonce\":\"AgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgI=\","
        "\"nonce_signed\":\"AwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMD"
        "AwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAw==\"}"), 0);

    ehem_ctx *ctx = ctx_with(fake);
    hem_logs_opts lo;
    memset(&lo, 0, sizeof lo);
    lo.passphrase = EJWT_FX_PASSPHRASE;
    lo.out = tmpfile(); lo.err = tmpfile();

    /* get → verbatim bytes on out. */
    assert_int_equal(hem_logs_get_run(ctx, &lo, "62310b2b", NULL), HEM_LOGS_OK);
    char buf[512];
    slurp(lo.out, buf, sizeof buf);
    assert_string_equal(buf, LOG_TEXT);

    /* key → three labeled lines. */
    fclose(lo.out);
    lo.out = tmpfile();
    assert_int_equal(hem_logs_key_run(ctx, &lo), HEM_LOGS_OK);
    slurp(lo.out, buf, sizeof buf);
    assert_non_null(strstr(buf, "key:          AQEBAQ"));
    assert_non_null(strstr(buf, "nonce:        AgICAg"));
    assert_non_null(strstr(buf, "nonce_signed: AwMDAw"));

    /* id guards → usage, no wire traffic. */
    size_t before = fake_transport_request_count(fake);
    assert_int_equal(hem_logs_get_run(ctx, &lo, NULL, NULL), HEM_LOGS_USAGE);
    assert_int_equal(hem_logs_get_run(ctx, &lo, "../x", NULL), HEM_LOGS_USAGE);
    assert_int_equal((int)fake_transport_request_count(fake), (int)before);

    fclose(lo.out); fclose(lo.err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */
/* selftest (REQ-TOOL-013)                                                    */
/* -------------------------------------------------------------------------- */

static void test_selftest_verdict_exit_codes(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    /* One login exchange per run (ehem_login drops the cache). */
    push_login(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"fls_state\":0,\"selftest_ts\":1705312300,"
        "\"repo_stats\":{\"total\":4,\"deleted\":9,\"invalid\":0,"
        "\"fragmented\":1,\"freeslots\":100}}"), 0);
    push_login(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"fls_state\":2,\"selftest_ts\":1705312301}"), 0);
    push_login(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 500,
        "boom"), 0);

    ehem_ctx *ctx = ctx_with(fake);
    hem_selftest_opts so;
    memset(&so, 0, sizeof so);
    so.passphrase = EJWT_FX_PASSPHRASE;
    so.out = tmpfile(); so.err = tmpfile();

    assert_int_equal(hem_selftest_run(ctx, &so), HEM_SELFTEST_OK);
    char buf[1024];
    slurp(so.out, buf, sizeof buf);
    assert_non_null(strstr(buf, "PASS"));
    assert_non_null(strstr(buf, "free slots"));

    fclose(so.out);
    so.out = tmpfile();
    assert_int_equal(hem_selftest_run(ctx, &so), HEM_SELFTEST_FAILSTATE);
    slurp(so.out, buf, sizeof buf);
    assert_non_null(strstr(buf, "FAIL"));

    assert_int_equal(hem_selftest_run(ctx, &so), HEM_SELFTEST_RUNTIME);

    /* Missing passphrase → usage. */
    so.passphrase = NULL;
    assert_int_equal(hem_selftest_run(ctx, &so), HEM_SELFTEST_USAGE);

    fclose(so.out); fclose(so.err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_update_preserves_descr),
        cmocka_unit_test(test_update_clear_descr),
        cmocka_unit_test(test_update_protected_ritual),
        cmocka_unit_test(test_update_rename_to_protected_warns),
        cmocka_unit_test(test_update_errors),
        cmocka_unit_test(test_logs_list_walks_pages),
        cmocka_unit_test(test_logs_get_and_key),
        cmocka_unit_test(test_selftest_verdict_exit_codes),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
