/*
 * hem-tool — a thin CLI over the Encedo HEM C SDK public API.
 *
 * implements: REQ-TOOL-001 (the `status` subcommand),
 *             REQ-TOOL-002 (certificate-refresh notice, `checkin` subcommand),
 *             REQ-TOOL-003 (the `cert-install` subcommand),
 *             REQ-TOOL-004 (the `keys list` subcommand),
 *             REQ-TOOL-006 (the `keys rm` subcommand),
 *             REQ-TOOL-007 (the `keys pub` subcommand),
 *             REQ-TOOL-008 (the `sign` subcommand),
 *             REQ-TOOL-009 (the `keys gen` subcommand),
 *             REQ-TOOL-011 (the `keys update` subcommand),
 *             REQ-TOOL-012 (the `logs` subcommands),
 *             REQ-TOOL-013 (the `selftest` subcommand),
 *             REQ-TOOL-014 (the `reboot` subcommand),
 *             REQ-TOOL-015 (the `tls-recover` subcommand),
 *             REQ-TOOL-024 (ehem_global_init up front, exit 12 on refusal),
 *             REQ-TOOL-025 (`--version` / `version`),
 *             REQ-TOOL-027 (`keys types`, dispatched before the SDK init),
 *             REQ-TOOL-028 (the `init-wizard` subcommand)
 *
 * Consumes ONLY the public headers in include/ehem/ — it doubles as living
 * documentation of the API and as the manual driver for the M1/M2 gates.
 * Argument parsing is dependency-free; the subcommand table is structured so
 * `keys list` / `keys rm` (M3) slot in without reworking main().
 *
 * Usage:
 *   hem-tool [--url URL] [--cacert FILE | --insecure] [--passphrase PW]
 *            [--force] <status|checkin|cert-install>
 * Connection URL comes from --url or the EHEM_URL environment variable; the
 * passphrase from EHEM_PASSPHRASE or --passphrase (deprecated since 1.2.0 —
 * a warning on stderr; REQ-TOOL-020 rev 2).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ehem/auth.h"
#include "ehem/ehem.h"
#include "ehem/system.h"

#include "cert_install.h"
#include "ext_cmd.h"
#include "init_cmd.h"
#include "keys.h"
#include "keytypes.h"
#include "logs.h"
#include "random.h"
#include "recover.h"
#include "recovery.h"
#include "registry.h"
#include "selftest.h"
#include "sign.h"
#include "startup.h"
#include "wipe.h"
#include "wizard.h"
#include "tool_auth.h"

#define MAX_LABEL_PREFIXES 32

#ifdef HEM_TOOL_DELAYLOAD
/* Windows release build (delayload_win.c, REQ-TOOL-026): while set, a missing
 * libwolfssl.dll reads "not found" in the --version report instead of ending
 * the run. */
void hem_tool_delayload_tolerate(int on);
#endif

typedef struct {
    const char *url;
    const char *cacert;
    int         insecure;
    const char *passphrase;   /* --passphrase / EHEM_PASSPHRASE (cert-install, keys) */
    int         force;        /* --force (cert-install) */

    /* keys rm selection/behavior (REQ-TOOL-006). */
    int         all;          /* --all */
    int         dry_run;      /* --dry-run */
    int         assume_yes;   /* --yes */
    const char *prefixes[MAX_LABEL_PREFIXES];  /* --label-prefix (repeatable) */
    size_t      prefix_count;

    /* keys pub / sign output format (REQ-TOOL-007/008). */
    int         hex;          /* --hex */
    int         raw;          /* --raw */

    /* sign inputs (REQ-TOOL-008). */
    const char *alg;          /* --alg (verbatim selector; NULL → default) */
    const char *in_path;      /* --in (message file; NULL → stdin) */
    const char *sigctx;       /* --sigctx (RFC 8032 context string) */

    /* keys gen inputs (REQ-TOOL-009). */
    const char *label;        /* --label (required for keys gen) */
    const char *descr;        /* --descr (optional raw-bytes blob) */
    const char *mode;         /* --mode (ECDH|ExDSA|ECDH,ExDSA; NULL → auto) */

    /* random inputs (REQ-TOOL-010). */
    const char *kid;          /* --kid (existing AES key; NULL → transient) */

    /* logs get output (REQ-TOOL-012). */
    const char *out_path;     /* --out (write the log file here; NULL → stdout) */

    /* ext family (REQ-TOOL-016). */
    int         no_qr;        /* --no-qr (print the QR payload JSON instead) */
    const char *scope;        /* --scope (ext login; NULL → system:config) */
    const char *note;         /* --note (free text for the phone UI) */
    long        timeout_sec;  /* --timeout (ext login confirmation, seconds) */
    const char *notify_url;   /* --notify-url (broker base override) */

    /* reboot behavior (REQ-TOOL-014). */
    int         wait_back;    /* --wait: poll until the device answers again */

    /* init-device inputs (REQ-TOOL-021). */
    const char *user;         /* --user */
    const char *email;        /* --email */
    const char *hostname;     /* --hostname */
    const char *ip;           /* --ip */
    const char *origin;       /* --origin */
    const char *master_words; /* --master-words (else EHEM_MASTER_WORDS) */
    const char *master_hex;   /* --master-secret-hex */
    const char *csr_out;      /* --csr-out */
    int         master_generate;   /* --master-generate */
    int         storage_mode;      /* --storage-mode */
    long long   disk0_size;        /* --disk0-size */
    int         dnsd;              /* --dnsd */
    int         no_trusted_ts;     /* --no-trusted-ts */
    int         no_trusted_backend;/* --no-trusted-backend */
    int         no_allow_keysearch;/* --no-allow-keysearch */
    int         gen_csr;           /* --gen-csr */
    int         ctx_id;            /* --ctx */
    int         reboot_flag;       /* --reboot */

    /* auth mode (REQ-TOOL-018). */
    int         mobile;       /* --mobile: push-confirm instead of passphrase */
    int         pw_flag;      /* --passphrase given explicitly (vs env) —
                               * --mobile + --passphrase is a usage error */

    /* help (REQ-TOOL-020): -h/--help defers until the command is known so
     * `hem-tool keys rm --help` renders keys-rm help, not the top page. */
    int         want_help;

    /* --version (REQ-TOOL-025): wins over any command, like --help. */
    int         want_version;
} cli_opts;

