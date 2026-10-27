/*
 * test_keys_gen.c — hem-tool `keys gen` (hem-tool-core hem_keys_gen_run)
 * driven offline through the fake transport.
 *
 * verifies: REQ-TOOL-009 (body carries exactly {type,label[,mode][,descr]};
 *           a NIST-P/K type without --mode sends "ECDH,ExDSA" with a note; a
 *           non-NIST type without --mode sends no mode; explicit --mode is
 *           verbatim; missing type/label/passphrase or a bad --mode literal →
 *           exit 2 with NO transport traffic; SDK EHEM_ERR_ARG (label bound) →
 *           exit 2, no create sent; device 400/406 → exit 1)
 *
 * Login fixtures mirror test_keys_pub.c; output captured via tmpfile().
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
#include "proto_auth.h"
#include "ejwt.h"
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":2000000000,\"lbl\":\"alice\"}"

#define FAST_KDF_ITERS 1000
#define NEW_KID "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

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
    m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"kp\"}",
                 (long long)(EJWT_FX_NOW + 100000));
    sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    snprintf(resp, sizeof resp, "{\"token\":\"hdr.%s.sig\"}", seg);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, resp), 0);
}

/* Run hem_keys_gen_run with an optional scripted create response. When
 * `create_status` is 0 no create is scripted (used for pure-usage tests, so
 * even the login is never consumed). Captures err text + request count. */
static int run_gen(const char *type, const char *label, const char *descr,
                   const char *mode, const char *passphrase,
                   long create_status, const char *create_body,
                   char *out_buf, size_t out_cap,
                   char *err_buf, size_t err_cap,
                   size_t *request_count, char *body_buf, size_t body_cap)
{
    ehem_transport *fake = fake_transport_new();
    FILE *out = tmpfile();
    FILE *errf = tmpfile();
    hem_keys_gen_opts o;
    int ret;

    assert_non_null(fake);
    assert_non_null(out);
    assert_non_null(errf);
    set_now(EJWT_FX_NOW);
    if (create_status != 0) {
        push_login(fake);
        assert_int_equal(fake_transport_push_response(fake, EHEM_OK,
                                                      create_status,
                                                      create_body), 0);
    }

    ehem_ctx *ctx = ctx_with(fake);
    memset(&o, 0, sizeof o);
    o.passphrase = passphrase;
    o.type = type;
    o.label = label;
    o.descr = descr;
    o.mode = mode;
    o.out = out;
    o.err = errf;

    ret = hem_keys_gen_run(ctx, &o);

    if (out_buf != NULL) {
        rewind(out);
        size_t n = fread(out_buf, 1, out_cap - 1, out);
        out_buf[n] = '\0';
    }
    if (err_buf != NULL) {
        rewind(errf);
        size_t n = fread(err_buf, 1, err_cap - 1, errf);
        err_buf[n] = '\0';
    }
    if (request_count != NULL) {
        *request_count = fake_transport_request_count(fake);
    }
    if (body_buf != NULL) {
        size_t n = fake_transport_request_count(fake);
        body_buf[0] = '\0';
        if (n > 0) {
            const uint8_t *b = fake_transport_request(fake, n - 1)->body;
            if (b != NULL) {
                snprintf(body_buf, body_cap, "%s", (const char *)b);
            }
        }
    }

    fclose(out);
    fclose(errf);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
    return ret;
}

/* -------------------------------------------------------------------------- */

/* Explicit --mode is sent verbatim; kid printed; login + create = 3 requests. */
static void test_gen_explicit_mode(void **state)
{
    (void)state;
    char out[128], body[256];
    size_t requests = 0;

    assert_int_equal(run_gen("SECP256R1", "k", NULL, "ExDSA",
                             EJWT_FX_PASSPHRASE, 200,
                             "{\"kid\":\"" NEW_KID "\"}",
                             out, sizeof out, NULL, 0, &requests,
                             body, sizeof body),
                     HEM_KEYS_OK);
    assert_int_equal((int)requests, 3);
    assert_string_equal(body,
        "{\"type\":\"SECP256R1\",\"label\":\"k\",\"mode\":\"ExDSA\"}");
    assert_non_null(strstr(out, NEW_KID));
}

/* NIST-P/K without --mode → tool injects "ECDH,ExDSA" and notes it on err. */
static void test_gen_nist_default_mode(void **state)
{
    (void)state;
    char out[128], err[256], body[256];

    assert_int_equal(run_gen("SECP256R1", "k", NULL, NULL,
                             EJWT_FX_PASSPHRASE, 200,
                             "{\"kid\":\"" NEW_KID "\"}",
                             out, sizeof out, err, sizeof err, NULL,
                             body, sizeof body),
                     HEM_KEYS_OK);
    assert_string_equal(body,
        "{\"type\":\"SECP256R1\",\"label\":\"k\",\"mode\":\"ECDH,ExDSA\"}");
    assert_non_null(strstr(err, "ECDH,ExDSA"));
    assert_non_null(strstr(out, NEW_KID));
}

