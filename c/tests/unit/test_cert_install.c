/*
 * test_cert_install.c — hem-tool `cert-install` orchestration, driven entirely
 * offline through the fake transport.
 *
 * verifies: REQ-TOOL-003 (harvest → skip-if-current → install → reboot →
 *           verify; skip path, --force, one distinct exit code per failure mode)
 *
 * The tool code under test (src/tools/hem-tool/cert_install.c, linked via
 * hem-tool-core) uses ONLY the public API; this test scripts the device + cloud
 * legs on the fake transport and drives the auth exchange with the internal
 * clock/KDF seams (proto_auth.h) so the install path's token acquisition is
 * cheap and deterministic. Each failure mode is asserted to exit with its own
 * distinct code (REQ-TOOL-003 acceptance criteria).
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
#include "ehem/system.h"
#include "ehem/auth.h"
#include "cert_install.h"
#include "proto_auth.h"       /* internal: auth clock + KDF-iters test seams */
#include "ejwt.h"             /* internal: base64url encoder for crafted tokens */
#include "transport.h"        /* internal: ehem_http_method */
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"
#include "fixtures/cert_fixture.h"

/* Auth challenge the fake device serves (fixture inputs; matches test_auth's
 * SYNCED variant — exp = pinned now + 60, i.e. a healthy device clock, so the
 * 401 case stays a plain credential failure and never triggers the
 * REQ-AUTH-004 drift recovery). */
#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":1700000060,\"lbl\":\"alice\"}"

/* Leg-3 device check-in reply and a minimal post-reboot status. */
static const char CI_OK[]      = "{\"status\":\"ok\",\"newcrt\":\"cert refreshed\"}";
static const char STATUS_MIN[] =
    "{ \"ctx\": 2, \"fls_state\": 0, \"uptime\": 42, \"temp\": 40 }";
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

static ehem_ctx *ctx_with(ehem_transport *fake)
{
    ehem_options opts;
    ehem_ctx *ctx = NULL;
    ehem_options_init(&opts);
    opts.transport = fake;
    assert_int_equal(ehem_ctx_create("https://hem.local", &opts, &ctx), EHEM_OK);
    return ctx;
}

/* Queue a {"token":"hdr.<b64url({"exp":N})>.sig"} auth response. */
static void push_token(ehem_transport *fake, int64_t token_exp)
{
    char payload[64], seg[128], resp[256];
    int m = snprintf(payload, sizeof payload, "{\"exp\":%lld}", (long long)token_exp);
    size_t sn = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(sn, (size_t)-1);
    snprintf(resp, sizeof resp, "{\"token\":\"hdr.%s.sig\"}", seg);
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, 200, resp), 0);
}

static void push(ehem_transport *fake, long status, const char *body)
{
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, body), 0);
}

/* Queue the three successful check-in legs (leg-1 challenge chosen by caller). */
static void push_checkin(ehem_transport *fake, const char *leg1)
{
    push(fake, 200, leg1);
    push(fake, 200, EHEM_FX_CHECKED_WITH_NEWCRT);
    push(fake, 200, CI_OK);
}

/* Queue a successful auth exchange (challenge GET + token POST). */
static void push_auth(ehem_transport *fake)
{
    push(fake, 200, CHALLENGE_JSON);
    push_token(fake, EJWT_FX_NOW + 100000);
}

/* Output capture via a temp file (portable: tmpfile/fseek/fread, unlike the
 * POSIX-only open_memstream — the unit suite must build on MinGW too). */
typedef struct { FILE *f; char buf[8192]; } cap;
static void cap_open(cap *c)
{
    c->f = tmpfile();
    assert_non_null(c->f);
    c->buf[0] = '\0';
}
static const char *cap_str(cap *c)
{
    long n;
    size_t r;
    fflush(c->f);
    n = ftell(c->f);
    if (n < 0) {
        n = 0;
    }
    if (n > (long)sizeof(c->buf) - 1) {
        n = (long)sizeof(c->buf) - 1;
    }
    fseek(c->f, 0, SEEK_SET);
    r = fread(c->buf, 1, (size_t)n, c->f);
    c->buf[r] = '\0';
    return c->buf;
}
static void cap_close(cap *c) { fclose(c->f); }

/* Run cert-install on a fresh ctx wrapping `fake`, capturing out/err. */
static int run(ehem_transport *fake, const char *passphrase, int force,
               int insecure, unsigned attempts, cap *out, cap *err)
{
    ehem_ctx *ctx = ctx_with(fake);
    hem_cert_install_opts o;
    int rc;
    memset(&o, 0, sizeof o);
    o.passphrase   = passphrase;
    o.force        = force;
    o.insecure     = insecure;
    o.poll_attempts = attempts;
    o.poll_delay_ms = 0;            /* never sleep in tests */
    o.out = out->f;
    o.err = err->f;
    rc = hem_cert_install_run(ctx, &o);
    ehem_ctx_destroy(ctx);
    return rc;
}

