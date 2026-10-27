/*
 * test_recover.c — hem-tool `tls-recover` orchestration, driven offline
 * through the fake transport.
 *
 * verifies: REQ-TOOL-015 (skip-if-healthy exit 0 + --force override; the full
 *           downed-device sequence status → check-in → login → recovery →
 *           reboot → poll until https:true, exit 0; no-bundle → exit 3; poll
 *           exhaustion → exit 4; missing passphrase → exit 2)
 *
 * hem-tool-core code (recover.c) uses ONLY the public API; the auth exchange
 * is driven with the internal clock/KDF seams like the other tool tests.
 * poll_delay_ms is forced to 0 so tests never sleep.
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
#include "ehem/system.h"     /* EHEM_DEFAULT_REGISTER_URL */
#include "recover.h"
#include "proto_auth.h"
#include "ejwt.h"
#include "transport.h"
#include "fake_transport.h"
#include "fixtures/ejwt_login_vector.h"

#define CHALLENGE_JSON \
    "{\"eid\":\"" EJWT_FX_EID "\",\"spk\":\"" EJWT_FX_SPK "\"," \
    "\"jti\":\"" EJWT_FX_JTI "\",\"exp\":2000000000,\"lbl\":\"alice\"}"

/* Device check-in legs (leg1 device / leg2 cloud / leg3 device). */
static const char CI_CHALLENGE[] = "{\"check\":\"BLOB\"}";
static const char CI_VERIFIED[]  = "{\"checked\":\"CLOUD\"}";
static const char CI_OK[]        = "{\"status\":\"ok\",\"newcrt\":\"\"}";

static const char STATUS_HTTPS_OFF[] =
    "{\"ctx\":0,\"fls_state\":0,\"uptime\":9,\"temp\":35,\"https\":false}";
static const char STATUS_HTTPS_ON[] =
    "{\"ctx\":0,\"fls_state\":0,\"uptime\":9,\"temp\":35,\"https\":true}";

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
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, body),
                     0);
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

/* Queue the shared recovery core: check-in legs (3) + login (2) + attestation
 * (1) + cloud register (1) + config install (1). */
static void push_recovery_core(ehem_transport *fake)
{
    push(fake, 200, CI_CHALLENGE);
    push(fake, 200, CI_VERIFIED);
    push(fake, 200, CI_OK);
    push_login(fake, "rc");
    push(fake, 200, "{\"crt\":\"x\",\"genuine\":\"g\"}");   /* attestation */
    push(fake, 200, "{\"crt\":\"Q1JU\",\"key\":\"K.C\",\"emp\":\"AAA\"}"); /* cloud */
    push(fake, 200, "{\"updated\":true,\"reboot_required\":true}");        /* install */
}

static hem_recover_opts base_opts(void)
{
    hem_recover_opts o;
    memset(&o, 0, sizeof o);
    o.passphrase   = EJWT_FX_PASSPHRASE;
    o.poll_delay_ms = 0;                 /* never sleep in tests */
    o.poll_attempts = 5;
    o.out = tmpfile();
    o.err = tmpfile();
    return o;
}

static void end_opts(hem_recover_opts *o)
{
    if (o->out) fclose(o->out);
    if (o->err) fclose(o->err);
}

/* -------------------------------------------------------------------------- */