/* REQ-TOOL-019/020: the top-level page renders from the command
 * registry (single source of truth in hem-tool-core). */
static void usage(FILE *f)
{
    hem_help_top(f, ehem_version());
}

/* REQ-TOOL-002: a security-relevant event (the device presented an invalid
 * certificate; the SDK refreshed it via check-in) must be visible. */
static void print_cert_notice(const ehem_ctx *ctx)
{
    if (ehem_cert_refreshed(ctx)) {
        fprintf(stderr,
                "notice: device TLS certificate was invalid (expired) - "
                "refreshed via check-in; connection re-verified\n");
    }
}

/* Print the last-error detail recorded on the context to stderr. */
static void print_last_error(ehem_ctx *ctx, ehem_rc rc, const char *what)
{
    const ehem_error *e = ehem_last_error(ctx);
    fprintf(stderr, "error: %s: %s\n", what, ehem_rc_str(rc));
    if (e != NULL) {
        if (e->message != NULL && e->message[0] != '\0') {
            fprintf(stderr, "  detail: %s\n", e->message);
        }
        if (e->http_status != 0) {
            fprintf(stderr, "  http status: %ld\n", e->http_status);
        }
        if (e->device_payload != NULL) {
            fprintf(stderr, "  device: %s\n", e->device_payload);
        }
    }
}

/* implements: REQ-TOOL-018 — every mobile push announces what it asks for
 * (one line per scope acquisition; multi-scope commands push more than once). */
static void mobile_push_notice(const char *scope, long timeout_ms, void *arg)
{
    (void)arg;
    fprintf(stderr, "mobile: push sent - approve \"%s\" on your phone "
                    "(waiting up to %ld s)\n", scope, timeout_ms / 1000);
}

/* The CLI connection/auth options as SDK options (shared by make_ctx and the
 * multi-posture `recovery` command). */
static void fill_opts(const cli_opts *o, ehem_options *opts)
{
    ehem_options_init(opts);
    if (o->timeout_sec > 0) {
        /* implements: REQ-TOOL-016 (ext login --timeout → confirm wait) */
        opts->confirm_timeout_ms = o->timeout_sec * 1000L;
    }
    if (o->mobile) {
        /* ext login keeps its own richer push line (hook set only here). */
        opts->confirm_notice = mobile_push_notice;
    }
    if (o->insecure) {
        opts->tls_mode = EHEM_TLS_INSECURE;
    } else if (o->cacert != NULL) {
        opts->tls_mode = EHEM_TLS_CA_FILE;
        opts->ca_file  = o->cacert;
    }
    /* Test hook: point the check-in relay leg somewhere else than the Encedo
     * cloud (the release workflow's expired-certificate check runs against a
     * local fake device and must not post its garbage to api.encedo.com). */
    {
        const char *ci = getenv("EHEM_CHECKIN_URL");
        if (ci != NULL && ci[0] != '\0') {
            opts->checkin_url = ci;
        }
    }
}

/* Create a context from the CLI connection options. Returns 0 and writes *out
 * on success; nonzero exit code otherwise (message already printed). */
static int make_ctx(const cli_opts *o, ehem_ctx **out)
{
    ehem_options opts;
    ehem_rc rc;

    if (o->url == NULL || o->url[0] == '\0') {
        fprintf(stderr, "error: no device URL - pass --url or set EHEM_URL\n");
        return 2;
    }
    fill_opts(o, &opts);

    rc = ehem_ctx_create(o->url, &opts, out);
    if (rc != EHEM_OK) {
        fprintf(stderr, "error: invalid URL or options: %s\n", ehem_rc_str(rc));
        return 1;
    }
    return 0;
}

static int cmd_status(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    ehem_status_info *st = NULL;
    ehem_version_info *ver = NULL;
    ehem_rc rc;
    size_t i;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    rc = ehem_system_status(ctx, &st);
    if (rc != EHEM_OK) {
        print_last_error(ctx, rc, "system status");
        ehem_ctx_destroy(ctx);
        return 1;
    }
    rc = ehem_system_version(ctx, &ver);
    if (rc != EHEM_OK) {
        print_last_error(ctx, rc, "system version");
        ehem_system_status_free(st);
        ehem_ctx_destroy(ctx);
        return 1;
    }

    printf("Device: %s\n", o->url);
    if (st->hostname != NULL) {
        printf("  hostname:   %s\n", st->hostname);
    }
    printf("  uptime:     %lld s\n", (long long)st->uptime);
    printf("  temp:       %.1f C\n", st->temp);
    if (st->storage_count > 0) {
        printf("  storage:    ");
        for (i = 0; i < st->storage_count; i++) {
            printf("%s%s", (i > 0) ? ", " : "", st->storage[i]);
        }
        printf("\n");
    }
    if (st->has_inited) {
        printf("  inited:     %s\n", st->inited ? "yes" : "no");
    }
    if (st->has_https) {
        printf("  https:      %s\n", st->https ? "yes" : "no");
    }
    printf("  hardware:   %s\n", ver->hwv);
    printf("  firmware:   %s\n", ver->fwv);
    printf("  bootloader: %s\n", ver->blv);

    print_cert_notice(ctx);
    ehem_system_version_free(ver);
    ehem_system_status_free(st);
    ehem_ctx_destroy(ctx);
    return 0;
}

static int cmd_checkin(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    ehem_checkin_info *res = NULL;
    ehem_rc rc;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    rc = ehem_system_checkin(ctx, &res);
    if (rc != EHEM_OK) {
        print_last_error(ctx, rc, "check-in");
        ehem_ctx_destroy(ctx);
        return 1;
    }

    printf("Check-in with %s completed\n", o->url);
    if (res->status != NULL) {
        printf("  status:           %s\n", res->status);
    }
    printf("  cert refreshed:   %s\n", res->cert_updated ? "yes" : "no");
    if (res->newfws != NULL && res->newfws[0] != '\0') {
        printf("  firmware update:  available (%s)\n", res->newfws);
    }
    if (res->newuis != NULL && res->newuis[0] != '\0') {
        printf("  manager update:   available (%s)\n", res->newuis);
    }

    ehem_checkin_result_free(res);
    ehem_ctx_destroy(ctx);
    return 0;
}

