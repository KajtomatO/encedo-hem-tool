/*
 * wipe.c — hem-tool `wipe-device` (see wipe.h).
 *
 * implements: REQ-TOOL-022
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* nanosleep under -std=c99 */
#endif
#include "wipe.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "ehem/auth.h"
#include "ehem/system.h"
#include "tool_auth.h"

static void sleep_ms(unsigned ms)
{
    if (ms == 0) {
        return;
    }
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec  = ms / 1000u;
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

static void wreport(FILE *err, ehem_ctx *ctx, ehem_rc rc, const char *what)
{
    const ehem_error *e = ehem_last_error(ctx);
    fprintf(err, "error: %s: %s", what, ehem_rc_str(rc));
    if (e->http_status != 0) {
        fprintf(err, " (HTTP %ld)", e->http_status);
    }
    if (e->message[0] != '\0') {
        fprintf(err, " - %s", e->message);
    }
    fputc('\n', err);
}

/* Read one line, stripping the newline. False on EOF with nothing read. */
static bool read_line(FILE *f, char *buf, size_t cap)
{
    size_t n;
    if (fgets(buf, (int)cap, f) == NULL) {
        buf[0] = '\0';
        return false;
    }
    n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }
    return true;
}

bool hem_wipe_http_url(const char *url, char *out, size_t cap)
{
    const char *rest = url;
    size_t n;
    if (url == NULL || out == NULL || cap == 0) {
        return false;
    }
    if (strncmp(url, "https://", 8) == 0) {
        rest = url + 8;
        n = 7 + strlen(rest);
        if (n + 1 > cap) {
            return false;
        }
        memcpy(out, "http://", 7);
        memcpy(out + 7, rest, strlen(rest) + 1);
        return true;
    }
    n = strlen(url);
    if (n + 1 > cap) {
        return false;
    }
    memcpy(out, url, n + 1);
    return true;
}

int hem_wipe_device_run(ehem_ctx *ctx, const hem_wipe_opts *o)
{
    FILE *in  = (o->in  != NULL) ? o->in  : stdin;
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    unsigned attempts = (o->poll_attempts != 0) ? o->poll_attempts
                                                : HEM_WIPE_POLL_ATTEMPTS;
    ehem_ctx *probe = (o->probe != NULL) ? o->probe : ctx;
    ehem_config_info *cfg = NULL;
    char hostname[128];
    char line[256];
    ehem_rc rc;
    unsigned i;
    int seen_down = 0;

    /* Fail fast, zero traffic: the wipe is passphrase-only (the firmware
     * allows config writes for token sub "U"/"M" only — a mobile bearer
     * carries the authenticator kid), and a run that cannot authenticate
     * must not even read the identity. */
    if (o->mobile) {
        fprintf(err, "error: wipe-device cannot use --mobile - the device "
                     "accepts the wipe only from a passphrase session "
                     "(token sub=\"U\" or \"M\"); pass --passphrase / set "
                     "EHEM_PASSPHRASE\n");
        return HEM_WIPE_USAGE;
    }
    if (o->passphrase == NULL || o->passphrase[0] == '\0') {
        fprintf(err, "error: no passphrase - pass --passphrase / set "
                     "EHEM_PASSPHRASE\n");
        return HEM_WIPE_USAGE;
    }

    /* 1. Identity, so the operator confirms the RIGHT device. */
    rc = hem_tool_login(ctx, o->passphrase, false, err);
    if (rc != EHEM_OK) {
        return (rc == EHEM_ERR_ARG) ? HEM_WIPE_USAGE : HEM_WIPE_RUNTIME;
    }
    rc = ehem_system_config(ctx, &cfg);
    if (rc != EHEM_OK) {
        wreport(err, ctx, rc, "config");
        return HEM_WIPE_RUNTIME;
    }
    if (cfg->hostname == NULL || cfg->hostname[0] == '\0' ||
        strlen(cfg->hostname) >= sizeof hostname) {
        ehem_system_config_free(cfg);
        fprintf(err, "error: the device reports no usable hostname - "
                     "refusing to wipe without an identity to confirm\n");
        return HEM_WIPE_RUNTIME;
    }
    memcpy(hostname, cfg->hostname, strlen(cfg->hostname) + 1);
    fprintf(out,
            "ABOUT TO FACTORY-RESET THIS DEVICE (IRREVERSIBLE):\n"
            "  hostname:   %s\n"
            "  devid:      %s\n"
            "  instanceid: %s\n"
            "  user:       %s\n"
            "Everything on it is erased: all keys, the user and master\n"
            "passwords, the TLS key and certificate, the audit logs and the\n"
            "paired phones. It comes back UNINITIALISED over http://.\n",
            hostname,
            cfg->devid != NULL ? cfg->devid : "?",
            cfg->instanceid != NULL ? cfg->instanceid : "-",
            cfg->user != NULL ? cfg->user : "?");
    ehem_system_config_free(cfg);

    /* 2. The confirmation — the hostname, exactly. No --yes exists. */
    fprintf(out, "type the device hostname ('%s') to confirm, anything else "
                 "aborts: ", hostname);
    fflush(out);
    if (!read_line(in, line, sizeof line) || strcmp(line, hostname) != 0) {
        fprintf(out, "aborted - nothing was changed\n");
        return HEM_WIPE_DECLINED;
    }

    /* 3. The wipe (REQ-SYS-014). */
    rc = ehem_system_wipeout(ctx);
    if (rc != EHEM_OK) {
        wreport(err, ctx, rc, "wipeout");
        return hem_tool_auth_exit(rc, err, HEM_WIPE_RUNTIME);
    }
    fprintf(out, "wipe accepted: the device erases its configuration and "
                 "restarts in ~2 s.\n"
                 "next: hem-tool --url http://<host> init-device ...\n");
    if (!o->wait_back) {
        return HEM_WIPE_OK;
    }

    /* 4. Wait: first see the old instance stop answering (it keeps serving
     * for ~2 s), then see the wiped device answer again — over http://,
     * because HTTPS died with the TLS material. */
    fprintf(out, "waiting for the device to restart...\n");
    for (i = 0; i < attempts; i++) {
        ehem_status_info *st = NULL;
        sleep_ms(o->poll_delay_ms);
        if (ehem_system_status(probe, &st) != EHEM_OK) {
            seen_down = 1;
            continue;
        }
        if (seen_down) {
            fprintf(out, "device back (https: %s) - uninitialised; next: "
                         "init-device\n",
                    (st->has_https && st->https) ? "yes" : "no");
            ehem_system_status_free(st);
            return HEM_WIPE_OK;
        }
        ehem_system_status_free(st);
    }
    fprintf(err, "error: the device did not answer again within the wait - "
                 "check it (power-cycle?) and run `hem-tool --url http://<host> "
                 "status`\n");
    return HEM_WIPE_TIMEOUT;
}
