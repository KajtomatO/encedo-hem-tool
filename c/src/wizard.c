/*
 * wizard.c — hem-tool `init-wizard` (see wizard.h).
 *
 * implements: REQ-TOOL-028
 *
 * The explanations and defaults are Encedo Manager's: its init form texts
 * (index.html — "Hostname and Settings", "Drives options", "Other options")
 * and the values assets/build.js sends (the cfg object: dnsd false, origin
 * "*", ctx 0; the storage layouts of layeredStorageEncrSet; the disk slider
 * in 2 MiB steps; ip 192.168.7.1/24).
 */
#include "wizard.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ehem/auth.h"
#include "init_cmd.h"
#include "prompt.h"
#include "registry.h"     /* HEM_TOOL_DEFAULT_HOSTNAME (REQ-TOOL-028 rev 3) */

#define WORD_COUNT    24
#define BACKUP_CHECKS 3

/* The storage layouts Encedo Manager offers (build.js layeredStorageEncrSet:
 * regular disk shown at start? x read-only / writable x CTR / XTS). */
static const struct {
    long long   code;
    const char *what;
} STORAGE_MODES[] = {
    { 81,  "regular disk shown at start, read-only; encrypted disk CTR" },
    { 83,  "regular disk shown at start, writable;  encrypted disk CTR" },
    { 161, "regular disk shown at start, read-only; encrypted disk XTS" },
    { 163, "regular disk shown at start, writable;  encrypted disk XTS" },
    { 80,  "regular disk hidden at start;           encrypted disk CTR" },
    { 160, "regular disk hidden at start;           encrypted disk XTS" },
};
#define STORAGE_MODE_COUNT (sizeof STORAGE_MODES / sizeof STORAGE_MODES[0])

typedef struct {
    hem_prompt pr;
    FILE      *out;
} wiz;

typedef struct {
    char      user[128];
    char      email[256];
    char      hostname[256];
    char      ip[64];
    char      origin[256];
    long long storage_mode;
    long long disk0;
    long long ctx_id;
    int       dnsd;
    int       trusted_ts;
    int       trusted_backend;
    int       allow_keysearch;
    int       gen_csr;
    char      pass[256];
    char      typed_words[1024];
    char     *generated;          /* ehem_mnemonic_generate() output */
    const char *words;            /* the mnemonic in use */
} answers;

static void scrub(void *p, size_t n)
{
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) {
        *v++ = 0;
    }
}

static void trim(char *s)
{
    size_t len = strlen(s), start = 0;
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    while (s[start] != '\0' && isspace((unsigned char)s[start])) {
        start++;
    }
    if (start > 0) {
        memmove(s, s + start, len - start + 1);
    }
}

/* One explained text field: Enter keeps the default; a field without a
 * default re-asks while empty. 0 = answered, 1 = cancelled. */
static int ask_text(wiz *w, const char *name, const char *explain,
                    const char *def, char *buf, size_t cap)
{
    char q[128];
    fprintf(w->out, "\n%s - %s\n", name, explain);
    if (def != NULL) {
        snprintf(q, sizeof q, "  %s [%s]: ", name, def);
    } else {
        snprintf(q, sizeof q, "  %s: ", name);
    }
    for (;;) {
        int rc = hem_prompt_line(&w->pr, q, buf, cap);
        if (rc == HEM_PROMPT_EOF) {
            return 1;
        }
        if (rc == HEM_PROMPT_TOO_LONG) {
            fprintf(w->out, "  too long (at most %u characters)\n",
                    (unsigned)(cap - 1));
            continue;
        }
        trim(buf);
        if (buf[0] != '\0') {
            return 0;
        }
        if (def != NULL) {
            snprintf(buf, cap, "%s", def);
            return 0;
        }
        fprintf(w->out, "  %s is required\n", name);
    }
}

