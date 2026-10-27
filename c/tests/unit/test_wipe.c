/*
 * test_wipe.c — hem-tool `wipe-device` orchestration, driven offline through
 * the fake transport.
 *
 * verifies: REQ-TOOL-022 (declined / wrong-hostname confirmations → exit 3
 *           with zero device writes; the exact hostname → ONE wipeout POST;
 *           there is no --yes to skip the prompt; --mobile → exit 2 and
 *           missing passphrase → exit 2 with zero traffic; --wait polls the
 *           http:// probe until seen-down-then-back (exit 0) or exhaustion
 *           (exit 4); a failed wipeout maps to exit 1; the http URL helper)
 *
 * ATTENDED-ONLY live verification (REQ-TEST-007): this file is the whole
 * automated coverage; the real wipe happens at the M10 gate.
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
#include "wipe.h"
#include "proto_auth.h"
#include "ejwt.h"
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":2000000000,\"lbl\":\"alice\"}"

static const char CONFIG_JSON[] =
    "{\"devid\":\"3dfd39eb56787905\",\"hostname\":\"my.ence.do\","
    "\"user\":\"usb C\",\"instanceid\":\"uuid-1\"}";
static const char STATUS_HTTP_ONLY[] =
    "{\"ctx\":0,\"fls_state\":0,\"uptime\":3,\"temp\":35,\"https\":false}";

#define FAST_KDF_ITERS 1000

static int64_t g_now;
static int64_t test_now_fn(void) { return g_now; }

static void set_now(int64_t now)
{
    g_now = now;
    ehem_auth_test_set_clock(test_now_fn);
    ehem_auth_test_set_kdf_iters(FAST_KDF_ITERS);
}

static ehem_ctx *ctx_with(ehem_transport *fake, const char *url)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    assert_int_equal(ehem_ctx_create(url, &opts, &ctx), EHEM_OK);
    return ctx;
}

static void push(ehem_transport *fake, int status, const char *body)
{
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, body), 0);
}

static void push_login(ehem_transport *fake, const char *tag)
{
    char payload[96], seg[160], resp[768];
    int m;
    size_t sn;
    push(fake, 200, CHALLENGE_JSON);
    m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"%s\"}",
                 (long long)(EJWT_FX_NOW + 100000), tag);
    sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    snprintf(resp, sizeof resp, "{\"token\":\"hdr.%s.sig\"}", seg);
    push(fake, 200, resp);
}

/* A FILE* holding the operator's typed answer. */
static FILE *answer(const char *text)
{
    FILE *f = tmpfile();
    assert_non_null(f);
    fputs(text, f);
    rewind(f);
    return f;
}

static char *slurp(FILE *f)
{
    long n;
    char *buf;
    assert_int_equal(fseek(f, 0, SEEK_END), 0);
    n = ftell(f);
    rewind(f);
    buf = malloc((size_t)n + 1);
    assert_non_null(buf);
    assert_int_equal(fread(buf, 1, (size_t)n, f), (size_t)n);
    buf[n] = '\0';
    return buf;
}

static void base_opts(hem_wipe_opts *o, FILE *in, FILE *out, FILE *err)
{
    memset(o, 0, sizeof *o);
    o->passphrase = EJWT_FX_PASSPHRASE;
    o->in = in;
    o->out = out;
    o->err = err;
    o->poll_attempts = 4;
    o->poll_delay_ms = 0;
}

/* Declined ("no") and wrong hostname → exit 3; the identity was shown; the
 * only traffic is login + config GET — no write ever happened. */
static void test_declined_and_wrong_hostname(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    const char *answers[] = { "no\n", "my.ence.de\n", "" };   /* "" = EOF */
    for (int k = 0; k < 3; k++) {
        ehem_transport *fake = fake_transport_new();
        assert_non_null(fake);
        push_login(fake, "w");                       /* req 0,1 */
        push(fake, 200, CONFIG_JSON);                /* req 2 */
        ehem_ctx *ctx = ctx_with(fake, "https://hem.local");
        FILE *in = answer(answers[k]);
        FILE *out = tmpfile();
        hem_wipe_opts o;
        base_opts(&o, in, out, out);

        assert_int_equal(hem_wipe_device_run(ctx, &o), HEM_WIPE_DECLINED);
        assert_int_equal((int)fake_transport_request_count(fake), 3);
        char *text = slurp(out);
        assert_non_null(strstr(text, "my.ence.do"));
        assert_non_null(strstr(text, "3dfd39eb56787905"));
        assert_non_null(strstr(text, "uuid-1"));
        assert_non_null(strstr(text, "aborted"));
        free(text);
        fclose(in);
        fclose(out);
        ehem_ctx_destroy(ctx);
        fake_transport_free(fake);
    }
}

/* The exact hostname → exactly one wipeout POST; exit 0 without --wait. */
static void test_confirmed_wipe(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "w");                           /* req 0,1 */
    push(fake, 200, CONFIG_JSON);                    /* req 2 */
    push(fake, 200, NULL);                           /* req 3: the empty 200 */
    ehem_ctx *ctx = ctx_with(fake, "https://hem.local");
    FILE *in = answer("my.ence.do\n");
    FILE *out = tmpfile();
    hem_wipe_opts o;
    base_opts(&o, in, out, out);

    assert_int_equal(hem_wipe_device_run(ctx, &o), HEM_WIPE_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 4);
    const fake_captured_request *post = fake_transport_request(fake, 3);
    assert_int_equal(post->method, EHEM_HTTP_POST);
    assert_string_equal(post->path, "/api/system/config");
    assert_string_equal((const char *)post->body, "{\"wipeout\":true}");
    char *text = slurp(out);
    assert_non_null(strstr(text, "wipe accepted"));
    assert_non_null(strstr(text, "init-device"));
    free(text);
    fclose(in);
    fclose(out);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* --mobile and a missing passphrase are refused with zero traffic. */
