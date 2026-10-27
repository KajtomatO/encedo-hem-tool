/*
 * test_init_tool.c — hem-tool `init-device` orchestration, driven offline
 * through the fake transport.
 *
 * verifies: REQ-TOOL-021 (check-in → GET init → POST init sequence with the
 *           Manager-default cfg and the master key derived from the given
 *           24 words; defaults vs explicit cfg flags; exactly one master
 *           source, missing required flags and an invalid mnemonic → exit 2
 *           with zero traffic; 406 → 3, 403 → 4, 400 → 5; --master-generate
 *           prints 24 words once; --csr-out writes the PEM; --reboot reboots
 *           and waits seen-down-then-back)
 *
 * ATTENDED-ONLY live verification (REQ-TEST-007): this file is the whole
 * automated coverage; the real init happens at the M10 gate.
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
#include "init_cmd.h"
#include "proto_auth.h"
#include "ejwt.h"
#include "json.h"
#include "crypto_shim.h"
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define INIT_CHALLENGE_JSON \
    "{\"exp\":2000000000,\"spk\":\"" EJWT_FX_SPK "\",\"jti\":\"" EJWT_FX_JTI "\"," \
    "\"genuine\":\"GEN\",\"eid\":\"" EJWT_FX_EID "\"}"

static const char CI_CHALLENGE[] = "{\"check\":\"BLOB\"}";
static const char CI_VERIFIED[]  = "{\"checked\":\"CLOUD\"}";
static const char CI_OK[]        = "{\"status\":\"ok\",\"newcrt\":\"\"}";
static const char STATUS_OK[]    =
    "{\"ctx\":0,\"fls_state\":0,\"uptime\":3,\"temp\":35,\"https\":false}";

/* REQ-AUTH-012 vector 1 (the Manager-code fixture, test_bip39.c). */
static const char WORDS[] =
    "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon art";

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
    assert_int_equal(ehem_ctx_create("http://hem.local", &opts, &ctx), EHEM_OK);
    return ctx;
}

static void push(ehem_transport *fake, int status, const char *body)
{
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, body), 0);
}

static void push_checkin(ehem_transport *fake)
{
    push(fake, 200, CI_CHALLENGE);
    push(fake, 200, CI_VERIFIED);
    push(fake, 200, CI_OK);
}

static void make_reply(char *out, size_t cap, const char *extra)
{
    char payload[96], seg[160];
    int m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"init\"}",
                     (long long)(EJWT_FX_NOW + 100000));
    size_t n = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(n, (size_t)-1);
    snprintf(out, cap, "{\"instanceid\":\"uuid-tool-1\",\"token\":\"hdr.%s.sig\"%s}",
             seg, extra);
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

static void base_opts(hem_init_opts *o, FILE *out)
{
    memset(o, 0, sizeof *o);
    o->passphrase = EJWT_FX_PASSPHRASE;
    o->master_words = WORDS;
    o->user = "alice";
    o->email = "alice@example.com";
    o->hostname = "my.ence.do";
    o->out = out;
    o->err = out;
    o->poll_attempts = 4;
    o->poll_delay_ms = 0;
}

/* The cfg object of the init JWT the tool posted (request index `idx`). */
static ehem_json *posted_cfg(ehem_transport *fake, size_t idx, ehem_json **root_out)
{
    const fake_captured_request *post = fake_transport_request(fake, idx);
    assert_int_equal(post->method, EHEM_HTTP_POST);
    assert_string_equal(post->path, "/api/auth/init");
    ehem_json *body = ehem_json_parse((const char *)post->body, post->body_len);
    assert_non_null(body);
    const char *jwt = NULL;
    assert_true(ehem_json_get_string(body, "init", &jwt));
    const char *d1 = strchr(jwt, '.');
    const char *d2 = strchr(d1 + 1, '.');
    static uint8_t plbuf[4096];
    size_t pn = ehem_b64url_decode(d1 + 1, (size_t)(d2 - d1 - 1), plbuf, sizeof plbuf);
    assert_int_not_equal(pn, (size_t)-1);
    ehem_json *claims = ehem_json_parse((const char *)plbuf, pn);
    assert_non_null(claims);
    ehem_json_free(body);
    *root_out = claims;
    return (ehem_json *)ehem_json_get(claims, "cfg");
}

/* Full sequence with the Manager defaults; the master key in the JWT is the
 * one the 24 words derive to. */