/* One explained yes/no field. 0 = answered, 1 = cancelled. */
static int ask_bool(wiz *w, const char *name, const char *explain, int def,
                    int *val)
{
    char q[96], buf[16];
    fprintf(w->out, "\n%s - %s\n", name, explain);
    snprintf(q, sizeof q, "  %s [%s]: ", name, def ? "Y/n" : "y/N");
    for (;;) {
        int rc = hem_prompt_line(&w->pr, q, buf, sizeof buf);
        if (rc == HEM_PROMPT_EOF) {
            return 1;
        }
        trim(buf);
        if (rc == HEM_PROMPT_OK && buf[0] == '\0') {
            *val = def;
            return 0;
        }
        if (rc == HEM_PROMPT_OK && (strcmp(buf, "y") == 0 || strcmp(buf, "Y") == 0 ||
                                    strcmp(buf, "yes") == 0)) {
            *val = 1;
            return 0;
        }
        if (rc == HEM_PROMPT_OK && (strcmp(buf, "n") == 0 || strcmp(buf, "N") == 0 ||
                                    strcmp(buf, "no") == 0)) {
            *val = 0;
            return 0;
        }
        fprintf(w->out, "  answer y or n\n");
    }
}

/* One explained number field within [min, max]; with `storage_codes` the
 * answer must be one of Encedo Manager's layouts. 0 = answered, 1 = cancelled. */
static int ask_number(wiz *w, const char *name, const char *explain,
                      long long def, long long min, long long max,
                      int storage_codes, long long *val)
{
    char q[96], buf[32];
    fprintf(w->out, "\n%s - %s\n", name, explain);
    snprintf(q, sizeof q, "  %s [%lld]: ", name, def);
    for (;;) {
        char *end = NULL;
        long long v;
        size_t i;
        int ok = 0;
        int rc = hem_prompt_line(&w->pr, q, buf, sizeof buf);
        if (rc == HEM_PROMPT_EOF) {
            return 1;
        }
        trim(buf);
        if (rc == HEM_PROMPT_OK && buf[0] == '\0') {
            *val = def;
            return 0;
        }
        v = (rc == HEM_PROMPT_OK) ? strtoll(buf, &end, 10) : 0;
        if (rc == HEM_PROMPT_OK && end != buf && *end == '\0' && v >= min && v <= max) {
            ok = 1;
            if (storage_codes) {
                ok = 0;
                for (i = 0; i < STORAGE_MODE_COUNT; i++) {
                    ok |= (STORAGE_MODES[i].code == v);
                }
            }
        }
        if (ok) {
            *val = v;
            return 0;
        }
        if (storage_codes) {
            fprintf(w->out, "  one of: 81 83 161 163 80 160\n");
        } else {
            fprintf(w->out, "  a whole number from %lld to %lld\n", min, max);
        }
    }
}

/* The words, numbered, four per row. */
static void show_words(FILE *out, const char *words)
{
    char word[32];
    const char *p = words;
    int n = 0;

    fprintf(out, "\n========== MASTER MNEMONIC - WRITE DOWN THESE 24 WORDS, IN ORDER "
                 "==========\n");
    while (*p != '\0') {
        size_t len = 0;
        while (*p == ' ') {
            p++;
        }
        while (p[len] != '\0' && p[len] != ' ') {
            len++;
        }
        if (len == 0) {
            break;
        }
        if (len >= sizeof word) {
            len = sizeof word - 1;
        }
        memcpy(word, p, len);
        word[len] = '\0';
        p += len;
        n++;
        fprintf(out, "  %2d %-12s%s", n, word, (n % 4 == 0) ? "\n" : "");
    }
    if (n % 4 != 0) {
        fputc('\n', out);
    }
    fprintf(out, "======================================================="
                 "======================\n"
                 "They are the device's master persona (Encedo Manager's master "
                 "passphrase).\nThey are shown now and never stored; the master key "
                 "can never be rotated.\n");
    scrub(word, sizeof word);
}

