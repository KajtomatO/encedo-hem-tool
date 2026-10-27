/*
 * test_ext_tool.c — hem-tool `ext` family (REQ-TOOL-016) through
 * hem-tool-core with the fake transport; QR rendering against the
 * qrcodegen reference behavior.
 *
 * verifies: REQ-TOOL-016 (pair: leg order, verbatim {kid,code} finalise
 *           pass-through, exit codes for scan-timeout and device-406;
 *           list: EXTAID filter, [PROTECTED] marking, pid printing;
 *           login: approved/rejected/timeout/no-pairing exit codes; QR:
 *           deterministic module matrix — line count and all-light quiet
 *           rows for a known payload; usage errors with zero traffic;
 *           the --no-qr print is the SDK's ehem_ext_qr_payload verbatim —
 *           hash = base64(SHA-256(request)), the Manager's key order)
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
#include "ext_cmd.h"
#include "proto_auth.h"       /* internal: clock/KDF seams */
#include "proto_ext.h"        /* internal: poll-interval seam */
#include "ejwt.h"
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":2000000000,\"lbl\":\"alice\"}"
#define B64_32 "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
#define KID_HEX "00112233445566778899aabbccddeeff"
#define CONFIG_JSON \
    "{\"devid\":\"d\",\"hostname\":\"hem.example\",\"user\":\"Alice\"," \
    "\"email\":\"a@example.com\",\"eid\":\"" B64_32 "\"}"

static const char CI_CHALLENGE[] = "{\"check\":\"CHALLENGE-BLOB\"}";
static const char CI_VERIFIED[]  = "{\"checked\":\"CLOUD-VERIFIED-BLOB\"}";
static const char CI_OK[]        = "{\"status\":\"ok\",\"newcrt\":\"\"}";

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

/* Read a whole tmpfile back as a NUL-terminated heap string. */
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

/* --- QR rendering ---------------------------------------------------------- */
static void test_qr_render(void **state)
{
    (void)state;
    FILE *f = tmpfile();
    assert_non_null(f);

    /* "HELLO" fits QR v1 (21 modules) at ECC LOW → 21+8 quiet rows over
     * 2-row lines = 15 lines (last line's bottom half is quiet). A tmpfile
     * takes the UTF-8 byte path on every platform; the Windows console path
     * (the same rows as UTF-16 via WriteConsoleW) needs a console and is
     * covered by the attended run (REQ-TOOL-016 rev 4). */
    assert_int_equal(hem_ext_qr_render("HELLO", f), 0);
    char *s = slurp(f);
    int lines = 0;
    for (const char *p = s; *p; p++) {
        lines += (*p == '\n');
    }
    assert_int_equal(lines, 15);

    /* The first line covers rows -4/-3 — all quiet (light) → all full
     * blocks in the inverted rendering: 29 cells × 3-byte "█". */
    const char *nl = strchr(s, '\n');
    assert_non_null(nl);
    assert_int_equal((int)(nl - s), 29 * 3);
    for (const char *p = s; p < nl; p += 3) {
        assert_memory_equal(p, "\xe2\x96\x88", 3);
    }
    free(s);
    fclose(f);

    assert_int_equal(hem_ext_qr_render(NULL, stdout), -1);

    /* Width fallback: a payload too wide for 80 columns is refused so the
     * caller prints it instead (73+ modules → 81+ cols with quiet zone). */
    {
        char big[1200];
        memset(big, 'a', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        FILE *g = tmpfile();
        assert_non_null(g);
        assert_int_equal(hem_ext_qr_render(big, g), -1);
        fclose(g);
    }
}

/* --- ext pair --------------------------------------------------------------- */

/* Script everything up to (excluding) the first register/check. */
static void push_pair_setup(ehem_transport *fake)
{
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CI_CHALLENGE), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CI_VERIFIED), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, CI_OK),
                     0);
    push_login(fake, "pair");                     /* scope system:config */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
                                                  CONFIG_JSON), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"epk\":\"" B64_32 "\"}"), 0);
    push_login(fake, "pair2");                    /* scope auth:ext:pair */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"request\":\"r.j.w\",\"eid\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"rid\":\"RID-1\",\"link\":\"https://l.ink/x\"}"), 0);
}