/* REQ-TOOL-003: harvest the cloud-delivered certificate and install it on a
 * device whose firmware cannot apply it itself, then reboot and verify. */
static int cmd_cert_install(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_cert_install_opts co;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&co, 0, sizeof co);
    co.passphrase    = o->passphrase;
    co.mobile       = o->mobile;
    co.force         = o->force;
    co.insecure      = o->insecure;
    co.poll_attempts = HEM_CERT_DEFAULT_POLL_ATTEMPTS;
    co.poll_delay_ms = HEM_CERT_DEFAULT_POLL_DELAY_MS;   /* real wait between polls */
    co.out           = stdout;
    co.err           = stderr;

    ret = hem_cert_install_run(ctx, &co);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-004: read-only inventory of the device's keys, protected keys
 * marked. The listing/marking lives in hem-tool-core (shared with keys rm). */
static int cmd_keys_list(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_keys_opts ko;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&ko, 0, sizeof ko);
    ko.passphrase = o->passphrase;
    ko.mobile     = o->mobile;
    ko.out        = stdout;
    ko.err        = stderr;

    ret = hem_keys_list_run(ctx, &ko);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-006: delete keys with the protected-key guard; the selection,
 * partition, prompts, and deletion live in hem-tool-core (shared with tests). */
static int cmd_keys_rm(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_keys_rm_opts ko;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&ko, 0, sizeof ko);
    ko.passphrase   = o->passphrase;
    ko.mobile       = o->mobile;
    ko.all          = o->all;
    ko.prefixes     = o->prefixes;
    ko.prefix_count = o->prefix_count;
    ko.dry_run      = o->dry_run;
    ko.assume_yes   = o->assume_yes;
    ko.out          = stdout;
    ko.err          = stderr;
    ko.in           = stdin;

    ret = hem_keys_rm_run(ctx, &ko);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-007: read-only public material + typed metadata for one key; the
 * fetch/format logic lives in hem-tool-core (shared with the unit test). */
static int cmd_keys_pub(const cli_opts *o, const char *kid)
{
    ehem_ctx *ctx = NULL;
    hem_keys_pub_opts ko;
    int ret;

    if (o->hex && o->raw) {
        fprintf(stderr, "error: --hex and --raw are mutually exclusive\n");
        return 2;
    }

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&ko, 0, sizeof ko);
    ko.passphrase = o->passphrase;
    ko.mobile     = o->mobile;
    ko.kid        = kid;
    ko.format     = o->raw ? HEM_KEYS_PUB_RAW
                           : (o->hex ? HEM_KEYS_PUB_HEX : HEM_KEYS_PUB_B64);
    ko.out        = stdout;
    ko.err        = stderr;

    ret = hem_keys_pub_run(ctx, &ko);
    if (ko.format != HEM_KEYS_PUB_RAW) {
        print_cert_notice(ctx);       /* raw mode keeps stdout bytes-only */
    }
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-008: sign a message with a device key; the input handling,
 * default-alg lookup, and formatting live in hem-tool-core. */
static int cmd_sign(const cli_opts *o, const char *kid)
{
    ehem_ctx *ctx = NULL;
    hem_sign_opts so;
    int ret;

    if (o->hex && o->raw) {
        fprintf(stderr, "error: --hex and --raw are mutually exclusive\n");
        return 2;
    }

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&so, 0, sizeof so);
    so.passphrase = o->passphrase;
    so.mobile     = o->mobile;
    so.kid        = kid;
    so.alg        = o->alg;
    so.in_path    = o->in_path;
    so.sigctx     = o->sigctx;
    so.format     = o->raw ? HEM_SIGN_OUT_RAW
                           : (o->hex ? HEM_SIGN_OUT_HEX : HEM_SIGN_OUT_B64);
    so.out        = stdout;
    so.err        = stderr;
    so.in         = stdin;

    ret = hem_sign_run(ctx, &so);
    if (so.format != HEM_SIGN_OUT_RAW) {
        print_cert_notice(ctx);       /* raw mode keeps stdout bytes-only */
    }
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-010: device hardware-RNG bytes; the count parsing, transient-key
 * orchestration, and formatting live in hem-tool-core. */
static int cmd_random(const cli_opts *o, const char *count_arg)
{
    ehem_ctx *ctx = NULL;
    hem_random_opts ro;
    int ret;

    if (o->hex && o->raw) {
        fprintf(stderr, "error: --hex and --raw are mutually exclusive\n");
        return 2;
    }

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&ro, 0, sizeof ro);
    ro.passphrase = o->passphrase;
    ro.mobile     = o->mobile;
    ro.count_arg  = count_arg;
    ro.kid        = o->kid;
    ro.raw        = o->raw;
    ro.out        = stdout;
    ro.err        = stderr;

    ret = hem_random_run(ctx, &ro);
    if (!ro.raw) {
        print_cert_notice(ctx);       /* raw mode keeps stdout bytes-only */
    }
    ehem_ctx_destroy(ctx);
    return ret;
}

/* REQ-TOOL-009: generate a key on the device; the mode default and error
 * mapping live in hem-tool-core. `type` is the third positional (TYPE). */
static int cmd_keys_gen(const cli_opts *o, const char *type)
{
    ehem_ctx *ctx = NULL;
    hem_keys_gen_opts ko;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&ko, 0, sizeof ko);
    ko.passphrase = o->passphrase;
    ko.mobile     = o->mobile;
    ko.type       = type;
    ko.label      = o->label;
    ko.descr      = o->descr;
    ko.mode       = o->mode;
    ko.out        = stdout;
    ko.err        = stderr;

    ret = hem_keys_gen_run(ctx, &ko);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

