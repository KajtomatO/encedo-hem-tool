/*
 * test_recovery.c — hem-tool `recovery` orchestration, driven offline through
 * three fake transports (the three TLS postures on one device).
 *
 * verifies: REQ-TOOL-023 (healthy → one check-in, exit 0; expired cert +
 *           chain → one relaxed check-in, the cert-install sequence, a
 *           trusted verify, exit 0; expired + no chain → exit 3 with NO second
 *           check-in; https down + http reports https:false → the tls-recover
 *           sequence, exit 0; both down → exit 4; other TLS failure with http
 *           reporting https:true → exit 5 and nothing written; --mobile and a
 *           missing passphrase → exit 2 with zero traffic; relies on the
 *           SDK's REQ-API-004 rev 2 - ehem_error.tls_expired is the public
 *           form of its REQ-NET-005 classification: set on the expired-cert
 *           probe, clear on other failures - verified in the SDK repository)
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
#include "ehem/system.h"
#include "recovery.h"
#include "recover.h"
#include "cert_install.h"
#include "proto_auth.h"
#include "ejwt.h"
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"
#include "fixtures/cert_fixture.h"

#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":1700000060,\"lbl\":\"alice\"}"

static const char CI_OK[]          = "{\"status\":\"ok\",\"newcrt\":\"cert refreshed\"}";
static const char STATUS_HTTPS_ON[] =
    "{\"ctx\":0,\"fls_state\":0,\"uptime\":9,\"temp\":35,\"https\":true}";
static const char STATUS_HTTPS_OFF[] =
    "{\"ctx\":0,\"fls_state\":0,\"uptime\":9,\"temp\":35,\"https\":false}";
static const char INSTALL_OK[] = "{\"updated\":true,\"reboot_required\":true}";

#define FAST_KDF_ITERS 1000

static int64_t g_now;
static int64_t test_now_fn(void) { return g_now; }

static void set_now(int64_t now)
{
    g_now = now;
    ehem_auth_test_set_clock(test_now_fn);
    ehem_auth_test_set_kdf_iters(FAST_KDF_ITERS);
}

static ehem_ctx *ctx_with(ehem_transport *fake, const char *url, int no_auto_checkin)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    opts.no_auto_checkin = no_auto_checkin;
    assert_int_equal(ehem_ctx_create(url, &opts, &ctx), EHEM_OK);
    return ctx;
}

static void push(ehem_transport *fake, int status, const char *body)
{
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, body), 0);
}

static void push_down(ehem_transport *fake)
{
    assert_int_equal(fake_transport_push_response(fake, EHEM_ERR_UNREACHABLE, 0, NULL), 0);
}

static void push_login(ehem_transport *fake)
{
    char payload[96], seg[160], resp[768];
    int m;
    size_t sn;
    push(fake, 200, CHALLENGE_JSON);
    m = snprintf(payload, sizeof payload, "{\"exp\":%lld}", (long long)(EJWT_FX_NOW + 100000));
    sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    snprintf(resp, sizeof resp, "{\"token\":\"hdr.%s.sig\"}", seg);
    push(fake, 200, resp);
}

static void push_checkin(ehem_transport *fake, const char *leg1, const char *leg2)
{
    push(fake, 200, leg1);
    push(fake, 200, leg2);
    push(fake, 200, CI_OK);
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

typedef struct {
    ehem_transport *f_https, *f_insecure, *f_http;
    ehem_ctx *https, *insecure, *http;
    FILE *out;
    hem_recovery_opts o;
} rig;

static void rig_open(rig *r)
{
    set_now(EJWT_FX_NOW);
    r->f_https = fake_transport_new();
    r->f_insecure = fake_transport_new();
    r->f_http = fake_transport_new();
    assert_non_null(r->f_https);
    assert_non_null(r->f_insecure);
    assert_non_null(r->f_http);
    r->https = ctx_with(r->f_https, "https://hem.local", 1);
    r->insecure = ctx_with(r->f_insecure, "https://hem.local", 0);
    r->http = ctx_with(r->f_http, "http://hem.local", 0);
    r->out = tmpfile();
    assert_non_null(r->out);
    memset(&r->o, 0, sizeof r->o);
    r->o.passphrase = EJWT_FX_PASSPHRASE;
    r->o.ctx_https = r->https;
    r->o.ctx_insecure = r->insecure;
    r->o.ctx_http = r->http;
    r->o.poll_attempts = 5;
    r->o.poll_delay_ms = 0;
    r->o.out = r->out;
    r->o.err = r->out;
}

static void rig_close(rig *r)
{
    fclose(r->out);
    ehem_ctx_destroy(r->http);
    ehem_ctx_destroy(r->insecure);
    ehem_ctx_destroy(r->https);
    fake_transport_free(r->f_http);
    fake_transport_free(r->f_insecure);
    fake_transport_free(r->f_https);
}

/* Case 1: status answers under trust → one check-in → exit 0. */
static void test_healthy(void **state)
{
    (void)state;
    rig r;
    rig_open(&r);
    push(r.f_https, 200, STATUS_HTTPS_ON);
    push_checkin(r.f_https, EHEM_FX_CHECK_CSN_MATCH, EHEM_FX_CHECKED_NO_NEWCRT);

    assert_int_equal(hem_recovery_run(&r.o), HEM_RECOVERY_OK);
    assert_int_equal((int)fake_transport_request_count(r.f_https), 4);
    assert_int_equal((int)fake_transport_request_count(r.f_insecure), 0);
    assert_int_equal((int)fake_transport_request_count(r.f_http), 0);
    char *t = slurp(r.out);
    assert_non_null(strstr(t, "healthy"));
    assert_non_null(strstr(t, "nothing to recover"));
    free(t);
    rig_close(&r);
}