static void test_usage_rejections(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake, "https://hem.local");
    FILE *in = answer("my.ence.do\n");
    FILE *err = tmpfile();
    hem_wipe_opts o;

    base_opts(&o, in, err, err);
    o.mobile = true;
    assert_int_equal(hem_wipe_device_run(ctx, &o), HEM_WIPE_USAGE);
    char *msg = slurp(err);
    assert_non_null(strstr(msg, "sub=\"U\""));
    free(msg);

    base_opts(&o, in, err, err);
    o.passphrase = NULL;
    assert_int_equal(hem_wipe_device_run(ctx, &o), HEM_WIPE_USAGE);
    base_opts(&o, in, err, err);
    o.passphrase = "";
    assert_int_equal(hem_wipe_device_run(ctx, &o), HEM_WIPE_USAGE);

    assert_int_equal((int)fake_transport_request_count(fake), 0);
    fclose(in);
    fclose(err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* A refused wipeout (403: e.g. a wrong role) → exit 1 with the detail. */
static void test_wipeout_refused(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_login(fake, "w");
    push(fake, 200, CONFIG_JSON);
    push(fake, 403, "{\"error\":\"forbidden\"}");
    ehem_ctx *ctx = ctx_with(fake, "https://hem.local");
    FILE *in = answer("my.ence.do\n");
    FILE *err = tmpfile();
    hem_wipe_opts o;
    base_opts(&o, in, err, err);

    assert_int_equal(hem_wipe_device_run(ctx, &o), HEM_WIPE_RUNTIME);
    char *msg = slurp(err);
    assert_non_null(strstr(msg, "wipeout"));
    assert_non_null(strstr(msg, "403"));
    free(msg);
    fclose(in);
    fclose(err);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* --wait: the probe (http:// context) must see the device DOWN and then
 * answering again → exit 0 naming the http-only state; never back → 4. */
static void test_wait_back_and_timeout(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    for (int back = 1; back >= 0; back--) {
        ehem_transport *fake = fake_transport_new();
        ehem_transport *pf = fake_transport_new();
        assert_non_null(fake);
        assert_non_null(pf);
        push_login(fake, "w");
        push(fake, 200, CONFIG_JSON);
        push(fake, 200, NULL);
        /* probe: old instance still up, then down, down, then back (or not) */
        assert_int_equal(fake_transport_push_response(pf, EHEM_OK, 200, STATUS_HTTP_ONLY), 0);
        assert_int_equal(fake_transport_push_response(pf, EHEM_ERR_UNREACHABLE, 0, NULL), 0);
        assert_int_equal(fake_transport_push_response(pf, EHEM_ERR_UNREACHABLE, 0, NULL), 0);
        if (back) {
            assert_int_equal(fake_transport_push_response(pf, EHEM_OK, 200, STATUS_HTTP_ONLY), 0);
        } else {
            assert_int_equal(fake_transport_push_response(pf, EHEM_ERR_UNREACHABLE, 0, NULL), 0);
        }
        ehem_ctx *ctx = ctx_with(fake, "https://hem.local");
        ehem_ctx *probe = ctx_with(pf, "http://hem.local");
        FILE *in = answer("my.ence.do\n");
        FILE *out = tmpfile();
        hem_wipe_opts o;
        base_opts(&o, in, out, out);
        o.wait_back = 1;
        o.probe = probe;

        int rc = hem_wipe_device_run(ctx, &o);
        char *text = slurp(out);
        if (back) {
            assert_int_equal(rc, HEM_WIPE_OK);
            assert_non_null(strstr(text, "device back (https: no)"));
        } else {
            assert_int_equal(rc, HEM_WIPE_TIMEOUT);
            assert_non_null(strstr(text, "did not answer again"));
        }
        assert_int_equal((int)fake_transport_request_count(pf), 4);
        free(text);
        fclose(in);
        fclose(out);
        ehem_ctx_destroy(probe);
        ehem_ctx_destroy(ctx);
        fake_transport_free(pf);
        fake_transport_free(fake);
    }
}

static void test_http_url(void **state)
{
    (void)state;
    char buf[64];
    assert_true(hem_wipe_http_url("https://my.ence.do", buf, sizeof buf));
    assert_string_equal(buf, "http://my.ence.do");
    assert_true(hem_wipe_http_url("http://10.0.0.2", buf, sizeof buf));
    assert_string_equal(buf, "http://10.0.0.2");
    assert_false(hem_wipe_http_url("https://my.ence.do", buf, 8));
    assert_false(hem_wipe_http_url(NULL, buf, sizeof buf));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_declined_and_wrong_hostname),
        cmocka_unit_test(test_confirmed_wipe),
        cmocka_unit_test(test_usage_rejections),
        cmocka_unit_test(test_wipeout_refused),
        cmocka_unit_test(test_wait_back_and_timeout),
        cmocka_unit_test(test_http_url),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
