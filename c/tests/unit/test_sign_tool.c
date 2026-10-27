/*
 * test_sign_tool.c — hem-tool `sign` (hem-tool-core hem_sign_run) driven
 * offline through the fake transport.
 *
 * verifies: REQ-TOOL-008 (message from --in FILE and from the stdin FILE
 *           signs and prints base64/hex/raw; explicit --alg skips the
 *           key-type fetch [request-sequence assert]; omitted --alg fetches
 *           the type once and picks the documented default per family;
 *           non-signing family → exit 1 naming the type with no sign
 *           request; empty/oversized message, unreadable --in, oversized
 *           --sigctx, missing kid/passphrase → exit 2 with no sign request;
 *           device 403/406 → exit 1)
 *
 * Output captured via portable tmpfile(); login fixtures mirror test_keys.c.
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
#include "ehem/crypto.h"
#include "sign.h"
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
    m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"st\"}",
                 (long long)(EJWT_FX_NOW + 100000));
    sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    snprintf(resp, sizeof resp, "{\"token\":\"hdr.%s.sig\"}", seg);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, resp), 0);
}

/* Write `data` into a fresh tmpfile positioned at the start. */
static FILE *msg_file(const void *data, size_t len)
{
    FILE *f = tmpfile();
    assert_non_null(f);
    if (len > 0) {
        assert_int_equal(fwrite(data, 1, len, f), len);
    }
    rewind(f);
    return f;
}

static size_t slurp(FILE *f, char *buf, size_t cap)
{
    size_t n;
    rewind(f);
    n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    return n;
}

#define TEST_KID "09bd0958e1499ecfd51ea62a3f49a84c"

/* "BAULDA==" decodes to {0x04,0x05,0x0b,0x0c}. */
static const char SIGN_RESP[] = "{\"sign\":\"BAULDA==\"}";

typedef struct run_result {
    int    ret;
    char   out[512];
    size_t out_len;
    char   err[512];
    size_t requests;
    char   last_body[512];   /* body of the LAST captured request ("" if none) */
} run_result;

/*
 * Run hem_sign_run with a scripted transport. `responses` are pushed in
 * order after the login pair (when with_login). The message comes from a
 * tmpfile-backed `o->in` unless in_path is given.
 */
static void run_sign(run_result *r, int with_login,
                     const char *const *responses, size_t response_count,
                     const char *kid, const char *alg, const char *passphrase,
                     const void *msg, size_t msg_len,
                     const char *in_path, const char *sigctx,
                     hem_sign_format format)
{
    ehem_transport *fake = fake_transport_new();
    FILE *out = tmpfile();
    FILE *errf = tmpfile();
    FILE *in = (in_path == NULL) ? msg_file(msg, msg_len) : NULL;
    hem_sign_opts o;
    size_t i;

    assert_non_null(fake);
    assert_non_null(out);
    assert_non_null(errf);
    set_now(EJWT_FX_NOW);
    if (with_login) {
        push_login(fake);
    }
    for (i = 0; i < response_count; i++) {
        assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                      responses[i]), 0);
    }

    ehem_ctx *ctx = ctx_with(fake);
    memset(&o, 0, sizeof o);
    o.passphrase = passphrase;
    o.kid = kid;
    o.alg = alg;
    o.in_path = in_path;
    o.sigctx = sigctx;
    o.format = format;
    o.out = out;
    o.err = errf;
    o.in = in;

    memset(r, 0, sizeof *r);
    r->ret = hem_sign_run(ctx, &o);
    r->out_len = slurp(out, r->out, sizeof r->out);
    slurp(errf, r->err, sizeof r->err);
    r->requests = fake_transport_request_count(fake);
    r->last_body[0] = '\0';
    if (r->requests > 0) {
        const fake_captured_request *req =
            fake_transport_request(fake, r->requests - 1);
        if (req != NULL && req->body != NULL) {
            snprintf(r->last_body, sizeof r->last_body, "%s",
                     (const char *)req->body);
        }
    }

    fclose(out);
    fclose(errf);
    if (in != NULL) {
        fclose(in);
    }
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* -------------------------------------------------------------------------- */