static void test_full_defaults(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    char reply[512];
    make_reply(reply, sizeof reply, ",\"reboot_required\":true,\"genuine\":\"G2\"");

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_checkin(fake);                        /* req 0..2 */
    push(fake, 200, INIT_CHALLENGE_JSON);      /* req 3 */
    push(fake, 200, reply);                    /* req 4 */
    ehem_ctx *ctx = ctx_with(fake);
    FILE *out = tmpfile();
    hem_init_opts o;
    base_opts(&o, out);

    assert_int_equal(hem_init_device_run(ctx, &o), HEM_INIT_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 5);
    assert_string_equal(fake_transport_request(fake, 0)->path, "/api/system/checkin");
    assert_string_equal(fake_transport_request(fake, 3)->path, "/api/auth/init");

    ehem_json *claims = NULL;
    ehem_json *cfg = posted_cfg(fake, 4, &claims);
    assert_non_null(cfg);
    const char *s = NULL; int64_t n64 = 0; bool b = false;
    uint8_t secret[32], priv[32], pub[32];
    char pub_b64[64];
    assert_int_equal(ehem_master_secret_from_mnemonic(NULL, WORDS, secret), EHEM_OK);
    assert_int_equal(ehem_x25519_keypair_from_seed(secret, priv, pub), EHEM_OK);
    assert_int_not_equal(ehem_b64_std_encode(pub, 32, pub_b64, sizeof pub_b64), (size_t)-1);
    assert_true(ehem_json_get_string(cfg, "masterkey", &s)); assert_string_equal(s, pub_b64);
    assert_true(ehem_json_get_string(claims, "iss", &s));    assert_string_equal(s, pub_b64);
    assert_true(ehem_json_get_string(cfg, "user", &s));      assert_string_equal(s, "alice");
    assert_true(ehem_json_get_string(cfg, "hostname", &s));  assert_string_equal(s, "my.ence.do");
    assert_true(ehem_json_get_string(cfg, "ip", &s));        assert_string_equal(s, HEM_INIT_DEFAULT_IP);
    assert_true(ehem_json_get_int64(cfg, "storage_mode", &n64));      assert_true(n64 == HEM_INIT_DEFAULT_STORAGE_MODE);
    assert_true(ehem_json_get_int64(cfg, "storage_disk0size", &n64)); assert_true(n64 == HEM_INIT_DEFAULT_DISK0_SIZE);
    assert_true(ehem_json_get_string(cfg, "origin", &s));    assert_string_equal(s, "*");
    assert_true(ehem_json_get_bool(cfg, "trusted_ts", &b));  assert_true(b);
    assert_true(ehem_json_get_bool(cfg, "dnsd", &b));        assert_false(b);
    assert_true(ehem_json_get_bool(cfg, "gen_csr", &b));     assert_false(b);
    ehem_json_free(claims);

    char *text = slurp(out);
    assert_non_null(strstr(text, "instanceid=uuid-tool-1"));
    assert_non_null(strstr(text, "reboot_required=yes"));
    assert_non_null(strstr(text, "hem-tool reboot (required)"));
    assert_null(strstr(text, "MASTER MNEMONIC"));            /* given words are never echoed */
    free(text);
    fclose(out);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Explicit cfg flags reach the JWT; --csr-out writes the PEM. */
static void test_flags_and_csr_out(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    char reply[512];
    make_reply(reply, sizeof reply, ",\"csr\":\"-----BEGIN CERTIFICATE REQUEST-----\\nX\\n-----END CERTIFICATE REQUEST-----\"");
    const char *path = "hem_init_csr_test.pem";   /* in the test's CWD */

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_checkin(fake);
    push(fake, 200, INIT_CHALLENGE_JSON);
    push(fake, 200, reply);
    ehem_ctx *ctx = ctx_with(fake);
    FILE *out = tmpfile();
    hem_init_opts o;
    base_opts(&o, out);
    o.ip = "10.0.0.9/16";
    o.storage_mode = 83;
    o.disk0_size = 4194304;
    o.origin = "https://app";
    o.dnsd = 1;
    o.no_trusted_backend = 1;
    o.gen_csr = 1;
    o.ctx_id = 5;
    o.csr_out = path;

    assert_int_equal(hem_init_device_run(ctx, &o), HEM_INIT_OK);
    ehem_json *claims = NULL;
    ehem_json *cfg = posted_cfg(fake, 4, &claims);
    const char *s = NULL; int64_t n64 = 0; bool b = false;
    assert_true(ehem_json_get_string(cfg, "ip", &s));        assert_string_equal(s, "10.0.0.9/16");
    assert_true(ehem_json_get_int64(cfg, "storage_mode", &n64));      assert_true(n64 == 83);
    assert_true(ehem_json_get_int64(cfg, "storage_disk0size", &n64)); assert_true(n64 == 4194304);
    assert_true(ehem_json_get_string(cfg, "origin", &s));    assert_string_equal(s, "https://app");
    assert_true(ehem_json_get_bool(cfg, "dnsd", &b));        assert_true(b);
    assert_true(ehem_json_get_bool(cfg, "trusted_backend", &b)); assert_false(b);
    assert_true(ehem_json_get_bool(cfg, "gen_csr", &b));     assert_true(b);
    assert_true(ehem_json_get_int64(cfg, "ctx", &n64));      assert_true(n64 == 5);
    ehem_json_free(claims);

    FILE *f = fopen(path, "rb");
    assert_non_null(f);
    char *pem = slurp(f);
    fclose(f);
    remove(path);
    assert_non_null(strstr(pem, "BEGIN CERTIFICATE REQUEST"));
    free(pem);
    char *text = slurp(out);
    assert_non_null(strstr(text, "csr: written to"));
    free(text);
    fclose(out);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Usage errors: zero traffic. */
static void test_usage_errors(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake);
    FILE *out = tmpfile();
    hem_init_opts o;

    base_opts(&o, out); o.passphrase = NULL;
    assert_int_equal(hem_init_device_run(ctx, &o), HEM_INIT_USAGE);
    base_opts(&o, out); o.master_words = NULL;            /* no source */
    assert_int_equal(hem_init_device_run(ctx, &o), HEM_INIT_USAGE);
    base_opts(&o, out); o.master_generate = 1;            /* two sources */
    assert_int_equal(hem_init_device_run(ctx, &o), HEM_INIT_USAGE);
    base_opts(&o, out); o.hostname = NULL;
    assert_int_equal(hem_init_device_run(ctx, &o), HEM_INIT_USAGE);
    base_opts(&o, out); o.master_words = "abandon abandon art";
    assert_int_equal(hem_init_device_run(ctx, &o), HEM_INIT_USAGE);
    base_opts(&o, out); o.master_words = NULL; o.master_hex = "zz";
    assert_int_equal(hem_init_device_run(ctx, &o), HEM_INIT_USAGE);
    assert_int_equal((int)fake_transport_request_count(fake), 0);

    char *text = slurp(out);
    assert_non_null(strstr(text, "need exactly 24"));
    assert_non_null(strstr(text, "64 hex"));
    free(text);
    fclose(out);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Device verdicts after the check-in: 406 → 3, 403 → 4, 400 → 5. */
static void test_device_exits(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    const struct { int get_status; int post_status; int exit; } cases[] = {
        { 406, 0, HEM_INIT_ALREADY }, { 403, 0, HEM_INIT_RTC }, { 200, 400, HEM_INIT_CFG },
        { 200, 401, HEM_INIT_RUNTIME },
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        ehem_transport *fake = fake_transport_new();
        assert_non_null(fake);
        push_checkin(fake);
        if (cases[k].get_status == 200) {
            push(fake, 200, INIT_CHALLENGE_JSON);
            push(fake, cases[k].post_status, NULL);
        } else {
            push(fake, cases[k].get_status, NULL);
        }
        ehem_ctx *ctx = ctx_with(fake);
        FILE *out = tmpfile();
        hem_init_opts o;
        base_opts(&o, out);
        o.master_words = NULL;
        o.master_hex = "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
        assert_int_equal(hem_init_device_run(ctx, &o), cases[k].exit);
        fclose(out);
        ehem_ctx_destroy(ctx);
        fake_transport_free(fake);
    }
}

/* --master-generate prints 24 words once; --reboot reboots and waits. */
static void test_generate_and_reboot(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    char reply[512];
    make_reply(reply, sizeof reply, ",\"reboot_required\":true");

    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_checkin(fake);                                  /* 0..2 */
    push(fake, 200, INIT_CHALLENGE_JSON);                /* 3 */
    push(fake, 200, reply);                              /* 4 */
    push(fake, 200, NULL);                               /* 5: reboot (empty 200) */
    assert_int_equal(fake_transport_push_response(fake, EHEM_ERR_UNREACHABLE, 0, NULL), 0); /* 6 */
    push(fake, 200, STATUS_OK);                          /* 7: back */
    ehem_ctx *ctx = ctx_with(fake);
    FILE *out = tmpfile();
    hem_init_opts o;
    base_opts(&o, out);
    o.master_words = NULL;
    o.master_generate = 1;
    o.reboot = 1;

    assert_int_equal(hem_init_device_run(ctx, &o), HEM_INIT_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 8);
    assert_string_equal(fake_transport_request(fake, 5)->path, "/api/system/reboot");
    assert_non_null(fake_transport_request_header(fake, 5, "Authorization")); /* the cached bearer */

    char *text = slurp(out);
    const char *banner = strstr(text, "MASTER MNEMONIC");
    assert_non_null(banner);
    const char *words = strchr(banner, '\n') + 1;
    int spaces = 0;
    for (const char *p = words; *p != '\n'; p++) {
        spaces += (*p == ' ');
    }
    assert_int_equal(spaces, 23);
    assert_null(strstr(banner + 1, "MASTER MNEMONIC — KEEP"));   /* printed once */
    assert_non_null(strstr(text, "device back"));
    free(text);
    fclose(out);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_full_defaults),
        cmocka_unit_test(test_flags_and_csr_out),
        cmocka_unit_test(test_usage_errors),
        cmocka_unit_test(test_device_exits),
        cmocka_unit_test(test_generate_and_reboot),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