static void test_pair_happy_timeout_refused(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake, 0);
    FILE *out = tmpfile();
    assert_non_null(out);

    hem_ext_pair_opts po;
    memset(&po, 0, sizeof po);
    po.passphrase    = EJWT_FX_PASSPHRASE;
    po.no_qr         = 1;
    po.poll_attempts = 2;
    po.poll_delay_ms = 0;
    po.out           = out;
    po.err           = out;

    /* Usage: no passphrase, zero traffic. */
    hem_ext_pair_opts bad = po;
    bad.passphrase = NULL;
    assert_int_equal(hem_ext_pair_run(ctx, &bad), HEM_EXT_USAGE);
    assert_int_equal(fake_transport_request_count(fake), 0);

    /* Happy: pending once, then the phone answers; validate + finalise. */
    push_pair_setup(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202, NULL), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"pid\":\"" B64_32 "\",\"reply\":\"re.p.ly\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"kid\":\"" KID_HEX "\",\"code\":\"Q09ERQ==\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, "{}"), 0);
    assert_int_equal(hem_ext_pair_run(ctx, &po), HEM_EXT_OK);

    size_t n = fake_transport_request_count(fake);
    const fake_captured_request *fin = fake_transport_request(fake, n - 1);
    assert_string_equal(fin->path,
                        EHEM_DEFAULT_NOTIFY_URL "/register/finalise/RID-1");
    assert_string_equal((const char *)fin->body,
        "{\"kid\":\"" KID_HEX "\",\"code\":\"Q09ERQ==\"}");
    char *printed = slurp(out);
    /* --no-qr prints the payload the phone scans: the SDK's
     * ehem_ext_qr_payload over the scripted "r.j.w" request and CONFIG_JSON —
     * the request passed VERBATIM (hash = base64(SHA-256("r.j.w"))), the
     * Manager's key order, nothing else on the line. */
    assert_non_null(strstr(printed,
        "\n{\"link\":\"https://l.ink/x\","
        "\"hash\":\"BQixETmS1vjcSjAPqjT0yq9poZjfeTnWczNU6VHYuI4=\","
        "\"user\":\"Alice\",\"email\":\"a@example.com\","
        "\"hostname\":\"hem.example\"}\n"));
    assert_null(strstr(printed, "not_implemented_yet"));
    assert_non_null(strstr(printed, "Paired. kid: " KID_HEX));
    free(printed);

    /* Scan timeout: attempts × 202. */
    push_pair_setup(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202, NULL), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202, NULL), 0);
    assert_int_equal(hem_ext_pair_run(ctx, &po), HEM_EXT_TIMEOUT);

    /* Device refuses the import (slots/dedup 406). */
    push_pair_setup(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"pid\":\"" B64_32 "\",\"reply\":\"re.p.ly\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 406, NULL), 0);
    assert_int_equal(hem_ext_pair_run(ctx, &po), HEM_EXT_REFUSED);

    fclose(out);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* --- ext list --------------------------------------------------------------- */
static void test_list(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake, 0);
    FILE *out = tmpfile();
    assert_non_null(out);

    /* descr = b64("EXTAID" + 32 bytes) for both entries. */
    uint8_t raw[38];
    char descr_b64[64];
    memcpy(raw, "EXTAID", 6);
    for (size_t i = 0; i < 32; i++) {
        raw[6 + i] = (uint8_t)i;
    }
    assert_int_not_equal(ehem_b64_std_encode(raw, sizeof raw, descr_b64,
                                             sizeof descr_b64), (size_t)-1);

    push_login(fake, "list");
    char page[512];
    snprintf(page, sizeof page,
        "{\"offset\":0,\"total\":2,\"listed\":2,\"list\":["
        "{\"kid\":\"" KID_HEX "\",\"type\":\"ECDH,CURVE25519\","
        "\"label\":\"Tom's phone (iPhone)\",\"descr\":\"%s\"},"
        "{\"kid\":\"ffeeddccbbaa99887766554433221100\","
        "\"type\":\"ECDH,CURVE25519\",\"label\":\"EHEMTEST-sim\","
        "\"descr\":\"%s\"}]}", descr_b64, descr_b64);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, page), 0);

    assert_int_equal(hem_ext_list_run(ctx, EJWT_FX_PASSPHRASE, false, out, out),
                     HEM_EXT_OK);
    char *printed = slurp(out);
    assert_non_null(strstr(printed, KID_HEX));
    assert_non_null(strstr(printed, "[PROTECTED]"));       /* (iPhone) */
    assert_non_null(strstr(printed, "EHEMTEST-sim"));
    assert_non_null(strstr(printed, "2 paired authenticators"));
    free(printed);

    /* Usage without a passphrase. */
    assert_int_equal(hem_ext_list_run(ctx, NULL, false, out, out),
                     HEM_EXT_USAGE);

    fclose(out);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* --- ext login -------------------------------------------------------------- */

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

static void test_login_outcomes(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_ext_test_set_poll_interval(1);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    ehem_ctx *ctx = ctx_with(fake, 2);   /* 2 ms confirmation timeout */
    FILE *out = tmpfile();
    assert_non_null(out);

    hem_ext_login_opts lo;
    memset(&lo, 0, sizeof lo);
    lo.out = out;
    lo.err = out;

    /* Approved: full acquisition + the demo config GET. */
    push_mobile_acquisition(fake);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"devid\":\"d\",\"hostname\":\"hem.example\",\"user\":\"Alice\"}"),
        0);
    assert_int_equal(hem_ext_login_run(ctx, &lo), HEM_EXT_OK);
    char *printed = slurp(out);
    assert_non_null(strstr(printed, "Approved. hostname: hem.example"));
    free(printed);

    /* Rejected on the phone. */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreq\":\"a.b.c\",\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"eventid\":\"EV-2\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"deny\":true}"), 0);
    assert_int_equal(hem_ext_login_run(ctx, &lo), HEM_EXT_REFUSED);

    /* Unanswered (timeout 2 ms / interval 1 ms → 3 polls). */
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"authreq\":\"a.b.c\",\"epk\":\"" B64_32 "\"}"), 0);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"eventid\":\"EV-3\"}"), 0);
    for (int i = 0; i < 3; i++) {
        assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 202,
                                                      NULL), 0);
    }
    assert_int_equal(hem_ext_login_run(ctx, &lo), HEM_EXT_TIMEOUT);

    /* No pairing (passphrase pre-check): empty search page → NOAUTH. */
    push_login(fake, "pre");
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200,
        "{\"offset\":0,\"total\":0,\"listed\":0,\"list\":[]}"), 0);
    lo.passphrase = EJWT_FX_PASSPHRASE;
    size_t before = fake_transport_request_count(fake);
    assert_int_equal(hem_ext_login_run(ctx, &lo), HEM_EXT_NOAUTH);
    assert_int_equal(fake_transport_request_count(fake), before + 3);

    fclose(out);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_qr_render),
        cmocka_unit_test(test_pair_happy_timeout_refused),
        cmocka_unit_test(test_list),
        cmocka_unit_test(test_login_outcomes),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