static void test_sign_explicit_alg_formats(void **state)
{
    (void)state;
    run_result r;
    const char *resp[] = { SIGN_RESP };
    const uint8_t msg[3] = { 1, 2, 3 };          /* b64 "AQID" */

    /* Base64 (default): signature line + trailing newline; NO get request —
     * login (2) + sign (1) only. */
    run_sign(&r, 1, resp, 1, TEST_KID, "Ed25519", EJWT_FX_PASSPHRASE,
             msg, sizeof msg, NULL, NULL, HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_OK);
    assert_string_equal(r.out, "BAULDA==\n");
    assert_int_equal((int)r.requests, 3);
    assert_string_equal(r.last_body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"AQID\","
                        "\"alg\":\"Ed25519\"}");

    /* Hex. */
    run_sign(&r, 1, resp, 1, TEST_KID, "Ed25519", EJWT_FX_PASSPHRASE,
             msg, sizeof msg, NULL, NULL, HEM_SIGN_OUT_HEX);
    assert_int_equal(r.ret, HEM_SIGN_OK);
    assert_string_equal(r.out, "04050b0c\n");

    /* Raw: exactly the signature bytes. */
    run_sign(&r, 1, resp, 1, TEST_KID, "Ed25519", EJWT_FX_PASSPHRASE,
             msg, sizeof msg, NULL, NULL, HEM_SIGN_OUT_RAW);
    assert_int_equal(r.ret, HEM_SIGN_OK);
    assert_int_equal((int)r.out_len, 4);
    assert_memory_equal(r.out, "\x04\x05\x0b\x0c", 4);
}

static void test_sign_sigctx_in_body(void **state)
{
    (void)state;
    run_result r;
    const char *resp[] = { SIGN_RESP };
    const uint8_t msg[1] = { 0x72 };             /* b64 "cg==" */

    run_sign(&r, 1, resp, 1, TEST_KID, "Ed25519ctx", EJWT_FX_PASSPHRASE,
             msg, sizeof msg, NULL, "fo", HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_OK);
    assert_string_equal(r.last_body,
                        "{\"kid\":\"" TEST_KID "\",\"msg\":\"cg==\","
                        "\"alg\":\"Ed25519ctx\",\"ctx\":\"Zm8=\"}");
}

static void test_sign_default_alg_lookup(void **state)
{
    (void)state;
    run_result r;
    const uint8_t msg[3] = { 1, 2, 3 };

    /* ED25519 → Ed25519; the get rides the SAME token (login 2 + get + sign
     * = 4 requests, no second login). */
    const char *ed[] = { "{\"type\":\"ED25519\",\"pubkey\":\"BAULDA==\","
                         "\"updated\":1}", SIGN_RESP };
    run_sign(&r, 1, ed, 2, TEST_KID, NULL, EJWT_FX_PASSPHRASE,
             msg, sizeof msg, NULL, NULL, HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_OK);
    assert_int_equal((int)r.requests, 4);
    assert_non_null(strstr(r.last_body, "\"alg\":\"Ed25519\""));
    assert_non_null(strstr(r.err, "note: using Ed25519"));

    /* SECP256R1 → SHA256WithECDSA (bare get-form type string). */
    const char *p256[] = { "{\"type\":\"SECP256R1\",\"pubkey\":\"BAULDA==\","
                           "\"updated\":1}", SIGN_RESP };
    run_sign(&r, 1, p256, 2, TEST_KID, NULL, EJWT_FX_PASSPHRASE,
             msg, sizeof msg, NULL, NULL, HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_OK);
    assert_non_null(strstr(r.last_body, "\"alg\":\"SHA256WithECDSA\""));
}

static void test_sign_non_signing_family(void **state)
{
    (void)state;
    run_result r;
    const uint8_t msg[3] = { 1, 2, 3 };
    const char *x255[] = { "{\"type\":\"CURVE25519\",\"pubkey\":\"BAULDA==\","
                           "\"updated\":1}" };

    run_sign(&r, 1, x255, 1, TEST_KID, NULL, EJWT_FX_PASSPHRASE,
             msg, sizeof msg, NULL, NULL, HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_RUNTIME);
    /* Names the type; issued NO sign request (login 2 + get = 3). */
    assert_non_null(strstr(r.err, "'CURVE25519'"));
    assert_int_equal((int)r.requests, 3);
}

static void test_sign_message_sources(void **state)
{
    (void)state;
    run_result r;
    const char *resp[] = { SIGN_RESP };
    char path[64];

    /* --in FILE: binary content, incl. a NUL byte. */
    snprintf(path, sizeof path, "test_sign_tool_msg.tmp");
    FILE *f = fopen(path, "wb");
    assert_non_null(f);
    assert_int_equal((int)fwrite("a\0b", 1, 3, f), 3);
    fclose(f);

    run_sign(&r, 1, resp, 1, TEST_KID, "Ed25519", EJWT_FX_PASSPHRASE,
             NULL, 0, path, NULL, HEM_SIGN_OUT_B64);
    remove(path);
    assert_int_equal(r.ret, HEM_SIGN_OK);
    assert_non_null(strstr(r.last_body, "\"msg\":\"YQBi\""));  /* b64("a\0b") */

    /* Unreadable --in → usage, no traffic at all. */
    run_sign(&r, 0, NULL, 0, TEST_KID, "Ed25519", EJWT_FX_PASSPHRASE,
             NULL, 0, "definitely/not/a/file.bin", NULL, HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_USAGE);
    assert_int_equal((int)r.requests, 0);
}