static int cmd_keys_update(const cli_opts *o, const char *kid)
{
    ehem_ctx *ctx = NULL;
    hem_keys_update_opts uo;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    memset(&uo, 0, sizeof uo);
    uo.passphrase = o->passphrase;
    uo.mobile     = o->mobile;
    uo.kid        = kid;
    uo.label      = o->label;
    uo.descr      = o->descr;
    uo.assume_yes = o->assume_yes;
    uo.out        = stdout;
    uo.err        = stderr;
    uo.in         = stdin;

    ret = hem_keys_update_run(ctx, &uo);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* Dispatch the `keys` command group (list / pub / gen / rm / update). */
static int cmd_keys(const cli_opts *o, const char *subcmd, const char *arg)
{
    if (subcmd == NULL) {
        fprintf(stderr, "error: 'keys' needs a subcommand "
                        "(list, pub, gen, rm, update, types)\n");
        usage(stderr);
        return 2;
    }
    /* Only `pub` (KID), `gen` (TYPE) and `update` (KID) take a positional. */
    if (arg != NULL && strcmp(subcmd, "pub") != 0 &&
        strcmp(subcmd, "gen") != 0 && strcmp(subcmd, "update") != 0) {
        fprintf(stderr, "error: unexpected argument '%s'\n", arg);
        usage(stderr);
        return 2;
    }
    if (strcmp(subcmd, "list") == 0) {
        return cmd_keys_list(o);
    }
    if (strcmp(subcmd, "pub") == 0) {
        return cmd_keys_pub(o, arg);
    }
    if (strcmp(subcmd, "gen") == 0) {
        return cmd_keys_gen(o, arg);
    }
    if (strcmp(subcmd, "rm") == 0) {
        return cmd_keys_rm(o);
    }
    if (strcmp(subcmd, "update") == 0) {
        return cmd_keys_update(o, arg);
    }
    fprintf(stderr, "error: unknown keys subcommand '%s'\n", subcmd);
    usage(stderr);
    return 2;
}

/* Dispatch the `logs` command group (list / get / key) — REQ-TOOL-012. */
static int cmd_logs(const cli_opts *o, const char *subcmd, const char *arg)
{
    ehem_ctx *ctx = NULL;
    hem_logs_opts lo;
    int ret;

    if (subcmd == NULL) {
        fprintf(stderr, "error: 'logs' needs a subcommand (list, get, key)\n");
        usage(stderr);
        return 2;
    }
    if (arg != NULL && strcmp(subcmd, "get") != 0) {
        fprintf(stderr, "error: unexpected argument '%s'\n", arg);
        usage(stderr);
        return 2;
    }

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }
    memset(&lo, 0, sizeof lo);
    lo.passphrase = o->passphrase;
    lo.mobile     = o->mobile;
    lo.out        = stdout;
    lo.err        = stderr;

    if (strcmp(subcmd, "list") == 0) {
        ret = hem_logs_list_run(ctx, &lo);
    } else if (strcmp(subcmd, "get") == 0) {
        ret = hem_logs_get_run(ctx, &lo, arg, o->out_path);
    } else if (strcmp(subcmd, "key") == 0) {
        ret = hem_logs_key_run(ctx, &lo);
    } else {
        fprintf(stderr, "error: unknown logs subcommand '%s'\n", subcmd);
        usage(stderr);
        ret = 2;
    }
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* `reboot [--wait]` — REQ-TOOL-014 over the REQ-SYS-005 binding. With
 * --wait, poll status until the device answers again (bounded ~90 s; the
 * loop is paced by each probe's short connect timeout — no sleep needed). */
static int cmd_reboot(const cli_opts *o, int wait_back)
{
    ehem_ctx *ctx = NULL;
    ehem_rc rc;
    int ret;

    if (!o->mobile && (o->passphrase == NULL || o->passphrase[0] == '\0')) {
        fprintf(stderr, "error: no passphrase - pass --passphrase / set "
                        "EHEM_PASSPHRASE, or use --mobile\n");
        return 2;
    }
    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }
    rc = hem_tool_login(ctx, o->passphrase, o->mobile != 0, stderr);
    if (rc == EHEM_OK) {
        rc = ehem_system_reboot(ctx);
    }
    if (rc != EHEM_OK) {
        print_last_error(ctx, rc, "reboot");
        ehem_ctx_destroy(ctx);
        return hem_tool_auth_exit(rc, stderr, 1);
    }
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    fprintf(stderr, "reboot accepted - the device restarts now\n");

    if (!wait_back) {
        fprintf(stderr, "it should answer again in ~30-60 s "
                        "(use --wait to block until then)\n");
        return 0;
    }

    /* Two phases, probes spaced ≥ 1 s by the SDK's own request pacing: first
     * wait for the OLD instance to stop answering (the firmware serves for
     * ~2 s after accepting the reboot), then wait for status to answer again. */
    {
        time_t start = time(NULL);
        time_t deadline = start + 180;
        int attempts = 0;
        int seen_down = 0;
        for (;;) {
            ehem_options opts;
            ehem_ctx *probe = NULL;
            ehem_status_info *st = NULL;
            int up = 0;
            ehem_options_init(&opts);
            if (o->insecure) {
                opts.tls_mode = EHEM_TLS_INSECURE;
            } else if (o->cacert != NULL) {
                opts.tls_mode = EHEM_TLS_CA_FILE;
                opts.ca_file  = o->cacert;
            }
            opts.connect_timeout_ms = 3000;
            opts.total_timeout_ms   = 4000;
            opts.request_pace_ms    = 1000;
            if (ehem_ctx_create(o->url, &opts, &probe) == EHEM_OK &&
                ehem_system_status(probe, &st) == EHEM_OK) {
                ehem_system_status_free(st);
                up = 1;
            }
            ehem_ctx_destroy(probe);
            if (up && seen_down) {
                fprintf(stderr, "device back after ~%ld s\n",
                        (long)(time(NULL) - start));
                return 0;
            }
            if (!up) {
                seen_down = 1;
            }
            attempts++;
            if (time(NULL) >= deadline || attempts >= 180) {
                fprintf(stderr, "error: device did not return within ~180 s\n");
                return 1;
            }
        }
    }
}

/* `tls-recover [--force]` — REQ-TOOL-015. */
/* `ext pair|list|login` (REQ-TOOL-016). */
static int cmd_ext(const cli_opts *o, const char *subcmd)
{
    ehem_ctx *ctx = NULL;
    int ret;

    if (subcmd == NULL) {
        fprintf(stderr, "error: ext needs a subcommand (pair | list | login)\n");
        usage(stderr);
        return 2;
    }
    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }

    if (strcmp(subcmd, "pair") == 0) {
        hem_ext_pair_opts po;
        memset(&po, 0, sizeof po);
        po.passphrase = o->passphrase;
        po.mobile     = o->mobile;
        po.notify_url = o->notify_url;
        po.no_qr      = o->no_qr;
        ret = hem_ext_pair_run(ctx, &po);
    } else if (strcmp(subcmd, "list") == 0) {
        ret = hem_ext_list_run(ctx, o->passphrase, o->mobile != 0, NULL, NULL);
    } else if (strcmp(subcmd, "login") == 0) {
        hem_ext_login_opts lo;
        memset(&lo, 0, sizeof lo);
        lo.scope      = o->scope;
        lo.note       = o->note;
        lo.passphrase = o->passphrase;
        lo.timeout_ms = (o->timeout_sec > 0) ? o->timeout_sec * 1000L : 0;
        ret = hem_ext_login_run(ctx, &lo);
    } else {
        fprintf(stderr, "error: unknown ext subcommand '%s'\n", subcmd);
        usage(stderr);
        ret = 2;
    }

    ehem_ctx_destroy(ctx);
    return ret;
}

