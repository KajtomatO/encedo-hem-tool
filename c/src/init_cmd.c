/*
 * init_cmd.c — hem-tool `init-device` (see init_cmd.h).
 *
 * implements: REQ-TOOL-021
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* nanosleep under -std=c99 */
#endif
#include "init_cmd.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "ehem/auth.h"
#include "ehem/system.h"

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

static void ireport(FILE *err, ehem_ctx *ctx, ehem_rc rc, const char *what)
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

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 64 hex characters → 32 bytes. */
static bool parse_hex32(const char *hex, uint8_t out[EHEM_MASTER_SECRET_SIZE])
{
    size_t i;
    if (hex == NULL || strlen(hex) != 2 * EHEM_MASTER_SECRET_SIZE) {
        return false;
    }
    for (i = 0; i < EHEM_MASTER_SECRET_SIZE; i++) {
        int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static void scrub(void *p, size_t n)
{
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) {
        *v++ = 0;
    }
}

int hem_init_device_run(ehem_ctx *ctx, const hem_init_opts *o)
{
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    unsigned attempts = (o->poll_attempts != 0) ? o->poll_attempts
                                                : HEM_INIT_POLL_ATTEMPTS;
    uint8_t secret[EHEM_MASTER_SECRET_SIZE];
    char *generated = NULL;
    ehem_init_params p;
    ehem_init_info *info = NULL;
    ehem_rc rc;
    int sources;
    int ret = HEM_INIT_OK;
    int do_reboot = o->reboot;

    /* 1. Inputs — zero traffic on a usage error. */
    if (o->passphrase == NULL || o->passphrase[0] == '\0') {
        fprintf(err, "error: no passphrase - pass --passphrase / set "
                     "EHEM_PASSPHRASE (it becomes the device's user password)\n");
        return HEM_INIT_USAGE;
    }
    sources = (o->master_words != NULL && o->master_words[0] != '\0') +
              (o->master_hex != NULL && o->master_hex[0] != '\0') +
              (o->master_generate != 0);
    if (sources != 1) {
        fprintf(err, "error: give exactly one master-secret source: "
                     "--master-words \"24 words\" (or EHEM_MASTER_WORDS), "
                     "--master-generate, or --master-secret-hex HEX\n");
        return HEM_INIT_USAGE;
    }
    if (o->user == NULL || o->user[0] == '\0' || o->email == NULL ||
        o->email[0] == '\0' || o->hostname == NULL || o->hostname[0] == '\0') {
        fprintf(err, "error: --user, --email and --hostname are required\n");
        return HEM_INIT_USAGE;
    }
    if (o->storage_mode < 0 || o->disk0_size < 0) {
        fprintf(err, "error: --storage-mode and --disk0-size must be positive\n");
        return HEM_INIT_USAGE;
    }

    /* 2. The master secret. */
    if (o->master_generate) {
        rc = ehem_mnemonic_generate(ctx, &generated);
        if (rc != EHEM_OK) {
            ireport(err, ctx, rc, "master-generate");
            return HEM_INIT_RUNTIME;
        }
        rc = ehem_master_secret_from_mnemonic(ctx, generated, secret);
        if (rc != EHEM_OK) {
            ireport(err, ctx, rc, "master-generate");
            ehem_mnemonic_free(generated);
            return HEM_INIT_RUNTIME;
        }
        fprintf(out,
                "==================== MASTER MNEMONIC - KEEP THESE 24 WORDS ====================\n"
                "%s\n"
                "===============================================================================\n"
                "They are the device's master persona (Encedo Manager's master passphrase).\n"
                "They are printed ONCE and never stored; the master key can never be rotated.\n",
                generated);
        ehem_mnemonic_free(generated);
        generated = NULL;
    } else if (o->master_words != NULL && o->master_words[0] != '\0') {
        rc = ehem_master_secret_from_mnemonic(ctx, o->master_words, secret);
        if (rc != EHEM_OK) {
            ireport(err, ctx, rc, "master-words");
            return HEM_INIT_USAGE;
        }
    } else {
        if (!parse_hex32(o->master_hex, secret)) {
            fprintf(err, "error: --master-secret-hex must be exactly 64 hex "
                         "characters (32 bytes)\n");
            return HEM_INIT_USAGE;
        }
    }

    /* 3. The init endpoints demand a set RTC: check in first (best effort —
     *    a 403 from the init itself is the authoritative verdict). */
    {
        ehem_checkin_info *ci = NULL;
        fprintf(out, "check-in: setting the device clock...\n");
        rc = ehem_system_checkin(ctx, &ci);
        ehem_checkin_result_free(ci);
        if (rc != EHEM_OK) {
            fprintf(err, "warning: check-in failed (%s) - continuing, the RTC "
                         "may already be set\n", ehem_rc_str(rc));
        }
    }

    /* 4. The init (REQ-AUTH-011). */
    ehem_init_params_init(&p);
    p.passphrase         = o->passphrase;
    p.master_secret      = secret;
    p.user               = o->user;
    p.email              = o->email;
    p.hostname           = o->hostname;
    p.ip                 = (o->ip != NULL && o->ip[0] != '\0') ? o->ip : HEM_INIT_DEFAULT_IP;
    p.storage_mode       = o->storage_mode != 0 ? o->storage_mode : HEM_INIT_DEFAULT_STORAGE_MODE;
    p.storage_disk0size  = o->disk0_size != 0 ? o->disk0_size : HEM_INIT_DEFAULT_DISK0_SIZE;
    p.origin             = o->origin;              /* NULL → "*" in the SDK */
    p.dnsd               = o->dnsd;
    p.no_trusted_ts      = o->no_trusted_ts;
    p.no_trusted_backend = o->no_trusted_backend;
    p.no_allow_keysearch = o->no_allow_keysearch;
    p.gen_csr            = o->gen_csr;
    p.ctx_id             = o->ctx_id;

    fprintf(out, "initialising %s (user '%s', ip %s, storage_mode %d, "
                 "disk0 %lld bytes)...\n",
            o->hostname, o->user, p.ip, p.storage_mode,
            (long long)p.storage_disk0size);
    rc = ehem_device_init(ctx, &p, &info);
    scrub(secret, sizeof secret);
    if (rc != EHEM_OK) {
        long st = ehem_last_error(ctx)->http_status;
        ireport(err, ctx, rc, "init");
        if (st == 406) {
            return HEM_INIT_ALREADY;
        }
        if (st == 403) {
            return HEM_INIT_RTC;
        }
        if (st == 400) {
            return HEM_INIT_CFG;
        }
        return HEM_INIT_RUNTIME;
    }

    fprintf(out, "initialised: instanceid=%s reboot_required=%s%s\n",
            info->instanceid, info->reboot_required ? "yes" : "no",
            info->genuine != NULL ? " (attestation token received)" : "");
    if (info->csr != NULL) {
        if (o->csr_out != NULL && o->csr_out[0] != '\0') {
            FILE *f = fopen(o->csr_out, "wb");
            if (f == NULL || fputs(info->csr, f) == EOF) {
                fprintf(err, "error: cannot write the CSR to '%s'\n", o->csr_out);
                if (f != NULL) {
                    fclose(f);
                }
                ret = HEM_INIT_RUNTIME;
            } else {
                fclose(f);
                fprintf(out, "csr: written to %s\n", o->csr_out);
            }
        } else {
            fprintf(out, "csr:\n%s\n", info->csr);
        }
    }
    if (info->reboot_required && o->confirm_reboot != NULL) {
        do_reboot = o->confirm_reboot(o->confirm_arg) != 0;
    }
    fprintf(out, "next: %slog in with the passphrase\n",
            info->reboot_required && !do_reboot
                ? "hem-tool reboot (required), then " : "");

    /* 5. Optional reboot + wait (the cached system:config bearer is used). */
    if (do_reboot && info->reboot_required) {
        unsigned i;
        int seen_down = 0;
        rc = ehem_system_reboot(ctx);
        if (rc != EHEM_OK) {
            ireport(err, ctx, rc, "reboot");
            ehem_init_info_free(info);
            return HEM_INIT_RUNTIME;
        }
        fprintf(out, "rebooting; waiting for the device to answer again...\n");
        for (i = 0; i < attempts; i++) {
            ehem_status_info *st = NULL;
            sleep_ms(o->poll_delay_ms);
            if (ehem_system_status(ctx, &st) != EHEM_OK) {
                seen_down = 1;
                continue;
            }
            ehem_system_status_free(st);
            if (seen_down) {
                fprintf(out, "device back\n");
                ehem_init_info_free(info);
                return ret;
            }
        }
        fprintf(err, "error: the device did not answer again within the wait "
                     "- check it and run `hem-tool status`\n");
        ehem_init_info_free(info);
        return HEM_INIT_RUNTIME;
    }
    ehem_init_info_free(info);
    return ret;
}