/* The one-based position's word of `words` into buf (cap). */
static void word_at(const char *words, unsigned pos, char *buf, size_t cap)
{
    const char *p = words;
    unsigned n = 0;

    buf[0] = '\0';
    while (*p != '\0') {
        size_t len = 0;
        while (*p == ' ') {
            p++;
        }
        while (p[len] != '\0' && p[len] != ' ') {
            len++;
        }
        if (len == 0) {
            return;
        }
        if (++n == pos) {
            if (len >= cap) {
                len = cap - 1;
            }
            memcpy(buf, p, len);
            buf[len] = '\0';
            return;
        }
        p += len;
    }
}

/* Three distinct positions, ascending. Not a secret — a backup check — so
 * the C library generator, seeded from the clock, is enough. */
static void pick_positions(unsigned pos[BACKUP_CHECKS])
{
    unsigned i, j;
    srand((unsigned)time(NULL));
    for (i = 0; i < BACKUP_CHECKS; i++) {
        int again;
        do {
            pos[i] = 1u + (unsigned)rand() % WORD_COUNT;
            again = 0;
            for (j = 0; j < i; j++) {
                again |= (pos[j] == pos[i]);
            }
        } while (again);
    }
    for (i = 1; i < BACKUP_CHECKS; i++) {
        for (j = i; j > 0 && pos[j - 1] > pos[j]; j--) {
            unsigned t = pos[j];
            pos[j] = pos[j - 1];
            pos[j - 1] = t;
        }
    }
}

/* Generated words: shown ONCE, then the backup is checked by typing the
 * words at three positions (hidden); a wrong word shows the words again.
 * 0 = confirmed, 1 = cancelled. */
static int backup_check(wiz *w, const char *words, const unsigned *test_pos)
{
    unsigned pos[BACKUP_CHECKS];
    char want[32], got[64], q[64];
    unsigned i;
    int rc = 0;

    if (test_pos != NULL) {
        memcpy(pos, test_pos, sizeof pos);
    } else {
        pick_positions(pos);
    }
    show_words(w->out, words);
    for (;;) {
        int all = 1;
        fprintf(w->out, "\nBackup check - type the words at positions %u, %u and "
                        "%u (hidden; the list is not shown again):\n",
                pos[0], pos[1], pos[2]);
        for (i = 0; i < BACKUP_CHECKS; i++) {
            snprintf(q, sizeof q, "  word %u: ", pos[i]);
            if (hem_prompt_secret(&w->pr, q, got, sizeof got) == HEM_PROMPT_EOF) {
                rc = 1;
                goto done;
            }
            trim(got);
            for (size_t k = 0; got[k] != '\0'; k++) {
                got[k] = (char)tolower((unsigned char)got[k]);
            }
            word_at(words, pos[i], want, sizeof want);
            if (strcmp(got, want) != 0) {
                all = 0;
            }
        }
        if (all) {
            fprintf(w->out, "  backup confirmed\n");
            goto done;
        }
        fprintf(w->out, "  that does not match - here are the words again:\n");
        show_words(w->out, words);
    }
done:
    scrub(want, sizeof want);
    scrub(got, sizeof got);
    return rc;
}

/* The init flow's reboot question (hem_init_opts.confirm_reboot). End of
 * input = no: the init has happened; the next-steps line says to reboot. */
static int ask_reboot(void *arg)
{
    wiz *w = (wiz *)arg;
    char buf[16];
    for (;;) {
        int rc = hem_prompt_line(&w->pr, "the device needs a reboot to finish - "
                                 "reboot now and wait for it? [Y/n]: ",
                                 buf, sizeof buf);
        if (rc == HEM_PROMPT_EOF) {
            return 0;
        }
        trim(buf);
        if (buf[0] == '\0' || strcmp(buf, "y") == 0 || strcmp(buf, "Y") == 0 ||
            strcmp(buf, "yes") == 0) {
            return 1;
        }
        if (strcmp(buf, "n") == 0 || strcmp(buf, "N") == 0 || strcmp(buf, "no") == 0) {
            return 0;
        }
        fprintf(w->out, "  answer y or n\n");
    }
}

