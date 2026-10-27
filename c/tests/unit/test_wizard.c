/*
 * test_wizard.c — hem-tool `init-wizard` (hem-tool-core wizard.c), scripted
 * dialogues over the fake transport.
 *
 * verifies: REQ-TOOL-028 (accepting every default with a typed mnemonic
 *           posts BYTE-IDENTICAL init bodies to init-device with the
 *           equivalent flags — and so do the generated words and explicit
 *           answers; a passphrase mismatch re-prompts; a wrong backup word
 *           shows the words again and re-asks; cancelling — a wrong
 *           hostname at the summary, or the end of input — exits 6 with
 *           zero transport I/O; a typed mnemonic skips generation and an
 *           invalid one re-prompts; after the one display the words appear
 *           in no later output; the passphrase appears in no output; the
 *           device's verdicts map like init-device's; the reboot question)
 *
 * ATTENDED-ONLY live verification (REQ-TEST-007): the real wizard run is
 * the M12 gate's (STEP-M12-140).
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
#include "wizard.h"
#include "proto_auth.h"
#include "ejwt.h"
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

/* A valid 24-word vector (test_bip39.c, the Manager-code fixtures): its words
 * are distinct from every other word the wizard prints, "bless" is unique. */
static const char LETTER[] =
    "letter advice cage absurd amount doctor acoustic avoid letter advice "
    "cage absurd amount doctor acoustic avoid letter advice cage absurd "
    "amount doctor acoustic bless";
static const char *const LETTER_WORDS[] = {
    "letter", "advice", "cage", "absurd", "amount", "doctor", "acoustic",
    "avoid", "bless",
};
static const unsigned POSITIONS[3] = { 2, 13, 24 };   /* advice, amount, bless */

#define PASS EJWT_FX_PASSPHRASE
/* The 13 settings prompts: user, email, then Enter for the 11 fields that
 * have a default — hostname's is my.ence.do (REQ-TOOL-028 rev 3). */
#define SETTINGS_DEFAULTS "alice\nalice@example.com\n\n\n\n\n\n\n\n\n\n\n\n"
#define PASSPHRASE_TWICE  PASS "\n" PASS "\n"
#define TYPED_LETTER      "t\n" "letter advice cage absurd amount doctor acoustic " \
                          "avoid letter advice cage absurd amount doctor acoustic " \
                          "avoid letter advice cage absurd amount doctor acoustic bless\n"
#define CONFIRM           "my.ence.do\n"

#define FAST_KDF_ITERS 1000

static int64_t g_now;
static int64_t test_now_fn(void) { return g_now; }

static int setup_clock(void **state)
{
    (void)state;
    g_now = EJWT_FX_NOW;
    ehem_auth_test_set_clock(test_now_fn);
    ehem_auth_test_set_kdf_iters(FAST_KDF_ITERS);
    return 0;
}

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

static size_t count(const char *hay, const char *needle)
{
    size_t n = 0;
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += strlen(needle);
    }
    return n;
}

static void push(ehem_transport *fake, int status, const char *body)
{
    assert_int_equal(fake_transport_push_response(fake, EHEM_OK, status, body), 0);
}

static void make_reply(char *out, size_t cap, int reboot_required)
{
    char payload[96], seg[160];
    int m = snprintf(payload, sizeof payload, "{\"exp\":%lld,\"k\":\"init\"}",
                     (long long)(EJWT_FX_NOW + 100000));
    size_t n = ehem_b64url_encode((const uint8_t *)payload, (size_t)m, seg, sizeof seg);
    assert_int_not_equal(n, (size_t)-1);
    snprintf(out, cap, "{\"instanceid\":\"uuid-wiz-1\",\"token\":\"hdr.%s.sig\","
                       "\"reboot_required\":%s}",
             seg, reboot_required ? "true" : "false");
}