/* `init-device ...` — REQ-TOOL-021 (attended-only, REQ-TEST-007). */
static int cmd_init_device(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_init_opts io;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }
    memset(&io, 0, sizeof io);
    io.passphrase         = o->passphrase;
    io.master_words       = o->master_words != NULL ? o->master_words
                                                    : getenv("EHEM_MASTER_WORDS");
    io.master_hex         = o->master_hex;
    io.master_generate    = o->master_generate;
    io.user               = o->user;
    io.email              = o->email;
    io.hostname           = o->hostname;
    io.ip                 = o->ip;
    io.storage_mode       = o->storage_mode;
    io.disk0_size         = o->disk0_size;
    io.origin             = o->origin;
    io.dnsd               = o->dnsd;
    io.no_trusted_ts      = o->no_trusted_ts;
    io.no_trusted_backend = o->no_trusted_backend;
    io.no_allow_keysearch = o->no_allow_keysearch;
    io.gen_csr            = o->gen_csr;
    io.ctx_id             = o->ctx_id;
    io.csr_out            = o->csr_out;
    io.reboot             = o->reboot_flag;
    io.poll_delay_ms      = HEM_INIT_POLL_DELAY_MS;
    io.out                = stdout;
    io.err                = stderr;

    ret = hem_init_device_run(ctx, &io);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* `init-wizard` — REQ-TOOL-028 (attended-only, REQ-TEST-007). The wizard asks
 * for every input itself; --url / EHEM_URL is its only pre-fill (settled at
 * STEP-M12-050: nothing secret from the environment in an interactive flow).
 * init-device's input flags are refused, and secrets in the environment are
 * ignored with a notice. */
static int cmd_init_wizard(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_wizard_opts wo;
    int ret;

    if (o->pw_flag || o->user != NULL || o->email != NULL ||
        o->hostname != NULL || o->ip != NULL || o->origin != NULL ||
        o->master_words != NULL || o->master_hex != NULL ||
        o->master_generate || o->csr_out != NULL || o->storage_mode != 0 ||
        o->disk0_size != 0 || o->dnsd || o->no_trusted_ts ||
        o->no_trusted_backend || o->no_allow_keysearch || o->gen_csr ||
        o->ctx_id != 0 || o->reboot_flag) {
        fprintf(stderr, "error: init-wizard asks for every value itself - "
                        "for a scripted run with flags use init-device\n");
        return 2;
    }
    if ((o->passphrase != NULL && o->passphrase[0] != '\0') ||
        getenv("EHEM_MASTER_WORDS") != NULL) {
        fprintf(stderr, "notice: init-wizard ignores EHEM_PASSPHRASE and "
                        "EHEM_MASTER_WORDS - it asks for both\n");
    }
    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }
    memset(&wo, 0, sizeof wo);
    wo.url           = o->url;
    wo.in            = stdin;
    wo.out           = stdout;
    wo.err           = stderr;
    wo.poll_delay_ms = HEM_INIT_POLL_DELAY_MS;
    ret = hem_init_wizard_run(ctx, &wo);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* `recovery` — REQ-TOOL-023: three postures on one device. */
static int cmd_recovery(const cli_opts *o)
{
    ehem_options opts;
    ehem_ctx *https = NULL, *insecure = NULL, *http = NULL;
    hem_recovery_opts ro;
    char http_url[512];
    int ret;

    if (o->url == NULL || o->url[0] == '\0') {
        fprintf(stderr, "error: no device URL - pass --url or set EHEM_URL\n");
        return 2;
    }
    /* The probe: the configured trust, automatic recovery OFF so the TLS
     * verdict surfaces (ehem_error.tls_expired) instead of being repaired
     * behind our back. */
    fill_opts(o, &opts);
    opts.no_auto_checkin = 1;
    if (ehem_ctx_create(o->url, &opts, &https) != EHEM_OK) {
        fprintf(stderr, "error: invalid URL or options\n");
        return 1;
    }
    /* The expired-certificate install leg. */
    fill_opts(o, &opts);
    opts.tls_mode = EHEM_TLS_INSECURE;
    opts.ca_file  = NULL;
    if (ehem_ctx_create(o->url, &opts, &insecure) != EHEM_OK) {
        insecure = NULL;
    }
    /* The HTTPS-down leg. */
    if (hem_wipe_http_url(o->url, http_url, sizeof http_url)) {
        ehem_options_init(&opts);
        if (ehem_ctx_create(http_url, &opts, &http) != EHEM_OK) {
            http = NULL;
        }
    }

    memset(&ro, 0, sizeof ro);
    ro.passphrase    = o->passphrase;
    ro.mobile        = o->mobile != 0;
    ro.ctx_https     = https;
    ro.ctx_insecure  = insecure;
    ro.ctx_http      = http;
    ro.poll_delay_ms = HEM_RECOVERY_POLL_DELAY_MS;
    ro.out           = stdout;
    ro.err           = stderr;

    ret = hem_recovery_run(&ro);
    ehem_ctx_destroy(http);
    ehem_ctx_destroy(insecure);
    ehem_ctx_destroy(https);
    return ret;
}