static int ask_settings(wiz *w, answers *a)
{
    char modes[512];
    size_t i, used = 0;

    fprintf(w->out, "\nDevice settings - Enter keeps the [default], Encedo "
                    "Manager's:\n");
    if (ask_text(w, "user", "the name of the device's user (you)", NULL,
                 a->user, sizeof a->user) ||
        ask_text(w, "email", "your address - lets Encedo find or block the "
                 "device when it is lost", NULL, a->email, sizeof a->email) ||
        /* REQ-TOOL-028 rev 3 (user decision 2026-10-19): the product default,
         * the same host hem-tool's default URL points at (REQ-TOOL-017). */
        ask_text(w, "hostname", "the domain the device answers on - hem-tool's "
                 "default URL is https://<hostname>\n  (Encedo Manager offers "
                 "my, dev, devel and moje.ence.do or your own *.ence.do prefix)",
                 HEM_TOOL_DEFAULT_HOSTNAME, a->hostname, sizeof a->hostname) ||
        ask_text(w, "ip", "the device's fixed local address/prefix - check "
                 "your LAN for a collision", HEM_INIT_DEFAULT_IP,
                 a->ip, sizeof a->ip)) {
        return 1;
    }
    for (i = 0; i < STORAGE_MODE_COUNT; i++) {
        int n = snprintf(modes + used, sizeof modes - used, "\n    %3lld  %s",
                         STORAGE_MODES[i].code, STORAGE_MODES[i].what);
        if (n < 0 || (size_t)n >= sizeof modes - used) {
            break;
        }
        used += (size_t)n;
    }
    {
        char explain[768];
        snprintf(explain, sizeof explain, "the storage layout (the regular "
                 "disk is unencrypted, the rest is the encrypted disk):%s",
                 modes);
        if (ask_number(w, "storage-mode", explain, HEM_INIT_DEFAULT_STORAGE_MODE,
                       1, 255, 1, &a->storage_mode)) {
            return 1;
        }
    }
    if (ask_number(w, "disk0-size", "bytes of the regular disk; the rest of "
                   "the storage is the encrypted disk\n  (Encedo Manager moves in "
                   "2 MiB = 2097152-byte steps)", HEM_INIT_DEFAULT_DISK0_SIZE,
                   1, INT64_MAX, 0, &a->disk0) ||
        ask_text(w, "origin", "the origin browsers may call the device API "
                 "from (CORS)", "*", a->origin, sizeof a->origin) ||
        ask_bool(w, "dnsd", "the device's DNS service (Encedo Manager always "
                 "sends no)", 0, &a->dnsd) ||
        ask_bool(w, "trusted-ts", "use the Encedo backend as a trusted source "
                 "of the current time", 1, &a->trusted_ts) ||
        ask_bool(w, "trusted-backend", "allow remote management - reporting a "
                 "stolen device, remote wipe", 1, &a->trusted_backend) ||
        ask_bool(w, "allow-keysearch", "allow searching the keystore by "
                 "description without an access token", 1, &a->allow_keysearch) ||
        ask_bool(w, "gen-csr", "have the device create a TLS certificate "
                 "request (CSR) for its own key", 0, &a->gen_csr) ||
        ask_number(w, "ctx", "an opaque context number kept with the settings "
                   "(Encedo Manager sends 0)", 0, 0, 2147483647LL, 0,
                   &a->ctx_id)) {
        return 1;
    }
    return 0;
}

