/*
 * test_random_tool.c — the hem-tool `random` subcommand (hem-tool-core
 * random.c) driven offline through the fake transport.
 *
 * verifies: REQ-TOOL-010 (`random 20 --kid K` issues 2 encrypt requests
 *           against K and prints 40 hex chars + newline; --raw writes
 *           exactly N bytes; without --kid the sequence is create → harvest
 *           → delete, and the delete also runs on an injected harvest
 *           failure; N=0 / N>4096 / non-numeric / missing passphrase →
 *           exit 2 with zero transport calls; a delete failure → exit 1
 *           with the leftover kid named)
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "ehem/ehem.h"
#include "ehem/auth.h"
#include "proto_auth.h"
#include "ejwt.h"
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#include "random.h"

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

static void push_login(ehem_transport *fake, const char *tag)
{
    char payload[96], seg[160], resp[768];
    int m;
    size_t sn;
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CHALLENGE_JSON), 0);
    m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"%s\"}",
                 (long long)(EJWT_FX_NOW + 100000), tag);
    sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    snprintf(resp, sizeof resp, "{\"token\":\"hdr.%s.sig\"}", seg);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, resp), 0);
}

#define TEST_KID "09bd0958e1499ecfd51ea62a3f49a84c"
#define NEW_KID  "aabbccddeeff00112233445566778899"

/* CBC-shaped encrypt response; IV = 16 bytes of 0x0f ("Dw8PDw8PDw8PDw8PDw8PDw=="
 * — computed below at runtime to stay honest). */
static void push_cbc_iv(ehem_transport *fake, uint8_t fill)
{
    uint8_t iv[16];
    char iv_b64[32];
    char resp[128];
    size_t n;

    memset(iv, fill, sizeof iv);
    n = ehem_b64_std_encode(iv, sizeof iv, iv_b64, sizeof iv_b64);
    assert_int_not_equal(n, (size_t)-1);
    snprintf(resp, sizeof resp,
             "{\"ciphertext\":\"BAULDA==\",\"iv\":\"%s\"}", iv_b64);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, resp), 0);
}

/* Read everything a tmpfile collected. */
static size_t slurp(FILE *f, char *buf, size_t cap)
{
    size_t n;
    rewind(f);
    n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    return n;
}

static int count_method(ehem_transport *fake, ehem_http_method m)
{
    int n = 0;
    for (size_t i = 0; i < fake_transport_request_count(fake); i++) {
        if (fake_transport_request(fake, i)->method == m) {
            n++;
        }
    }
    return n;
}

/* -------------------------------------------------------------------------- */

