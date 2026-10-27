/*
 * registry.h — the hem-tool command registry: the SINGLE source of truth
 * for every subcommand's name, synopsis, description, authentication
 * class, and option list. main.c only renders what lives here, so the
 * help/auth documentation can never drift from the command set.
 *
 * implements: REQ-TOOL-017 (default device URL resolution + notice),
 *             REQ-TOOL-019 (auth-requirement grouping as registry DATA),
 *             REQ-TOOL-020 (per-command help rendered from the registry)
 */
#ifndef HEM_REGISTRY_H
#define HEM_REGISTRY_H

#include <stddef.h>
#include <stdio.h>

/* Authentication requirement (REQ-TOOL-019). The class is DATA here; the
 * behavior it documents is enforced by the commands themselves through the
 * tool_auth chokepoint (proven in test_tool_auth.c):
 *   NONE            — no credentials (status, checkin);
 *   BEARER          — any bearer: passphrase or --mobile;
 *   PASSPHRASE_ONLY — ext pair: the device demands sub="U" for the pairing
 *                     trio (REQ-AUTH-006); mobile bearers carry
 *                     sub=base64(kid) and are rejected. */
typedef enum {
    HEM_AUTH_NONE = 0,
    HEM_AUTH_BEARER,
    HEM_AUTH_PASSPHRASE_ONLY
} hem_auth_class;

/* Where a command is listed in the top-level help (REQ-TOOL-019 rev 2):
 * MAIN = under its auth group; MANUAL_RECOVERY = in the trailing "manual
 * recovery" section (the building blocks `recovery` drives: cert-install,
 * tls-recover). Per-command help still shows the auth class. */
typedef enum {
    HEM_SECTION_MAIN = 0,
    HEM_SECTION_MANUAL_RECOVERY
} hem_cmd_section;

typedef struct {
    const char *flag;   /* spelled with its argument, e.g. "--label LABEL" */
    const char *help;   /* one line */
} hem_cmd_option;

typedef struct {
    const char *family;    /* NULL for a top-level command; "keys"/"logs"/"ext" */
    const char *name;      /* "status", or the subcommand ("rm" under "keys") */
    const char *synopsis;  /* positional args spelled out */
    const char *summary;   /* ONE line for the grouped top-level listing */
    const char *details;   /* per-command help paragraph (may span lines) */
    hem_auth_class auth;
    const hem_cmd_option *options;   /* this command's own options */
    size_t option_count;
    hem_cmd_section section;         /* listing section; zero = MAIN */
    void (*more)(FILE *f);           /* renders extra per-command help after
                                      * the details (keys gen: the type list,
                                      * REQ-TOOL-027); NULL = none */
} hem_command;

/* The full command table (REQ-TOOL-019/020 completeness walks). */
const hem_command *hem_registry_commands(size_t *count);

/* Find a command: (family, name) for two-word commands, (name, NULL) for
 * top-level ones. Returns NULL when unknown. */
const hem_command *hem_registry_find(const char *cmd, const char *subcmd);

/* Human string for an auth class (used by help rendering and tests). */
const char *hem_auth_class_str(hem_auth_class c);

/*
 * Top-level help (REQ-TOOL-019/020): usage line, the shared
 * connection/auth options, then the commands grouped by auth class — one
 * line per command, NO command-specific options (those live in
 * per-command help), closing with the `hem-tool help <command>` pointer.
 */
void hem_help_top(FILE *f, const char *version);

/*
 * Per-command help (REQ-TOOL-020): synopsis, auth requirement, details,
 * and ONLY that command's options (plus a pointer at the shared ones).
 * (cmd, NULL) on a family name prints the family summary. Returns 0 on
 * success, -1 when (cmd, subcmd) names no command or family.
 */
int hem_help_command(FILE *f, const char *cmd, const char *subcmd);

/*
 * REQ-TOOL-017: the built-in default device URL — the product `my`-domain
 * convention (the provisioning cloud registers devices under the `my`
 * domain: api.encedo.com/domain/register/my), not a user-specific address.
 * Tool-side ONLY: the SDK itself takes URLs as explicit parameters.
 * REQ-TOOL-028 rev 3: init-wizard offers the same hostname as its default
 * (user decision 2026-10-19), so both come from one definition.
 */
#define HEM_TOOL_DEFAULT_HOSTNAME "my.ence.do"
#define HEM_TOOL_DEFAULT_URL "https://" HEM_TOOL_DEFAULT_HOSTNAME

/*
 * Resolve the effective device URL: --url flag > EHEM_URL environment >
 * HEM_TOOL_DEFAULT_URL. Never returns NULL. When the default is used, a
 * one-line notice goes to `err` (NULL → stderr) so a user pointed at the
 * wrong device finds out immediately — stdout stays untouched for --raw
 * pipelines.
 */
const char *hem_tool_resolve_url(const char *flag_url, const char *env_url,
                                 FILE *err);

#endif /* HEM_REGISTRY_H */