/* The passphrase, hidden and twice. 0 = set, 1 = cancelled. */
static int ask_passphrase(wiz *w, answers *a)
{
    char again[sizeof a->pass];
    int rc = 0;

    fprintf(w->out, "\nThe passphrase - the device's user password: you log in "
                    "with it (hem-tool --passphrase,\nEncedo Manager). The device "
                    "decides what it accepts.\n");
    for (;;) {
        if (hem_prompt_secret(&w->pr, "  passphrase (hidden): ", a->pass,
                              sizeof a->pass) == HEM_PROMPT_EOF) {
            rc = 1;
            break;
        }
        if (a->pass[0] == '\0') {
            fprintf(w->out, "  the passphrase cannot be empty\n");
            continue;
        }
        if (hem_prompt_secret(&w->pr, "  again (hidden): ", again,
                              sizeof again) == HEM_PROMPT_EOF) {
            rc = 1;
            break;
        }
        if (strcmp(a->pass, again) == 0) {
            break;
        }
        fprintf(w->out, "  the two entries differ - once more\n");
    }
    scrub(again, sizeof again);
    return rc;
}

/* The master secret: generated words (default) or a typed mnemonic.
 * 0 = set, 1 = cancelled, -1 = the words could not be generated. */
static int ask_master(wiz *w, ehem_ctx *ctx, const hem_wizard_opts *o,
                      answers *a)
{
    char buf[16];

    fprintf(w->out, "\nThe master secret - the device's master persona (Encedo "
                    "Manager's master passphrase).\nIt signs this initialisation "
                    "and can NEVER be rotated: keep its 24 words safe.\n");
    for (;;) {
        int rc = hem_prompt_line(&w->pr, "  [g]enerate 24 new words or [t]ype "
                                 "an existing mnemonic [G/t]: ", buf, sizeof buf);
        if (rc == HEM_PROMPT_EOF) {
            return 1;
        }
        trim(buf);
        if (buf[0] == '\0' || strcmp(buf, "g") == 0 || strcmp(buf, "G") == 0) {
            if (o->test_words != NULL) {
                a->words = o->test_words;
            } else {
                ehem_rc erc = ehem_mnemonic_generate(ctx, &a->generated);
                if (erc != EHEM_OK) {
                    return -1;
                }
                a->words = a->generated;
            }
            return backup_check(w, a->words, o->test_positions);
        }
        if (strcmp(buf, "t") == 0 || strcmp(buf, "T") == 0) {
            break;
        }
        fprintf(w->out, "  answer g or t\n");
    }
    for (;;) {
        uint8_t secret[EHEM_MASTER_SECRET_SIZE];
        ehem_rc erc;
        int rc = hem_prompt_secret(&w->pr, "  mnemonic (24 words, hidden): ",
                                   a->typed_words, sizeof a->typed_words);
        if (rc == HEM_PROMPT_EOF) {
            return 1;
        }
        trim(a->typed_words);
        /* No ctx: its error detail could quote a mistyped word back. */
        erc = (rc == HEM_PROMPT_OK)
            ? ehem_master_secret_from_mnemonic(NULL, a->typed_words, secret)
            : EHEM_ERR_ARG;
        scrub(secret, sizeof secret);
        if (erc == EHEM_OK) {
            a->words = a->typed_words;
            return 0;
        }
        fprintf(w->out, "  not a valid 24-word mnemonic (BIP39 English words, "
                        "checksum) - try again\n");
    }
}

static void summary(wiz *w, const hem_wizard_opts *o, const answers *a,
                    int generated)
{
    FILE *out = w->out;
    fprintf(out, "\nSummary - nothing has been sent to the device yet:\n"
                 "  device           %s\n"
                 "  user             %s\n"
                 "  email            %s\n"
                 "  hostname         %s\n"
                 "  ip               %s\n",
            o->url != NULL ? o->url : "(the context's)", a->user, a->email,
            a->hostname, a->ip);
    fprintf(out, "  storage-mode     %lld\n"
                 "  disk0-size       %lld bytes\n"
                 "  origin           %s\n",
            a->storage_mode, a->disk0, a->origin);
    fprintf(out, "  dnsd             %s\n"
                 "  trusted-ts       %s\n"
                 "  trusted-backend  %s\n"
                 "  allow-keysearch  %s\n"
                 "  gen-csr          %s\n"
                 "  ctx              %lld\n"
                 "  passphrase       (hidden)\n"
                 "  master secret    24 words, %s\n",
            a->dnsd ? "yes" : "no", a->trusted_ts ? "yes" : "no",
            a->trusted_backend ? "yes" : "no", a->allow_keysearch ? "yes" : "no",
            a->gen_csr ? "yes" : "no", a->ctx_id,
            generated ? "generated and backed up" : "typed in (not shown)");
}