/* A non-NIST type without --mode sends no mode field at all. */
static void test_gen_non_nist_no_mode(void **state)
{
    (void)state;
    char out[128], err[256], body[256];

    assert_int_equal(run_gen("AES256", "k", NULL, NULL,
                             EJWT_FX_PASSPHRASE, 200,
                             "{\"kid\":\"" NEW_KID "\"}",
                             out, sizeof out, err, sizeof err, NULL,
                             body, sizeof body),
                     HEM_KEYS_OK);
    assert_string_equal(body,
        "{\"type\":\"AES256\",\"label\":\"k\"}");
    /* no default-mode note for a symmetric key */
    assert_null(strstr(err, "ECDH,ExDSA"));
    assert_non_null(strstr(out, NEW_KID));
}

/* --descr's raw bytes are base64-encoded by the SDK ("abc" → "YWJj"). */
static void test_gen_with_descr(void **state)
{
    (void)state;
    char out[128], body[256];

    assert_int_equal(run_gen("ED25519", "k", "abc", NULL,
                             EJWT_FX_PASSPHRASE, 200,
                             "{\"kid\":\"" NEW_KID "\"}",
                             out, sizeof out, NULL, 0, NULL,
                             body, sizeof body),
                     HEM_KEYS_OK);
    assert_string_equal(body,
        "{\"type\":\"ED25519\",\"label\":\"k\",\"descr\":\"YWJj\"}");
}

/* Usage errors → exit 2 with ZERO transport traffic. */
static void test_gen_usage_errors(void **state)
{
    (void)state;
    size_t requests = 99;

    /* missing type */
    assert_int_equal(run_gen(NULL, "k", NULL, NULL, EJWT_FX_PASSPHRASE,
                             0, NULL, NULL, 0, NULL, 0, &requests, NULL, 0),
                     HEM_KEYS_USAGE);
    assert_int_equal((int)requests, 0);

    /* missing label */
    requests = 99;
    assert_int_equal(run_gen("ED25519", NULL, NULL, NULL, EJWT_FX_PASSPHRASE,
                             0, NULL, NULL, 0, NULL, 0, &requests, NULL, 0),
                     HEM_KEYS_USAGE);
    assert_int_equal((int)requests, 0);

    /* missing passphrase */
    requests = 99;
    assert_int_equal(run_gen("ED25519", "k", NULL, NULL, NULL,
                             0, NULL, NULL, 0, NULL, 0, &requests, NULL, 0),
                     HEM_KEYS_USAGE);
    assert_int_equal((int)requests, 0);

    /* bad --mode literal */
    requests = 99;
    assert_int_equal(run_gen("SECP256R1", "k", NULL, "ExDSA,ECDH",
                             EJWT_FX_PASSPHRASE,
                             0, NULL, NULL, 0, NULL, 0, &requests, NULL, 0),
                     HEM_KEYS_USAGE);
    assert_int_equal((int)requests, 0);
}

/* SDK client-side validation (label > 32 bytes) → exit 2, no create request. */
static void test_gen_sdk_arg_error(void **state)
{
    (void)state;
    size_t requests = 99;
    char err[256];

    /* 33-char label is rejected by ehem_key_create before any transport. */
    assert_int_equal(run_gen("ED25519",
                             "012345678901234567890123456789012", NULL, NULL,
                             EJWT_FX_PASSPHRASE, 0, NULL,
                             NULL, 0, err, sizeof err, &requests, NULL, 0),
                     HEM_KEYS_USAGE);
    assert_int_equal((int)requests, 0);
    assert_non_null(strstr(err, "label"));
}

/* Device 400 (unsupported type) and 406 (repo failure) → exit 1. */
static void test_gen_device_errors(void **state)
{
    (void)state;

    assert_int_equal(run_gen("NOPE", "k", NULL, NULL, EJWT_FX_PASSPHRASE,
                             400, "{\"error\":\"unsupported type\"}",
                             NULL, 0, NULL, 0, NULL, NULL, 0),
                     HEM_KEYS_RUNTIME);

    assert_int_equal(run_gen("ED25519", "k", NULL, NULL, EJWT_FX_PASSPHRASE,
                             406, NULL, NULL, 0, NULL, 0, NULL, NULL, 0),
                     HEM_KEYS_RUNTIME);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_gen_explicit_mode),
        cmocka_unit_test(test_gen_nist_default_mode),
        cmocka_unit_test(test_gen_non_nist_no_mode),
        cmocka_unit_test(test_gen_with_descr),
        cmocka_unit_test(test_gen_usage_errors),
        cmocka_unit_test(test_gen_sdk_arg_error),
        cmocka_unit_test(test_gen_device_errors),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