/* Case 2 with a renewal: expired probe → ONE relaxed check-in (chain) →
 * cert-install's own sequence on the insecure context → trusted verify. */
static void test_expired_with_chain(void **state)
{
    (void)state;
    rig r;
    rig_open(&r);
    assert_int_equal(fake_transport_push_tls_expired(r.f_https, EHEM_ERR_NETWORK), 0); /* probe */
    push(r.f_https, 200, STATUS_HTTPS_ON);                                              /* verify */
    /* recovery's own check-in: */
    push_checkin(r.f_insecure, EHEM_FX_CHECK_CSN_OTHER, EHEM_FX_CHECKED_WITH_NEWCRT);
    /* cert-install: check-in, auth, install, reboot, poll (down, back) */
    push_checkin(r.f_insecure, EHEM_FX_CHECK_CSN_OTHER, EHEM_FX_CHECKED_WITH_NEWCRT);
    push_login(r.f_insecure);
    push(r.f_insecure, 200, INSTALL_OK);
    push(r.f_insecure, 200, NULL);
    push_down(r.f_insecure);
    push(r.f_insecure, 200, STATUS_HTTPS_ON);

    assert_int_equal(hem_recovery_run(&r.o), HEM_RECOVERY_OK);
    assert_int_equal((int)fake_transport_request_count(r.f_https), 2);
    assert_int_equal((int)fake_transport_request_count(r.f_insecure), 12);
    assert_int_equal((int)fake_transport_request_count(r.f_http), 0);
    assert_string_equal(fake_transport_request(r.f_insecure, 8)->path, "/api/system/config");
    assert_string_equal(fake_transport_request(r.f_insecure, 9)->path, "/api/system/reboot");
    char *t = slurp(r.out);
    assert_non_null(strstr(t, "EXPIRED"));
    assert_non_null(strstr(t, "renewal delivered"));
    assert_non_null(strstr(t, "recovered: the device serves a trusted certificate"));
    free(t);
    rig_close(&r);
}

/* Case 2 without a renewal: exit 3 after exactly ONE check-in, nothing
 * installed; the public flag was the classifier. */
static void test_expired_no_renewal(void **state)
{
    (void)state;
    rig r;
    rig_open(&r);
    assert_int_equal(fake_transport_push_tls_expired(r.f_https, EHEM_ERR_NETWORK), 0);
    push_checkin(r.f_insecure, EHEM_FX_CHECK_NO_CSN, EHEM_FX_CHECKED_NO_NEWCRT);
    push_checkin(r.f_insecure, EHEM_FX_CHECK_NO_CSN, EHEM_FX_CHECKED_NO_NEWCRT); /* must stay unused */

    assert_int_equal(hem_recovery_run(&r.o), HEM_RECOVERY_NO_RENEWAL);
    assert_int_equal((int)fake_transport_request_count(r.f_https), 1);
    assert_int_equal((int)fake_transport_request_count(r.f_insecure), 3);   /* one attempt */
    assert_true(ehem_last_error(r.https)->tls_expired);
    char *t = slurp(r.out);
    assert_non_null(strstr(t, "has not issued a renewal"));
    assert_non_null(strstr(t, "tried once"));
    free(t);
    rig_close(&r);
}