/* `wipe-device [--wait]` — REQ-TOOL-022 (attended-only, REQ-TEST-007). */
static int cmd_wipe_device(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    ehem_ctx *probe = NULL;
    hem_wipe_opts wo;
    char http_url[512];
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }
    /* The post-wipe poll goes over http:// — the TLS material is gone. */
    if (o->wait_back && hem_wipe_http_url(o->url, http_url, sizeof http_url)) {
        ehem_options opts;
        ehem_options_init(&opts);
        if (ehem_ctx_create(http_url, &opts, &probe) != EHEM_OK) {
            probe = NULL;
        }
    }
    memset(&wo, 0, sizeof wo);
    wo.passphrase    = o->passphrase;
    wo.mobile        = o->mobile != 0;
    wo.wait_back     = o->wait_back;
    wo.poll_delay_ms = HEM_WIPE_POLL_DELAY_MS;
    wo.probe         = probe;
    wo.in            = stdin;
    wo.out           = stdout;
    wo.err           = stderr;

    ret = hem_wipe_device_run(ctx, &wo);
    ehem_ctx_destroy(probe);
    ehem_ctx_destroy(ctx);
    return ret;
}

static int cmd_tls_recover(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_recover_opts ro;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }
    memset(&ro, 0, sizeof ro);
    ro.passphrase   = o->passphrase;
    ro.mobile       = o->mobile;
    ro.force        = o->force;
    ro.poll_delay_ms = HEM_RECOVER_POLL_DELAY_MS;
    ro.out          = stdout;
    ro.err          = stderr;

    ret = hem_tls_recover_run(ctx, &ro);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

/* `selftest` — REQ-TOOL-013. */
static int cmd_selftest(const cli_opts *o)
{
    ehem_ctx *ctx = NULL;
    hem_selftest_opts so;
    int ret;

    ret = make_ctx(o, &ctx);
    if (ret != 0) {
        return ret;
    }
    memset(&so, 0, sizeof so);
    so.passphrase = o->passphrase;
    so.mobile     = o->mobile;
    so.out        = stdout;
    so.err        = stderr;

    ret = hem_selftest_run(ctx, &so);
    print_cert_notice(ctx);
    ehem_ctx_destroy(ctx);
    return ret;
}

