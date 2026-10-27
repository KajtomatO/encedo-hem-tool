/*
 * test_keys_pub.c — hem-tool `keys pub` (hem-tool-core hem_keys_pub_run)
 * driven offline through the fake transport.
 *
 * verifies: REQ-TOOL-007 (asymmetric key prints type + classification +
 *           base64 material; --hex switches encoding; --raw emits exactly
 *           the material bytes and nothing else; CERT/DER_PKEY prints der;
 *           symmetric prints "no material" and exits 0; malformed kid /
 *           missing passphrase → exit 2 with NO transport traffic; 406 →
 *           exit 1 naming the kid; read-only — auth + get only)
 *
 * Output captured via portable tmpfile() (the M2-070 MinGW lesson); login
 * fixtures mirror test_keys.c.
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

/* Read a captured FILE back into buf (NUL-terminated); returns length. */
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
static const char GET_ED[] =
    "{\"type\":\"ED25519\",\"pubkey\":\"BAULDA==\",\"updated\":1717000000}";
static const char GET_CERT[] = "{\"type\":\"CERT\",\"der\":\"BAULDA==\"}";
static const char GET_AES[] = "{\"type\":\"AES256\",\"updated\":5}";
static const char GET_X25519_FLAGS[] =
    "{\"type\":\"ECDH,CURVE25519\",\"pubkey\":\"BAULDA==\"}";

/* Run hem_keys_pub_run against one scripted get response; returns exit code,
 * captures out/err. */
static int run_pub(const char *get_resp, hem_keys_pub_format format,
                   const char *kid, const char *passphrase,
                   char *out_buf, size_t out_cap,
                   size_t *out_len, size_t *request_count)
{
    ehem_transport *fake = fake_transport_new();
    FILE *out = tmpfile();
    FILE *errf = tmpfile();
    hem_keys_pub_opts o;
    int ret;

    assert_non_null(fake);
    assert_non_null(out);
    assert_non_null(errf);
    set_now(EJWT_FX_NOW);
    if (get_resp != NULL) {
        push_login(fake);
        assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                      get_resp), 0);
    }

    ehem_ctx *ctx = ctx_with(fake);
    memset(&o, 0, sizeof o);
    o.passphrase = passphrase;
    o.kid = kid;
    o.format = format;
    o.out = out;
    o.err = errf;

    ret = hem_keys_pub_run(ctx, &o);
    if (out_buf != NULL) {
        size_t n = slurp(out, out_buf, out_cap);
        if (out_len != NULL) {
            *out_len = n;
        }
    }
    if (request_count != NULL) {
        *request_count = fake_transport_request_count(fake);
    }

    fclose(out);
    fclose(errf);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
    return ret;
}

/* -------------------------------------------------------------------------- */

static void test_pub_asymmetric_b64(void **state)
{
    (void)state;
    char out[1024];
    size_t requests = 0;

    assert_int_equal(run_pub(GET_ED, HEM_KEYS_PUB_B64, TEST_KID,
                             EJWT_FX_PASSPHRASE, out, sizeof out, NULL,
                             &requests), HEM_KEYS_OK);
    assert_non_null(strstr(out, "kid:      " TEST_KID));
    assert_non_null(strstr(out, "type:     ED25519"));
    assert_non_null(strstr(out, "family:   ED25519"));
    assert_non_null(strstr(out, "updated:  1717000000"));
    assert_non_null(strstr(out, "pubkey:   BAULDA=="));
    /* Read-only: challenge GET + token POST + get GET, nothing else. */
    assert_int_equal((int)requests, 3);
}

static void test_pub_hex_and_flag_set(void **state)
{
    (void)state;
    char out[1024];

    assert_int_equal(run_pub(GET_X25519_FLAGS, HEM_KEYS_PUB_HEX, TEST_KID,
                             EJWT_FX_PASSPHRASE, out, sizeof out, NULL, NULL),
                     HEM_KEYS_OK);
    assert_non_null(strstr(out, "type:     ECDH,CURVE25519"));
    assert_non_null(strstr(out, "family:   CURVE25519"));
    assert_non_null(strstr(out, "modes:    ECDH"));
    assert_non_null(strstr(out, "pubkey:   04050b0c"));
}