/* -------------------------------------------------------------------------- */

/* Full happy path: harvest → not-current → login → install → reboot → poll →
 * verify, with a summary naming old→new serial and the new validity window. */
static void test_full_install_flow(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);

    push_checkin(fake, EHEM_FX_CHECK_CSN_OTHER);   /* current serial != leaf */
    push_auth(fake);
    push(fake, 200, INSTALL_OK);                   /* /api/system/config */
    push(fake, 200, NULL);                         /* /api/system/reboot: empty 200 */
    assert_int_equal(                              /* still rebooting */
        fake_transport_push_response(fake, EHEM_ERR_UNREACHABLE, 0, NULL), 0);
    push(fake, 200, STATUS_MIN);                   /* back up */

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, 0, 0, 5, &out, &err),
                     HEM_CERT_OK);

    /* Full request sequence, in order. */
    assert_int_equal((int)fake_transport_request_count(fake), 9);
    assert_string_equal(fake_transport_request(fake, 0)->path, "/api/system/checkin");
    assert_string_equal(fake_transport_request(fake, 1)->path, EHEM_DEFAULT_CHECKIN_URL);
    assert_string_equal(fake_transport_request(fake, 2)->path, "/api/system/checkin");
    assert_string_equal(fake_transport_request(fake, 3)->path, "/api/auth/token");
    assert_string_equal(fake_transport_request(fake, 4)->path, "/api/auth/token");
    assert_string_equal(fake_transport_request(fake, 5)->path, "/api/system/config");
    assert_string_equal(fake_transport_request(fake, 6)->path, "/api/system/reboot");
    assert_string_equal(fake_transport_request(fake, 7)->path, "/api/system/status");
    assert_string_equal(fake_transport_request(fake, 8)->path, "/api/system/status");

    /* The install POST carried the harvested chain in the cert-only body. */
    assert_non_null(strstr((const char *)fake_transport_request(fake, 5)->body,
                           EHEM_FX_LEAF_B64));

    /* Summary names old→new serial + the new validity window; verify proven. */
    const char *o = cap_str(&out);
    assert_non_null(strstr(o, "certificate rotated"));
    assert_non_null(strstr(o, EHEM_FX_OTHER_SERIAL " -> " EHEM_FX_LEAF_SERIAL));
    assert_non_null(strstr(o, EHEM_FX_LEAF_NOT_BEFORE));
    assert_non_null(strstr(o, EHEM_FX_LEAF_NOT_AFTER));
    assert_non_null(strstr(o, "verify:   OK"));

    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* Device already serves the delivered cert → exit 0 without install/reboot. */
static void test_skip_if_current(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_checkin(fake, EHEM_FX_CHECK_CSN_MATCH);   /* current serial == leaf */

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, 0, 0, 5, &out, &err),
                     HEM_CERT_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 3);   /* check-in only */
    assert_non_null(strstr(cap_str(&out), "already current"));
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* --force reinstalls even when the device is already current. */
static void test_force_reinstalls(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_checkin(fake, EHEM_FX_CHECK_CSN_MATCH);   /* already current... */
    push_auth(fake);
    push(fake, 200, INSTALL_OK);
    push(fake, 200, NULL);
    push(fake, 200, STATUS_MIN);

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, /*force=*/1, 0, 5, &out, &err),
                     HEM_CERT_OK);
    /* ...but --force proceeds through install + reboot + verify. */
    assert_int_equal((int)fake_transport_request_count(fake), 8);
    assert_non_null(strstr(cap_str(&out), "certificate rotated"));
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* No chain AND no current serial (device loads no cert / non-TLD hostname) →
 * distinct nonzero exit, nothing installed. */
static void test_no_chain(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push(fake, 200, EHEM_FX_CHECK_NO_CSN);
    push(fake, 200, EHEM_FX_CHECKED_NO_NEWCRT);
    push(fake, 200, CI_OK);

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, 0, 0, 5, &out, &err),
                     HEM_CERT_NO_CHAIN);
    assert_int_equal((int)fake_transport_request_count(fake), 3);
    assert_non_null(strstr(cap_str(&err), "no certificate"));
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* No chain BUT the device reports a current serial: the broker suppressed the
 * chain because the device is already up to date (the real-device signal) →
 * exit 0 "already current", nothing installed. */