int hem_init_wizard_run(ehem_ctx *ctx, const hem_wizard_opts *o)
{
    wiz w;
    answers a;
    hem_init_opts io;
    FILE *out = (o->out != NULL) ? o->out : stdout;
    FILE *err = (o->err != NULL) ? o->err : stderr;
    char q[400];
    char confirm[256];
    int ret, m;

    memset(&a, 0, sizeof a);
    hem_prompt_init(&w.pr, o->in, out, err);
    w.out = out;

    /* 1. What this does. */
    fprintf(out, "hem-tool init-wizard - initialise (personalise) an Encedo "
                 "HEM, step by step.\n\n"
                 "The device must be UNINITIALISED (new, or wiped with "
                 "`hem-tool wipe-device`) and reached\nover http:// (%s). "
                 "Initialisation sets its user, network and storage settings,\n"
                 "its passphrase and its master secret - the master secret can "
                 "never be changed\nafterwards. Nothing is sent to the device "
                 "until you confirm the summary at the end;\nend the input "
                 "(Ctrl-D, or Ctrl-Z Enter on Windows) to cancel at any "
                 "prompt.\n",
            o->url != NULL ? o->url : "the device URL");

    /* 2.-4. The answers. */
    if (ask_settings(&w, &a) || ask_passphrase(&w, &a)) {
        goto cancelled;
    }
    m = ask_master(&w, ctx, o, &a);
    if (m < 0) {
        fprintf(err, "error: the 24 words could not be generated\n");
        ret = HEM_INIT_RUNTIME;
        goto out;
    }
    if (m > 0) {
        goto cancelled;
    }

    /* 5. The summary behind the typed hostname (wipe-device's pattern). */
    summary(&w, o, &a, a.words != a.typed_words);
    snprintf(q, sizeof q, "\ntype the hostname ('%s') to initialise the "
             "device now, anything else cancels: ", a.hostname);
    if (hem_prompt_line(&w.pr, q, confirm, sizeof confirm) != HEM_PROMPT_OK) {
        goto cancelled;
    }
    trim(confirm);
    if (strcmp(confirm, a.hostname) != 0) {
        goto cancelled;
    }

    /* 6. The init-device flow itself — same body, same exit codes. */
    memset(&io, 0, sizeof io);
    io.passphrase         = a.pass;
    io.master_words       = a.words;
    io.user               = a.user;
    io.email              = a.email;
    io.hostname           = a.hostname;
    io.ip                 = a.ip;
    io.storage_mode       = (int)a.storage_mode;
    io.disk0_size         = (int64_t)a.disk0;
    io.origin             = a.origin;
    io.dnsd               = a.dnsd;
    io.no_trusted_ts      = !a.trusted_ts;
    io.no_trusted_backend = !a.trusted_backend;
    io.no_allow_keysearch = !a.allow_keysearch;
    io.gen_csr            = a.gen_csr;
    io.ctx_id             = (int)a.ctx_id;
    io.confirm_reboot     = ask_reboot;
    io.confirm_arg        = &w;
    io.poll_attempts      = o->poll_attempts;
    io.poll_delay_ms      = o->poll_delay_ms;
    io.out                = out;
    io.err                = err;
    fputc('\n', out);
    ret = hem_init_device_run(ctx, &io);
    goto out;

cancelled:
    fprintf(out, "\ncancelled - nothing was sent to the device\n");
    ret = HEM_WIZARD_CANCELLED;
out:
    if (a.generated != NULL) {
        scrub(a.generated, strlen(a.generated));
        ehem_mnemonic_free(a.generated);
    }
    scrub(&a, sizeof a);
    return ret;
}