static void test_random_with_kid_hex_output(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "t1");
    push_cbc_iv(fake, 0xab);
    push_cbc_iv(fake, 0xcd);

    ehem_ctx *ctx = ctx_with(fake);
    FILE *out = tmpfile();
    FILE *err = tmpfile();
    hem_random_opts o;
    char got[256];

    memset(&o, 0, sizeof o);
    o.passphrase = EJWT_FX_PASSPHRASE;
    o.count_arg  = "20";
    o.kid        = TEST_KID;
    o.out        = out;
    o.err        = err;

    assert_int_equal(hem_random_run(ctx, &o), HEM_RANDOM_OK);

    /* 40 hex chars + newline: 16×"ab" then 4×"cd". */
    slurp(out, got, sizeof got);
    assert_int_equal((int)strlen(got), 41);
    assert_memory_equal(got,
        "abababababababababababababababab", 32);
    assert_memory_equal(got + 32, "cdcdcdcd\n", 9);

    /* login (2) + 2 encrypts; NO create/delete with --kid. */
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    assert_int_equal(count_method(fake, EHEM_HTTP_DELETE), 0);

    fclose(out);
    fclose(err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_random_raw_output(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "t2");
    push_cbc_iv(fake, 0x77);

    ehem_ctx *ctx = ctx_with(fake);
    FILE *out = tmpfile();
    hem_random_opts o;
    char got[64];
    size_t n;

    memset(&o, 0, sizeof o);
    o.passphrase = EJWT_FX_PASSPHRASE;
    o.count_arg  = "9";
    o.kid        = TEST_KID;
    o.raw        = 1;
    o.out        = out;
    o.err        = tmpfile();

    assert_int_equal(hem_random_run(ctx, &o), HEM_RANDOM_OK);
    n = slurp(out, got, sizeof got);
    assert_int_equal((int)n, 9);          /* exactly N bytes, no newline */
    for (int i = 0; i < 9; i++) {
        assert_int_equal((uint8_t)got[i], 0x77);
    }

    fclose(out);
    fclose(o.err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_random_transient_key_lifecycle(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    /* login(gen) + create + login(use) + encrypt + login(del) + delete. */
    push_login(fake, "t3a");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"kid\":\"" NEW_KID "\"}"),
                     0);
    push_login(fake, "t3b");
    push_cbc_iv(fake, 0x42);
    push_login(fake, "t3c");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = ctx_with(fake);
    FILE *out = tmpfile();
    hem_random_opts o;

    memset(&o, 0, sizeof o);
    o.passphrase = EJWT_FX_PASSPHRASE;
    o.count_arg  = "16";
    o.out        = out;
    o.err        = tmpfile();

    assert_int_equal(hem_random_run(ctx, &o), HEM_RANDOM_OK);

    assert_int_equal((int)fake_transport_request_count(fake), 9);
    const fake_captured_request *create = fake_transport_request(fake, 2);
    assert_non_null(create);
    assert_string_equal(create->path, "/api/keymgmt/create");
    assert_non_null(strstr((const char *)create->body,
                           "\"label\":\"EHEMTEST hem-tool random\""));
    assert_non_null(strstr((const char *)create->body, "\"type\":\"AES128\""));
    const fake_captured_request *del = fake_transport_request(fake, 8);
    assert_non_null(del);
    assert_int_equal(del->method, EHEM_HTTP_DELETE);
    assert_string_equal(del->path, "/api/keymgmt/delete/" NEW_KID);

    fclose(out);
    fclose(o.err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_random_delete_runs_on_harvest_failure(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "t4a");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"kid\":\"" NEW_KID "\"}"),
                     0);
    push_login(fake, "t4b");
    /* Harvest fails: device 406 on the encrypt. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406, NULL), 0);
    push_login(fake, "t4c");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, NULL), 0);

    ehem_ctx *ctx = ctx_with(fake);
    hem_random_opts o;

    memset(&o, 0, sizeof o);
    o.passphrase = EJWT_FX_PASSPHRASE;
    o.count_arg  = "16";
    o.out        = tmpfile();
    o.err        = tmpfile();

    assert_int_equal(hem_random_run(ctx, &o), HEM_RANDOM_RUNTIME);

    /* The delete STILL happened. */
    assert_int_equal(count_method(fake, EHEM_HTTP_DELETE), 1);
    const fake_captured_request *del =
        fake_transport_request(fake, fake_transport_request_count(fake) - 1);
    assert_string_equal(del->path, "/api/keymgmt/delete/" NEW_KID);

    fclose(o.out);
    fclose(o.err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_random_usage_errors_no_io(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    hem_random_opts o;
    static const char *bad_counts[] = { NULL, "", "0", "4097", "12x", "-4" };

    for (size_t i = 0; i < sizeof bad_counts / sizeof bad_counts[0]; i++) {
        memset(&o, 0, sizeof o);
        o.passphrase = EJWT_FX_PASSPHRASE;
        o.count_arg  = bad_counts[i];
        o.out        = tmpfile();
        o.err        = tmpfile();
        assert_int_equal(hem_random_run(ctx, &o), HEM_RANDOM_USAGE);
        fclose(o.out);
        fclose(o.err);
    }

    /* Missing passphrase; malformed --kid. */
    memset(&o, 0, sizeof o);
    o.count_arg = "16";
    o.out = tmpfile();
    o.err = tmpfile();
    assert_int_equal(hem_random_run(ctx, &o), HEM_RANDOM_USAGE);
    fclose(o.out);
    fclose(o.err);

    memset(&o, 0, sizeof o);
    o.passphrase = EJWT_FX_PASSPHRASE;
    o.count_arg = "16";
    o.kid = "not-a-kid";
    o.out = tmpfile();
    o.err = tmpfile();
    assert_int_equal(hem_random_run(ctx, &o), HEM_RANDOM_USAGE);
    fclose(o.out);
    fclose(o.err);

    assert_int_equal((int)fake_transport_request_count(fake), 0);

    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

static void test_random_delete_failure_is_runtime(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "t5a");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  "{\"kid\":\"" NEW_KID "\"}"),
                     0);
    push_login(fake, "t5b");
    push_cbc_iv(fake, 0x01);
    push_login(fake, "t5c");
    /* Delete fails: device 406. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406, NULL), 0);

    ehem_ctx *ctx = ctx_with(fake);
    hem_random_opts o;
    char errtxt[512];

    memset(&o, 0, sizeof o);
    o.passphrase = EJWT_FX_PASSPHRASE;
    o.count_arg  = "8";
    o.out        = tmpfile();
    o.err        = tmpfile();

    assert_int_equal(hem_random_run(ctx, &o), HEM_RANDOM_RUNTIME);
    slurp(o.err, errtxt, sizeof errtxt);
    assert_non_null(strstr(errtxt, NEW_KID));   /* leftover kid named */

    fclose(o.out);
    fclose(o.err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_random_with_kid_hex_output),
        cmocka_unit_test(test_random_raw_output),
        cmocka_unit_test(test_random_transient_key_lifecycle),
        cmocka_unit_test(test_random_delete_runs_on_harvest_failure),
        cmocka_unit_test(test_random_usage_errors_no_io),
        cmocka_unit_test(test_random_delete_failure_is_runtime),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