/* Case 3: https down, http says https:false → the tls-recover sequence. */
static void test_https_down_tls_recover(void **state)
{
    (void)state;
    rig r;
    rig_open(&r);
    push_down(r.f_https);
    push(r.f_http, 200, STATUS_HTTPS_OFF);                       /* diagnosis */
    /* tls-recover (force): check-in, login, attestation, cloud, install, reboot, poll */
    push_checkin(r.f_http, EHEM_FX_CHECK_NO_CSN, EHEM_FX_CHECKED_NO_NEWCRT);
    push_login(r.f_http);
    push(r.f_http, 200, "{\"crt\":\"x\",\"genuine\":\"g\"}");
    push(r.f_http, 200, "{\"crt\":\"Q1JU\",\"key\":\"K.C\",\"emp\":\"AAA\"}");
    push(r.f_http, 200, INSTALL_OK);
    push(r.f_http, 200, NULL);
    push(r.f_http, 200, STATUS_HTTPS_ON);

    assert_int_equal(hem_recovery_run(&r.o), HEM_RECOVERY_OK);
    assert_int_equal((int)fake_transport_request_count(r.f_https), 1);
    assert_false(ehem_last_error(r.https)->tls_expired);
    assert_int_equal((int)fake_transport_request_count(r.f_insecure), 0);
    /* diagnosis probe + check-in 3 + login 2 + attestation + cloud + install
     * + reboot + one poll = 11 */
    assert_int_equal((int)fake_transport_request_count(r.f_http), 11);
    char *t = slurp(r.out);
    assert_non_null(strstr(t, "HTTPS is down"));
    assert_non_null(strstr(t, "recovered: the device serves HTTPS again"));
    free(t);
    rig_close(&r);
}

/* Case 4: neither scheme answers → exit 4. Case 5: http says https:true →
 * exit 5, nothing written. */
static void test_unreachable_and_other_tls(void **state)
{
    (void)state;
    rig r;
    rig_open(&r);
    push_down(r.f_https);
    push_down(r.f_http);
    assert_int_equal(hem_recovery_run(&r.o), HEM_RECOVERY_UNREACHABLE);
    char *t = slurp(r.out);
    assert_non_null(strstr(t, "unreachable"));
    free(t);
    rig_close(&r);

    rig_open(&r);
    assert_int_equal(fake_transport_push_response(r.f_https, EHEM_ERR_NETWORK, 0, NULL), 0);
    push(r.f_http, 200, STATUS_HTTPS_ON);
    assert_int_equal(hem_recovery_run(&r.o), HEM_RECOVERY_TLS_OTHER);
    assert_int_equal((int)fake_transport_request_count(r.f_insecure), 0);
    assert_int_equal((int)fake_transport_request_count(r.f_http), 1);
    t = slurp(r.out);
    assert_non_null(strstr(t, "not an expired certificate"));
    assert_non_null(strstr(t, "Nothing was changed"));
    free(t);
    rig_close(&r);

    /* The http status WITHOUT an https field (the healthy dev device omits
     * it): unknown → exit 5 too — tls-recover never runs on a guess. */
    rig_open(&r);
    push_down(r.f_https);
    push(r.f_http, 200, "{\"ctx\":0,\"fls_state\":0,\"uptime\":9,\"temp\":35}");
    assert_int_equal(hem_recovery_run(&r.o), HEM_RECOVERY_TLS_OTHER);
    assert_int_equal((int)fake_transport_request_count(r.f_http), 1);
    t = slurp(r.out);
    assert_non_null(strstr(t, "does not report its HTTPS state"));
    free(t);
    rig_close(&r);
}

/* Usage: --mobile / no passphrase → exit 2, zero traffic. */
static void test_usage(void **state)
{
    (void)state;
    rig r;
    rig_open(&r);
    r.o.mobile = true;
    assert_int_equal(hem_recovery_run(&r.o), HEM_RECOVERY_USAGE);
    r.o.mobile = false;
    r.o.passphrase = NULL;
    assert_int_equal(hem_recovery_run(&r.o), HEM_RECOVERY_USAGE);
    assert_int_equal((int)fake_transport_request_count(r.f_https), 0);
    char *t = slurp(r.out);
    assert_non_null(strstr(t, "sub=\"U\" or \"M\""));
    free(t);
    rig_close(&r);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_healthy),
        cmocka_unit_test(test_expired_with_chain),
        cmocka_unit_test(test_expired_no_renewal),
        cmocka_unit_test(test_https_down_tls_recover),
        cmocka_unit_test(test_unreachable_and_other_tls),
        cmocka_unit_test(test_usage),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