static void test_sign_usage_errors(void **state)
{
    (void)state;
    run_result r;
    static uint8_t big[EHEM_SIGN_MSG_MAX + 1];
    static char big_ctx[EHEM_SIGN_SIG_CTX_MAX + 2];
    const uint8_t msg[1] = { 0x41 };

    /* Missing passphrase / kid / malformed kid. */
    run_sign(&r, 0, NULL, 0, TEST_KID, "Ed25519", NULL, msg, 1, NULL, NULL,
             HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_USAGE);
    run_sign(&r, 0, NULL, 0, NULL, "Ed25519", EJWT_FX_PASSPHRASE, msg, 1,
             NULL, NULL, HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_USAGE);
    run_sign(&r, 0, NULL, 0, "nope", "Ed25519", EJWT_FX_PASSPHRASE, msg, 1,
             NULL, NULL, HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_USAGE);

    /* Empty and oversized message. */
    run_sign(&r, 0, NULL, 0, TEST_KID, "Ed25519", EJWT_FX_PASSPHRASE,
             NULL, 0, NULL, NULL, HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_USAGE);
    memset(big, 0x41, sizeof big);
    run_sign(&r, 0, NULL, 0, TEST_KID, "Ed25519", EJWT_FX_PASSPHRASE,
             big, sizeof big, NULL, NULL, HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_USAGE);

    /* Oversized --sigctx. */
    memset(big_ctx, 'c', sizeof big_ctx - 1);
    big_ctx[sizeof big_ctx - 1] = '\0';
    run_sign(&r, 0, NULL, 0, TEST_KID, "Ed25519ctx", EJWT_FX_PASSPHRASE,
             msg, 1, NULL, big_ctx, HEM_SIGN_OUT_B64);
    assert_int_equal(r.ret, HEM_SIGN_USAGE);

    /* None of the above reached the transport. */
    assert_int_equal((int)r.requests, 0);
}

static void test_sign_device_errors(void **state)
{
    (void)state;
    const uint8_t msg[1] = { 0x41 };
    run_result r;

    /* 403 (wrong scope / sub M) → runtime failure. */
    {
        ehem_transport *fake = fake_transport_new();
        FILE *out = tmpfile(), *errf = tmpfile(), *in = msg_file(msg, 1);
        hem_sign_opts o;
        assert_non_null(fake);
        set_now(EJWT_FX_NOW);
        push_login(fake);
        assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 403,
                                                      "{\"e\":1}"), 0);
        ehem_ctx *ctx = ctx_with(fake);
        memset(&o, 0, sizeof o);
        o.passphrase = EJWT_FX_PASSPHRASE;
        o.kid = TEST_KID;
        o.alg = "Ed25519";
        o.format = HEM_SIGN_OUT_B64;
        o.out = out; o.err = errf; o.in = in;
        r.ret = hem_sign_run(ctx, &o);
        assert_int_equal(r.ret, HEM_SIGN_RUNTIME);
        slurp(errf, r.err, sizeof r.err);
        assert_non_null(strstr(r.err, "EHEM_ERR_SCOPE_DENIED"));
        fclose(out); fclose(errf); fclose(in);
        ehem_ctx_destroy(ctx);
        fake_transport_free(fake);
    }

    /* 406 (kid not found / wrong key type / crypto failure) → runtime. */
    {
        ehem_transport *fake = fake_transport_new();
        FILE *out = tmpfile(), *errf = tmpfile(), *in = msg_file(msg, 1);
        hem_sign_opts o;
        assert_non_null(fake);
        set_now(EJWT_FX_NOW);
        push_login(fake);
        assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406,
                                                      NULL), 0);
        ehem_ctx *ctx = ctx_with(fake);
        memset(&o, 0, sizeof o);
        o.passphrase = EJWT_FX_PASSPHRASE;
        o.kid = TEST_KID;
        o.alg = "Ed25519";
        o.format = HEM_SIGN_OUT_B64;
        o.out = out; o.err = errf; o.in = in;
        r.ret = hem_sign_run(ctx, &o);
        assert_int_equal(r.ret, HEM_SIGN_RUNTIME);
        fclose(out); fclose(errf); fclose(in);
        ehem_ctx_destroy(ctx);
        fake_transport_free(fake);
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sign_explicit_alg_formats),
        cmocka_unit_test(test_sign_sigctx_in_body),
        cmocka_unit_test(test_sign_default_alg_lookup),
        cmocka_unit_test(test_sign_non_signing_family),
        cmocka_unit_test(test_sign_message_sources),
        cmocka_unit_test(test_sign_usage_errors),
        cmocka_unit_test(test_sign_device_errors),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