static void test_already_current_no_chain(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push(fake, 200, EHEM_FX_CHECK_CSN_OTHER);      /* device has a serial... */
    push(fake, 200, EHEM_FX_CHECKED_NO_NEWCRT);    /* ...but no chain delivered */
    push(fake, 200, CI_OK);

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, 0, 0, 5, &out, &err),
                     HEM_CERT_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 3);
    assert_non_null(strstr(cap_str(&out), "already current"));
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* The check-in itself fails → distinct exit. */
static void test_checkin_failed(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push(fake, 500, "boom");

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, 0, 0, 5, &out, &err),
                     HEM_CERT_CHECKIN_FAILED);
    assert_int_equal((int)fake_transport_request_count(fake), 1);
    assert_non_null(strstr(cap_str(&err), "check-in"));
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* Delivered chain does not parse → distinct exit. */
static void test_parse_failed(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push(fake, 200, EHEM_FX_CHECK_CSN_OTHER);
    push(fake, 200, EHEM_FX_CHECKED_BAD_NEWCRT);
    push(fake, 200, CI_OK);

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, 0, 0, 5, &out, &err),
                     HEM_CERT_PARSE_FAILED);
    assert_int_equal((int)fake_transport_request_count(fake), 3);
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* Install needed but no passphrase → distinct exit, device untouched. */
static void test_no_passphrase(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_checkin(fake, EHEM_FX_CHECK_CSN_OTHER);

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, /*passphrase=*/NULL, 0, 0, 5, &out, &err),
                     HEM_CERT_NO_PASSPHRASE);
    assert_int_equal((int)fake_transport_request_count(fake), 3);
    assert_non_null(strstr(cap_str(&err), "authentication"));
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* Authentication rejected during install → distinct exit. */
static void test_auth_failed(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_checkin(fake, EHEM_FX_CHECK_CSN_OTHER);
    push(fake, 200, CHALLENGE_JSON);
    push(fake, 401, "{\"error\":\"bad passphrase\"}");   /* token POST rejected */

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, 0, 0, 5, &out, &err),
                     HEM_CERT_AUTH_FAILED);
    assert_int_equal((int)fake_transport_request_count(fake), 5);
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* Device rejects the certificate (HTTP 400) → distinct exit. */
static void test_install_rejected(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_checkin(fake, EHEM_FX_CHECK_CSN_OTHER);
    push_auth(fake);
    push(fake, 400, "{\"error\":\"invalid certificate\"}");

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, 0, 0, 5, &out, &err),
                     HEM_CERT_INSTALL_FAILED);
    assert_int_equal((int)fake_transport_request_count(fake), 6);
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* Reboot request fails → distinct exit. */
static void test_reboot_failed(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_checkin(fake, EHEM_FX_CHECK_CSN_OTHER);
    push_auth(fake);
    push(fake, 200, INSTALL_OK);
    push(fake, 403, "{\"error\":\"not allowed\"}");   /* reboot: 403, no retry */

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, 0, 0, 5, &out, &err),
                     HEM_CERT_REBOOT_FAILED);
    assert_int_equal((int)fake_transport_request_count(fake), 7);
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* Device never returns after reboot → distinct exit, TLS-mode-aware message. */
static void test_device_timeout(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_checkin(fake, EHEM_FX_CHECK_CSN_OTHER);
    push_auth(fake);
    push(fake, 200, INSTALL_OK);
    push(fake, 200, NULL);                        /* reboot ok */
    fake_transport_set_default_rc(fake, EHEM_ERR_UNREACHABLE);   /* every poll fails */

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, 0, /*insecure=*/0, 3, &out, &err),
                     HEM_CERT_DEVICE_TIMEOUT);
    /* checkin(3) + auth(2) + install(1) + reboot(1) + 3 polls = 10. */
    assert_int_equal((int)fake_transport_request_count(fake), 10);
    assert_non_null(strstr(cap_str(&err), "system-trusted"));   /* verifying mode */
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

/* Under --insecure the verify is reported as skipped, not proven. */
static void test_insecure_verify_skipped(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push_checkin(fake, EHEM_FX_CHECK_CSN_OTHER);
    push_auth(fake);
    push(fake, 200, INSTALL_OK);
    push(fake, 200, NULL);
    push(fake, 200, STATUS_MIN);

    cap out, err;
    cap_open(&out); cap_open(&err);
    assert_int_equal(run(fake, EJWT_FX_PASSPHRASE, 0, /*insecure=*/1, 5, &out, &err),
                     HEM_CERT_OK);
    assert_non_null(strstr(cap_str(&out), "verify:   SKIPPED"));
    cap_close(&out); cap_close(&err);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_full_install_flow),
        cmocka_unit_test(test_skip_if_current),
        cmocka_unit_test(test_force_reinstalls),
        cmocka_unit_test(test_no_chain),
        cmocka_unit_test(test_already_current_no_chain),
        cmocka_unit_test(test_checkin_failed),
        cmocka_unit_test(test_parse_failed),
        cmocka_unit_test(test_no_passphrase),
        cmocka_unit_test(test_auth_failed),
        cmocka_unit_test(test_install_rejected),
        cmocka_unit_test(test_reboot_failed),
        cmocka_unit_test(test_device_timeout),
        cmocka_unit_test(test_insecure_verify_skipped),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