int main(int argc, char **argv)
{
    cli_opts o;
    const char *cmd = NULL;
    const char *subcmd = NULL;
    const char *arg = NULL;
    int i;
    int ret;

    memset(&o, 0, sizeof o);
    /* o.url stays flag-only; env + default resolve AFTER parsing
     * (hem_tool_resolve_url, REQ-TOOL-017). */
    o.passphrase = getenv("EHEM_PASSPHRASE");  /* --passphrase overrides below */

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--url") == 0 && i + 1 < argc) {
            o.url = argv[++i];
        } else if (strncmp(a, "--url=", 6) == 0) {
            o.url = a + 6;
        } else if (strcmp(a, "--cacert") == 0 && i + 1 < argc) {
            o.cacert = argv[++i];
        } else if (strncmp(a, "--cacert=", 9) == 0) {
            o.cacert = a + 9;
        } else if (strcmp(a, "--passphrase") == 0 && i + 1 < argc) {
            o.passphrase = argv[++i];
            o.pw_flag = 1;
        } else if (strncmp(a, "--passphrase=", 13) == 0) {
            o.passphrase = a + 13;
            o.pw_flag = 1;
        } else if (strcmp(a, "--mobile") == 0) {
            o.mobile = 1;
        } else if (strcmp(a, "--force") == 0) {
            o.force = 1;
        } else if (strcmp(a, "--all") == 0) {
            o.all = 1;
        } else if (strcmp(a, "--dry-run") == 0) {
            o.dry_run = 1;
        } else if (strcmp(a, "--yes") == 0) {
            o.assume_yes = 1;
        } else if (strcmp(a, "--label-prefix") == 0 && i + 1 < argc) {
            if (o.prefix_count >= MAX_LABEL_PREFIXES) {
                fprintf(stderr, "error: too many --label-prefix (max %d)\n",
                        MAX_LABEL_PREFIXES);
                return 2;
            }
            o.prefixes[o.prefix_count++] = argv[++i];
        } else if (strncmp(a, "--label-prefix=", 15) == 0) {
            if (o.prefix_count >= MAX_LABEL_PREFIXES) {
                fprintf(stderr, "error: too many --label-prefix (max %d)\n",
                        MAX_LABEL_PREFIXES);
                return 2;
            }
            o.prefixes[o.prefix_count++] = a + 15;
        } else if (strcmp(a, "--alg") == 0 && i + 1 < argc) {
            o.alg = argv[++i];
        } else if (strncmp(a, "--alg=", 6) == 0) {
            o.alg = a + 6;
        } else if (strcmp(a, "--in") == 0 && i + 1 < argc) {
            o.in_path = argv[++i];
        } else if (strncmp(a, "--in=", 5) == 0) {
            o.in_path = a + 5;
        } else if (strcmp(a, "--out") == 0 && i + 1 < argc) {
            o.out_path = argv[++i];
        } else if (strncmp(a, "--out=", 6) == 0) {
            o.out_path = a + 6;
        } else if (strcmp(a, "--no-qr") == 0) {
            o.no_qr = 1;
        } else if (strcmp(a, "--scope") == 0 && i + 1 < argc) {
            o.scope = argv[++i];
        } else if (strncmp(a, "--scope=", 8) == 0) {
            o.scope = a + 8;
        } else if (strcmp(a, "--note") == 0 && i + 1 < argc) {
            o.note = argv[++i];
        } else if (strncmp(a, "--note=", 7) == 0) {
            o.note = a + 7;
        } else if (strcmp(a, "--timeout") == 0 && i + 1 < argc) {
            o.timeout_sec = atol(argv[++i]);
        } else if (strncmp(a, "--timeout=", 10) == 0) {
            o.timeout_sec = atol(a + 10);
        } else if (strcmp(a, "--notify-url") == 0 && i + 1 < argc) {
            o.notify_url = argv[++i];
        } else if (strncmp(a, "--notify-url=", 13) == 0) {
            o.notify_url = a + 13;
        } else if (strcmp(a, "--sigctx") == 0 && i + 1 < argc) {
            o.sigctx = argv[++i];
        } else if (strncmp(a, "--sigctx=", 9) == 0) {
            o.sigctx = a + 9;
        } else if (strcmp(a, "--label") == 0 && i + 1 < argc) {
            o.label = argv[++i];
        } else if (strncmp(a, "--label=", 8) == 0) {
            o.label = a + 8;
        } else if (strcmp(a, "--descr") == 0 && i + 1 < argc) {
            o.descr = argv[++i];
        } else if (strncmp(a, "--descr=", 8) == 0) {
            o.descr = a + 8;
        } else if (strcmp(a, "--mode") == 0 && i + 1 < argc) {
            o.mode = argv[++i];
        } else if (strncmp(a, "--mode=", 7) == 0) {
            o.mode = a + 7;
        } else if (strcmp(a, "--kid") == 0 && i + 1 < argc) {
            o.kid = argv[++i];
        } else if (strncmp(a, "--kid=", 6) == 0) {
            o.kid = a + 6;
        } else if (strcmp(a, "--wait") == 0) {
            o.wait_back = 1;
        } else if (strcmp(a, "--user") == 0 && i + 1 < argc) {
            o.user = argv[++i];
        } else if (strncmp(a, "--user=", 7) == 0) {
            o.user = a + 7;
        } else if (strcmp(a, "--email") == 0 && i + 1 < argc) {
            o.email = argv[++i];
        } else if (strncmp(a, "--email=", 8) == 0) {
            o.email = a + 8;
        } else if (strcmp(a, "--hostname") == 0 && i + 1 < argc) {
            o.hostname = argv[++i];
        } else if (strncmp(a, "--hostname=", 11) == 0) {
            o.hostname = a + 11;
        } else if (strcmp(a, "--ip") == 0 && i + 1 < argc) {
            o.ip = argv[++i];
        } else if (strncmp(a, "--ip=", 5) == 0) {
            o.ip = a + 5;
        } else if (strcmp(a, "--origin") == 0 && i + 1 < argc) {
            o.origin = argv[++i];
        } else if (strncmp(a, "--origin=", 9) == 0) {
            o.origin = a + 9;
        } else if (strcmp(a, "--master-words") == 0 && i + 1 < argc) {
            o.master_words = argv[++i];
        } else if (strncmp(a, "--master-words=", 15) == 0) {
            o.master_words = a + 15;
        } else if (strcmp(a, "--master-secret-hex") == 0 && i + 1 < argc) {
            o.master_hex = argv[++i];
        } else if (strncmp(a, "--master-secret-hex=", 20) == 0) {
            o.master_hex = a + 20;
        } else if (strcmp(a, "--master-generate") == 0) {
            o.master_generate = 1;
        } else if (strcmp(a, "--csr-out") == 0 && i + 1 < argc) {
            o.csr_out = argv[++i];
        } else if (strncmp(a, "--csr-out=", 10) == 0) {
            o.csr_out = a + 10;
        } else if (strcmp(a, "--storage-mode") == 0 && i + 1 < argc) {
            o.storage_mode = atoi(argv[++i]);
        } else if (strncmp(a, "--storage-mode=", 15) == 0) {
            o.storage_mode = atoi(a + 15);
        } else if (strcmp(a, "--disk0-size") == 0 && i + 1 < argc) {
            o.disk0_size = atoll(argv[++i]);
        } else if (strncmp(a, "--disk0-size=", 13) == 0) {
            o.disk0_size = atoll(a + 13);
        } else if (strcmp(a, "--ctx") == 0 && i + 1 < argc) {
            o.ctx_id = atoi(argv[++i]);
        } else if (strncmp(a, "--ctx=", 6) == 0) {
            o.ctx_id = atoi(a + 6);
        } else if (strcmp(a, "--dnsd") == 0) {
            o.dnsd = 1;
        } else if (strcmp(a, "--no-trusted-ts") == 0) {
            o.no_trusted_ts = 1;
        } else if (strcmp(a, "--no-trusted-backend") == 0) {
            o.no_trusted_backend = 1;
        } else if (strcmp(a, "--no-allow-keysearch") == 0) {
            o.no_allow_keysearch = 1;
        } else if (strcmp(a, "--gen-csr") == 0) {
            o.gen_csr = 1;
        } else if (strcmp(a, "--reboot") == 0) {
            o.reboot_flag = 1;
        } else if (strcmp(a, "--insecure") == 0) {
            o.insecure = 1;
        } else if (strcmp(a, "--hex") == 0) {
            o.hex = 1;
        } else if (strcmp(a, "--raw") == 0) {
            o.raw = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            o.want_help = 1;   /* deferred: per-command help needs the command */
        } else if (strcmp(a, "--version") == 0) {
            o.want_version = 1;
        } else if (a[0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", a);
            usage(stderr);
            return 2;
        } else if (cmd == NULL) {
            cmd = a;
        } else if (subcmd == NULL) {
            subcmd = a;                 /* e.g. the `list` in `keys list` */
        } else if (arg == NULL) {
            arg = a;                    /* e.g. the KID in `keys pub KID` */
        } else {
            fprintf(stderr, "error: unexpected argument '%s'\n", a);
            usage(stderr);
            return 2;
        }
    }

    if (o.cacert != NULL && o.insecure) {
        fprintf(stderr, "error: --cacert and --insecure are mutually exclusive\n");
        return 2;
    }
    if (o.mobile && o.pw_flag) {
        /* implements: REQ-TOOL-018 — the explicit flags conflict; a
         * passphrase from the ENVIRONMENT is simply outranked by --mobile
         * (hem.env is commonly auto-sourced). */
        fprintf(stderr,
                "error: --mobile and --passphrase are mutually exclusive\n");
        return 2;
    }
    if (o.pw_flag) {
        /* implements: REQ-TOOL-020 rev 2 — the flag is deprecated (user
         * decision 2026-10-19): once, on stderr, whenever it is used. */
        fprintf(stderr,
                "warning: --passphrase is deprecated and will be removed in a "
                "later release -\n"
                "         the passphrase stays in your shell history and shows "
                "in the process\n"
                "         list; set EHEM_PASSPHRASE instead\n");
    }

    /* Everything up to the ehem_global_init() below runs WITHOUT the SDK
     * initialised, so it works on any crypto library (REQ-TOOL-024): the
     * version report and the help pages. */

    /* REQ-TOOL-025: `--version` anywhere (it wins over a command, like
     * --help) or the `version` command spelling. */
    if (o.want_version || (cmd != NULL && strcmp(cmd, "version") == 0)) {
        if (!o.want_version && subcmd != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", subcmd);
            usage(stderr);
            return 2;
        }
#ifdef HEM_TOOL_DELAYLOAD
        hem_tool_delayload_tolerate(1);
#endif
        hem_tool_print_version(stdout);
        return 0;
    }

    /* REQ-TOOL-020: `hem-tool help [<command> [<sub>]]` and the deferred
     * `<command> --help` spelling — both render from the registry. */
    if (cmd != NULL && strcmp(cmd, "help") == 0) {
        if (subcmd == NULL) {
            usage(stdout);
            return 0;
        }
        if (hem_help_command(stdout, subcmd, arg) != 0) {
            fprintf(stderr, "error: unknown command '%s%s%s'\n",
                    subcmd, arg != NULL ? " " : "", arg != NULL ? arg : "");
            return 2;
        }
        return 0;
    }
    if (o.want_help) {
        if (cmd == NULL) {
            usage(stdout);
            return 0;
        }
        if (hem_help_command(stdout, cmd, subcmd) != 0) {
            fprintf(stderr, "error: unknown command '%s%s%s'\n",
                    cmd, subcmd != NULL ? " " : "",
                    subcmd != NULL ? subcmd : "");
            return 2;
        }
        return 0;
    }

    /* REQ-TOOL-027: `keys types [TYPE]` is local documentation — no SDK
     * initialisation, no URL, no credentials, so it works offline and on any
     * crypto library (REQ-TOOL-024). */
    if (cmd != NULL && strcmp(cmd, "keys") == 0 &&
        subcmd != NULL && strcmp(subcmd, "types") == 0) {
        return hem_keys_types_run(arg, stdout, stderr);
    }

    if (cmd == NULL) {
        usage(stderr);
        return 2;
    }

    /* REQ-TOOL-019 rev 2: the registry's auth class is enforced here, before
     * any traffic — a PASSPHRASE_ONLY command (ext pair, cert-install,
     * tls-recover, wipe-device, recovery) refuses --mobile up front. A
     * top-level command's second word is a positional argument (sign KID),
     * so try the bare name first. A usage error, so it is reported whatever
     * the state of the crypto library. */
    {
        const hem_command *rc_cmd = hem_registry_find(cmd, NULL);
        if (rc_cmd == NULL) {
            rc_cmd = hem_registry_find(cmd, subcmd);
        }
        if (hem_tool_check_auth_class(rc_cmd, o.mobile != 0, stderr) != 0) {
            return 2;
        }
    }

    /* REQ-TOOL-024: the first SDK call of every command — before any other
     * output (the URL notice below included) or traffic. A refused crypto
     * library gets one message and exit 12 here instead of crashing inside
     * the first context; the SDK's own implicit inits are idempotent no-ops
     * after this. */
    ret = hem_tool_global_init(stderr);
    if (ret != 0) {
        return ret;
    }

    /* REQ-TOOL-017: --url > EHEM_URL > the built-in default (with a
     * one-line stderr notice when the default is used). */
    o.url = hem_tool_resolve_url(o.url, getenv("EHEM_URL"), stderr);

    if (strcmp(cmd, "status") == 0) {
        ret = cmd_status(&o);
    } else if (strcmp(cmd, "checkin") == 0) {
        ret = cmd_checkin(&o);
    } else if (strcmp(cmd, "cert-install") == 0) {
        ret = cmd_cert_install(&o);
    } else if (strcmp(cmd, "keys") == 0) {
        ret = cmd_keys(&o, subcmd, arg);
    } else if (strcmp(cmd, "logs") == 0) {
        ret = cmd_logs(&o, subcmd, arg);
    } else if (strcmp(cmd, "ext") == 0) {
        ret = cmd_ext(&o, subcmd);
    } else if (strcmp(cmd, "reboot") == 0) {
        if (subcmd != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", subcmd);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_reboot(&o, o.wait_back);
        }
    } else if (strcmp(cmd, "recovery") == 0) {
        if (subcmd != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", subcmd);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_recovery(&o);
        }
    } else if (strcmp(cmd, "init-device") == 0) {
        if (subcmd != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", subcmd);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_init_device(&o);
        }
    } else if (strcmp(cmd, "init-wizard") == 0) {
        if (subcmd != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", subcmd);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_init_wizard(&o);
        }
    } else if (strcmp(cmd, "wipe-device") == 0) {
        if (subcmd != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", subcmd);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_wipe_device(&o);
        }
    } else if (strcmp(cmd, "tls-recover") == 0) {
        if (subcmd != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", subcmd);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_tls_recover(&o);
        }
    } else if (strcmp(cmd, "selftest") == 0) {
        if (subcmd != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", subcmd);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_selftest(&o);
        }
    } else if (strcmp(cmd, "sign") == 0) {
        if (arg != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", arg);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_sign(&o, subcmd);   /* subcmd is the KID */
        }
    } else if (strcmp(cmd, "random") == 0) {
        if (arg != NULL) {
            fprintf(stderr, "error: unexpected argument '%s'\n", arg);
            usage(stderr);
            ret = 2;
        } else {
            ret = cmd_random(&o, subcmd); /* subcmd is the byte count N */
        }
    } else {
        fprintf(stderr, "error: unknown command '%s'\n", cmd);
        usage(stderr);
        ret = 2;
    }

    ehem_global_cleanup();
    return ret;
}