/* Device already serving HTTPS → skip, exit 0, only the status probe sent. */
static void test_skip_when_healthy(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push(fake, 200, STATUS_HTTPS_ON);

    ehem_ctx *ctx = ctx_with(fake);
    hem_recover_opts o = base_opts();
    assert_int_equal(hem_tls_recover_run(ctx, &o), HEM_RECOVER_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 1);  /* status only */
    end_opts(&o);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* --force overrides the skip: the full recovery runs even when HTTPS is up. */
static void test_force_overrides_skip(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    /* No initial status probe when --force (it is skipped). */
    push_recovery_core(fake);
    push(fake, 200, NULL);                /* reboot: empty 200 */
    push(fake, 200, STATUS_HTTPS_ON);     /* post-reboot poll → back with HTTPS */

    ehem_ctx *ctx = ctx_with(fake);
    hem_recover_opts o = base_opts();
    o.force = 1;
    assert_int_equal(hem_tls_recover_run(ctx, &o), HEM_RECOVER_OK);
    /* checkin(3) + login(2) + attest(1) + cloud(1) + install(1) + reboot(1)
     * + poll(1) = 10; no leading status probe. */
    assert_int_equal((int)fake_transport_request_count(fake), 10);
    assert_string_equal(fake_transport_request(fake, 8)->path,
                        "/api/system/reboot");
    assert_string_equal(fake_transport_request(fake, 9)->path,
                        "/api/system/status");
    end_opts(&o);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Downed device: status https:false → full recovery → reboot → the second
 * poll reports HTTPS back → exit 0. */
static void test_full_recovery(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push(fake, 200, STATUS_HTTPS_OFF);    /* initial probe: needs recovery */
    push_recovery_core(fake);
    push(fake, 200, NULL);                /* reboot: empty 200 */
    push(fake, 200, STATUS_HTTPS_OFF);    /* poll 1: not back yet */
    push(fake, 200, STATUS_HTTPS_ON);     /* poll 2: HTTPS up */

    ehem_ctx *ctx = ctx_with(fake);
    hem_recover_opts o = base_opts();
    assert_int_equal(hem_tls_recover_run(ctx, &o), HEM_RECOVER_OK);
    /* initial status(0) + checkin(1,2,3) + login(4,5) + attest(6) +
     * cloud register(7): carried {genuine} to the default URL. */
    const fake_captured_request *reg = fake_transport_request(fake, 7);
    assert_string_equal(reg->path, EHEM_DEFAULT_REGISTER_URL);
    assert_string_equal((const char *)reg->body, "{\"genuine\":\"g\"}");
    end_opts(&o);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Cloud reply without `crt` → exit 3, no reboot. */
static void test_no_bundle(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push(fake, 200, STATUS_HTTPS_OFF);
    push(fake, 200, CI_CHALLENGE);
    push(fake, 200, CI_VERIFIED);
    push(fake, 200, CI_OK);
    push_login(fake, "nb");
    push(fake, 200, "{\"crt\":\"x\",\"genuine\":\"g\"}");
    push(fake, 200, "{\"error\":\"unknown device\"}");   /* no crt */

    ehem_ctx *ctx = ctx_with(fake);
    hem_recover_opts o = base_opts();
    assert_int_equal(hem_tls_recover_run(ctx, &o), HEM_RECOVER_NO_BUNDLE);
    end_opts(&o);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Device never returns serving HTTPS within the poll budget → exit 4. */
static void test_poll_exhaustion(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    push(fake, 200, STATUS_HTTPS_OFF);
    push_recovery_core(fake);
    push(fake, 200, NULL);                /* reboot: empty 200 */
    /* Every poll still reports https:false (poll_attempts = 5). */
    int i;
    for (i = 0; i < 5; i++) {
        push(fake, 200, STATUS_HTTPS_OFF);
    }

    ehem_ctx *ctx = ctx_with(fake);
    hem_recover_opts o = base_opts();
    assert_int_equal(hem_tls_recover_run(ctx, &o), HEM_RECOVER_TIMEOUT);
    end_opts(&o);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

/* Missing passphrase → usage, no traffic. */
static void test_missing_passphrase(void **state)
{
    (void)state;
    set_now(EJWT_FX_NOW);
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);

    ehem_ctx *ctx = ctx_with(fake);
    hem_recover_opts o = base_opts();
    o.passphrase = NULL;
    assert_int_equal(hem_tls_recover_run(ctx, &o), HEM_RECOVER_USAGE);
    assert_int_equal((int)fake_transport_request_count(fake), 0);
    end_opts(&o);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_skip_when_healthy),
        cmocka_unit_test(test_force_overrides_skip),
        cmocka_unit_test(test_full_recovery),
        cmocka_unit_test(test_no_bundle),
        cmocka_unit_test(test_poll_exhaustion),
        cmocka_unit_test(test_missing_passphrase),
    };
    if (ehem_global_init() != EHEM_OK) {
        return 1;
    }
    int failed = cmocka_run_group_tests(tests, NULL, NULL);
    ehem_global_cleanup();
    return failed;
}