/* check-in (0..2), GET init (3), POST init (4). */
static ehem_transport *fake_init_ok(int reboot_required)
{
    char reply[512];
    ehem_transport *fake = fake_transport_new();
    assert_non_null(fake);
    make_reply(reply, sizeof reply, reboot_required);
    push(fake, 200, CI_CHALLENGE);
    push(fake, 200, CI_VERIFIED);
    push(fake, 200, CI_OK);
    push(fake, 200, INIT_CHALLENGE_JSON);
    push(fake, 200, reply);
    return fake;
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

/* The POST init body (request 4) as a NUL-terminated copy. */
static char *posted_body(ehem_transport *fake)
{
    const fake_captured_request *post = fake_transport_request(fake, 4);
    char *copy;
    assert_non_null(post);
    assert_int_equal(post->method, EHEM_HTTP_POST);
    assert_string_equal(post->path, "/api/auth/init");
    copy = malloc(post->body_len + 1);
    assert_non_null(copy);
    memcpy(copy, post->body, post->body_len);
    copy[post->body_len] = '\0';
    return copy;
}

/* init-device's body for the equivalent flags (`tweak` sets the non-default
 * ones). */
static char *init_device_body(const char *words, void (*tweak)(hem_init_opts *))
{
    ehem_transport *fake = fake_init_ok(0);
    ehem_ctx *ctx = ctx_with(fake);
    FILE *out = tmpfile();
    hem_init_opts o;
    char *body;

    assert_non_null(out);
    memset(&o, 0, sizeof o);
    o.passphrase = PASS;
    o.master_words = words;
    o.user = "alice";
    o.email = "alice@example.com";
    o.hostname = "my.ence.do";
    o.out = out;
    o.err = out;
    if (tweak != NULL) {
        tweak(&o);
    }
    assert_int_equal(hem_init_device_run(ctx, &o), HEM_INIT_OK);
    body = posted_body(fake);
    fclose(out);
    ehem_ctx_destroy(ctx);
    fake_transport_free(fake);
    return body;
}

typedef struct {
    int   rc;
    char *out;
    char *err;
} run_result;

/* Run the wizard on `script` against `fake`. */
static run_result run_wizard(ehem_transport *fake, const char *script,
                             const char *test_words)
{
    ehem_ctx *ctx = ctx_with(fake);
    FILE *in = tmpfile(), *out = tmpfile(), *err = tmpfile();
    hem_wizard_opts o;
    run_result r;

    assert_non_null(in);
    assert_non_null(out);
    assert_non_null(err);
    fputs(script, in);
    rewind(in);
    memset(&o, 0, sizeof o);
    o.url = "http://hem.local";
    o.in = in;
    o.out = out;
    o.err = err;
    o.poll_attempts = 4;
    o.poll_delay_ms = 0;
    o.test_words = test_words;
    o.test_positions = POSITIONS;
    r.rc = hem_init_wizard_run(ctx, &o);
    r.out = slurp(out);
    r.err = slurp(err);
    fclose(in);
    fclose(out);
    fclose(err);
    ehem_ctx_destroy(ctx);
    return r;
}

static void assert_no_secrets(const run_result *r)
{
    assert_null(strstr(r->out, PASS));
    assert_null(strstr(r->err, PASS));
}

static void run_free(run_result *r)
{
    free(r->out);
    free(r->err);
}

/* Every default + a typed mnemonic: init-device's exact body; the typed
 * words and the passphrase are never printed. */
static void test_defaults_match_init_device(void **state)
{
    (void)state;
    char *want = init_device_body(LETTER, NULL);
    ehem_transport *fake = fake_init_ok(0);
    run_result r = run_wizard(fake, SETTINGS_DEFAULTS PASSPHRASE_TWICE
                              TYPED_LETTER CONFIRM, NULL);
    char *got;
    size_t i;

    assert_int_equal(r.rc, HEM_INIT_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 5);
    got = posted_body(fake);
    assert_string_equal(got, want);                       /* byte-identical */
    /* REQ-TOOL-028 rev 3: hostname offers my.ence.do and Enter took it (the
     * body above carries init-device's "my.ence.do"); user and email have no
     * default. */
    assert_non_null(strstr(r.out, "  hostname [my.ence.do]: "));
    assert_non_null(strstr(r.out, "  user: "));
    assert_null(strstr(r.out, "  user ["));
    assert_non_null(strstr(r.out, "  email: "));
    assert_null(strstr(r.out, "  email ["));
    assert_non_null(strstr(r.out, "Summary - nothing has been sent"));
    assert_non_null(strstr(r.out, "typed in (not shown)"));
    assert_non_null(strstr(r.out, "initialised: instanceid=uuid-wiz-1"));
    assert_null(strstr(r.out, "MASTER MNEMONIC"));        /* no generation */
    for (i = 0; i < sizeof LETTER_WORDS / sizeof LETTER_WORDS[0]; i++) {
        assert_null(strstr(r.out, LETTER_WORDS[i]));      /* typed words unseen */
    }
    assert_no_secrets(&r);
    assert_int_equal(count(r.err, "not a terminal"), 1);
    free(got);
    free(want);
    run_free(&r);
    fake_transport_free(fake);
}

/* Generated words: shown once, a wrong backup word shows them again and
 * re-asks (case does not matter), then the same body as init-device with
 * those words; after the last display no word appears again. */
static void test_generated_words_backup(void **state)
{
    (void)state;
    char *want = init_device_body(LETTER, NULL);
    ehem_transport *fake = fake_init_ok(0);
    run_result r = run_wizard(fake, SETTINGS_DEFAULTS PASSPHRASE_TWICE
                              "\n"                          /* generate */
                              "advice\nwrong\nbless\n"      /* word 13 wrong */
                              "Advice\namount\nBLESS\n"     /* right */
                              CONFIRM, LETTER);
    const char *tail;
    char *got;
    size_t i;

    assert_int_equal(r.rc, HEM_INIT_OK);
    got = posted_body(fake);
    assert_string_equal(got, want);
    assert_int_equal(count(r.out, "MASTER MNEMONIC - WRITE DOWN"), 2);
    assert_int_equal(count(r.out, "does not match"), 1);
    assert_int_equal(count(r.out, "backup confirmed"), 1);
    assert_non_null(strstr(r.out, "positions 2, 13 and 24"));
    assert_int_equal(count(r.out, "bless"), 2);           /* the two displays only */
    tail = strstr(r.out, "backup confirmed");
    assert_non_null(tail);
    for (i = 0; i < sizeof LETTER_WORDS / sizeof LETTER_WORDS[0]; i++) {
        assert_null(strstr(tail, LETTER_WORDS[i]));
    }
    assert_non_null(strstr(r.out, "generated and backed up"));
    assert_no_secrets(&r);
    free(got);
    free(want);
    run_free(&r);
    fake_transport_free(fake);
}

/* A passphrase mismatch (and an empty one) re-prompts; cancelling at the
 * summary then exits 6 with zero transport I/O. */
static void test_passphrase_mismatch_then_cancel(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    run_result r;

    assert_non_null(fake);
    r = run_wizard(fake, SETTINGS_DEFAULTS
                   "\n"                                      /* empty */
                   PASS "\nsomething-else\n"                 /* mismatch */
                   PASSPHRASE_TWICE TYPED_LETTER
                   "not.the.host\n", NULL);
    assert_int_equal(r.rc, HEM_WIZARD_CANCELLED);
    assert_int_equal(HEM_WIZARD_CANCELLED, 6);
    assert_int_equal((int)fake_transport_request_count(fake), 0);
    assert_int_equal(count(r.out, "cannot be empty"), 1);
    assert_int_equal(count(r.out, "differ"), 1);
    assert_non_null(strstr(r.out, "Summary"));
    assert_non_null(strstr(r.out, "cancelled - nothing was sent"));
    assert_null(strstr(r.out, "something-else"));
    assert_no_secrets(&r);
    run_free(&r);
    fake_transport_free(fake);
}

/* REQ-TOOL-028 rev 3: user and email have no default — an empty answer
 * re-asks; hostname offers my.ence.do and Enter takes it (the summary shows
 * it). Cancelled at the typed confirmation: exit 6, zero transport I/O. */
static void test_required_fields_reprompt(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    run_result r;

    assert_non_null(fake);
    r = run_wizard(fake, "\n" "alice\n"                   /* user: empty, then */
                   "\n" "alice@example.com\n"              /* email: empty, then */
                   "\n\n\n\n\n\n\n\n\n\n\n"                /* hostname .. ctx */
                   PASSPHRASE_TWICE TYPED_LETTER "x\n", NULL);
    assert_int_equal(r.rc, HEM_WIZARD_CANCELLED);
    assert_int_equal((int)fake_transport_request_count(fake), 0);
    assert_int_equal(count(r.out, "user is required"), 1);
    assert_int_equal(count(r.out, "email is required"), 1);
    assert_null(strstr(r.out, "hostname is required"));
    assert_int_equal(count(r.out, "  hostname [my.ence.do]: "), 1);
    assert_non_null(strstr(r.out, "hostname         my.ence.do"));
    assert_non_null(strstr(r.out, "('my.ence.do')"));      /* the confirmation */
    assert_no_secrets(&r);
    run_free(&r);
    fake_transport_free(fake);
}

/* The end of input at any prompt cancels: exit 6, zero transport I/O. */
static void test_end_of_input_cancels(void **state)
{
    (void)state;
    const char *scripts[] = {
        "",
        "alice\n",
        SETTINGS_DEFAULTS PASS "\n",
        SETTINGS_DEFAULTS PASSPHRASE_TWICE "t\n",
        SETTINGS_DEFAULTS PASSPHRASE_TWICE "\nadvice\n",
        SETTINGS_DEFAULTS PASSPHRASE_TWICE TYPED_LETTER,
    };
    size_t k;
    for (k = 0; k < sizeof scripts / sizeof scripts[0]; k++) {
        ehem_transport *fake = fake_transport_new();
        run_result r;
        assert_non_null(fake);
        r = run_wizard(fake, scripts[k], LETTER);
        assert_int_equal(r.rc, HEM_WIZARD_CANCELLED);
        assert_int_equal((int)fake_transport_request_count(fake), 0);
        run_free(&r);
        fake_transport_free(fake);
    }
}

/* An invalid typed mnemonic re-prompts without quoting it back. */
static void test_invalid_mnemonic_reprompts(void **state)
{
    (void)state;
    ehem_transport *fake = fake_transport_new();
    run_result r;

    assert_non_null(fake);
    r = run_wizard(fake, SETTINGS_DEFAULTS PASSPHRASE_TWICE
                   "t\nabandon abandon zebra\n"
                   "letter advice cage absurd amount doctor acoustic avoid letter "
                   "advice cage absurd amount doctor acoustic avoid letter advice "
                   "cage absurd amount doctor acoustic bless\n"
                   "nope\n", NULL);
    assert_int_equal(r.rc, HEM_WIZARD_CANCELLED);
    assert_int_equal(count(r.out, "not a valid 24-word mnemonic"), 1);
    assert_null(strstr(r.out, "zebra"));
    assert_null(strstr(r.err, "zebra"));
    assert_null(strstr(r.out, "abandon"));
    assert_int_equal((int)fake_transport_request_count(fake), 0);
    run_free(&r);
    fake_transport_free(fake);
}

static void tweak_explicit(hem_init_opts *o)
{
    o->ip = "10.0.0.9/16";
    o->storage_mode = 83;
    o->disk0_size = 4194304;
    o->origin = "https://app";
    o->dnsd = 1;
    o->no_trusted_backend = 1;
    o->gen_csr = 1;
    o->ctx_id = 5;
    o->hostname = "dev.ence.do";          /* typed over the my.ence.do default */
}

/* Explicit answers (after rejected ones) reach the body exactly as the
 * equivalent init-device flags. */
static void test_explicit_answers_match_flags(void **state)
{
    (void)state;
    char *want = init_device_body(LETTER, tweak_explicit);
    ehem_transport *fake = fake_init_ok(0);
    run_result r = run_wizard(fake,
                              "alice\nalice@example.com\ndev.ence.do\n"
                              "10.0.0.9/16\n"
                              "99\n83\n"                     /* storage: 99 refused */
                              "abc\n4194304\n"               /* disk0 */
                              "https://app\n"
                              "maybe\ny\n"                   /* dnsd */
                              "\n"                           /* trusted-ts: yes */
                              "n\n"                          /* trusted-backend */
                              "\n"                           /* allow-keysearch: yes */
                              "yes\n"                        /* gen-csr */
                              "-1\n5\n"                      /* ctx: -1 refused */
                              PASSPHRASE_TWICE TYPED_LETTER "dev.ence.do\n", NULL);
    char *got;

    assert_int_equal(r.rc, HEM_INIT_OK);
    got = posted_body(fake);
    assert_string_equal(got, want);
    assert_non_null(strstr(r.out, "one of: 81 83 161 163 80 160"));
    assert_non_null(strstr(r.out, "answer y or n"));
    assert_int_equal(count(r.out, "a whole number from"), 2);
    free(got);
    free(want);
    run_free(&r);
    fake_transport_free(fake);
}

/* The device's verdicts map like init-device's (406 → 3); the reboot
 * question: yes reboots and waits, no leaves the next-steps hint. */
static void test_verdicts_and_reboot_question(void **state)
{
    (void)state;
    ehem_transport *fake;
    run_result r;

    /* 406 on GET init → already initialised, exit 3. */
    fake = fake_transport_new();
    assert_non_null(fake);
    push(fake, 200, CI_CHALLENGE);
    push(fake, 200, CI_VERIFIED);
    push(fake, 200, CI_OK);
    push(fake, 406, NULL);
    r = run_wizard(fake, SETTINGS_DEFAULTS PASSPHRASE_TWICE TYPED_LETTER CONFIRM, NULL);
    assert_int_equal(r.rc, HEM_INIT_ALREADY);
    run_free(&r);
    fake_transport_free(fake);

    /* reboot_required + "y" → reboot, seen down, back. */
    fake = fake_init_ok(1);
    push(fake, 200, NULL);                                   /* 5: reboot */
    assert_int_equal(fake_transport_push_response(fake, EHEM_ERR_UNREACHABLE, 0, NULL), 0);
    push(fake, 200, STATUS_OK);                              /* 7: back */
    r = run_wizard(fake, SETTINGS_DEFAULTS PASSPHRASE_TWICE TYPED_LETTER CONFIRM "y\n",
                   NULL);
    assert_int_equal(r.rc, HEM_INIT_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 8);
    assert_string_equal(fake_transport_request(fake, 5)->path, "/api/system/reboot");
    assert_non_null(strstr(r.out, "reboot now and wait"));
    assert_non_null(strstr(r.out, "device back"));
    run_free(&r);
    fake_transport_free(fake);

    /* reboot_required + "n" → no reboot request, the hint instead. */
    fake = fake_init_ok(1);
    r = run_wizard(fake, SETTINGS_DEFAULTS PASSPHRASE_TWICE TYPED_LETTER CONFIRM "n\n",
                   NULL);
    assert_int_equal(r.rc, HEM_INIT_OK);
    assert_int_equal((int)fake_transport_request_count(fake), 5);
    assert_non_null(strstr(r.out, "hem-tool reboot (required)"));
    run_free(&r);
    fake_transport_free(fake);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_defaults_match_init_device, setup_clock),
        cmocka_unit_test_setup(test_generated_words_backup, setup_clock),
        cmocka_unit_test_setup(test_passphrase_mismatch_then_cancel, setup_clock),
        cmocka_unit_test_setup(test_required_fields_reprompt, setup_clock),
        cmocka_unit_test_setup(test_end_of_input_cancels, setup_clock),
        cmocka_unit_test_setup(test_invalid_mnemonic_reprompts, setup_clock),
        cmocka_unit_test_setup(test_explicit_answers_match_flags, setup_clock),
        cmocka_unit_test_setup(test_verdicts_and_reboot_question, setup_clock),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