static void test_pub_raw_bytes_only(void **state)
{
    (void)state;
    char out[64];
    size_t n = 0;
    static const uint8_t raw[4] = { 0x04, 0x05, 0x0b, 0x0c };

    assert_int_equal(run_pub(GET_ED, HEM_KEYS_PUB_RAW, TEST_KID,
                             EJWT_FX_PASSPHRASE, out, sizeof out, &n, NULL),
                     HEM_KEYS_OK);
    /* Exactly the material bytes — no prose, no trailing newline. */
    assert_int_equal((int)n, 4);
    assert_memory_equal(out, raw, 4);
}

static void test_pub_cert_der(void **state)
{
    (void)state;
    char out[1024];

    assert_int_equal(run_pub(GET_CERT, HEM_KEYS_PUB_B64, TEST_KID,
                             EJWT_FX_PASSPHRASE, out, sizeof out, NULL, NULL),
                     HEM_KEYS_OK);
    assert_non_null(strstr(out, "family:   CERT"));
    assert_non_null(strstr(out, "der:      BAULDA=="));
}

static void test_pub_symmetric_no_material(void **state)
{
    (void)state;
    char out[1024];
    size_t n = 0;

    assert_int_equal(run_pub(GET_AES, HEM_KEYS_PUB_B64, TEST_KID,
                             EJWT_FX_PASSPHRASE, out, sizeof out, NULL, NULL),
                     HEM_KEYS_OK);
    assert_non_null(strstr(out, "family:   AES256"));
    assert_non_null(strstr(out, "material: (none"));

    /* Raw mode with no material: empty stdout, still exit 0. */
    assert_int_equal(run_pub(GET_AES, HEM_KEYS_PUB_RAW, TEST_KID,
                             EJWT_FX_PASSPHRASE, out, sizeof out, &n, NULL),
                     HEM_KEYS_OK);
    assert_int_equal((int)n, 0);
}

static void test_pub_usage_errors_no_io(void **state)
{
    (void)state;
    size_t requests = 99;

    /* Missing passphrase → usage, no traffic. */
    assert_int_equal(run_pub(NULL, HEM_KEYS_PUB_B64, TEST_KID, NULL,
                             NULL, 0, NULL, &requests), HEM_KEYS_USAGE);
    assert_int_equal((int)requests, 0);

    /* Missing / malformed kid → usage, no traffic. */
    assert_int_equal(run_pub(NULL, HEM_KEYS_PUB_B64, NULL, EJWT_FX_PASSPHRASE,
                             NULL, 0, NULL, &requests), HEM_KEYS_USAGE);
    assert_int_equal((int)requests, 0);
    assert_int_equal(run_pub(NULL, HEM_KEYS_PUB_B64, "definitely-not-a-kid",
                             EJWT_FX_PASSPHRASE, NULL, 0, NULL, &requests),
                     HEM_KEYS_USAGE);
    assert_int_equal((int)requests, 0);
}

static void test_pub_not_found(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    FILE *out = tmpfile();
    FILE *errf = tmpfile();
    hem_keys_pub_opts o;
    char err_buf[512];

    assert_non_null(fake);
    assert_non_null(out);
    assert_non_null(errf);
    set_now(EJWT_FX_NOW);
    push_login(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406, NULL), 0);

    ehem_ctx *ctx = ctx_with(fake);
    memset(&o, 0, sizeof o);
    o.passphrase = EJWT_FX_PASSPHRASE;
    o.kid = TEST_KID;
    o.out = out;
    o.err = errf;

    assert_int_equal(hem_keys_pub_run(ctx, &o), HEM_KEYS_RUNTIME);
    slurp(errf, err_buf, sizeof err_buf);
    assert_non_null(strstr(err_buf, "key not found: " TEST_KID));

    fclose(out);
    fclose(errf);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pub_asymmetric_b64),
        cmocka_unit_test(test_pub_hex_and_flag_set),
        cmocka_unit_test(test_pub_raw_bytes_only),
        cmocka_unit_test(test_pub_cert_der),
        cmocka_unit_test(test_pub_symmetric_no_material),
        cmocka_unit_test(test_pub_usage_errors_no_io),
        cmocka_unit_test(test_pub_not_found),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
