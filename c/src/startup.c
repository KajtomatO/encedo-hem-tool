/*
 * startup.c — explicit SDK initialisation and the --version report (see
 * startup.h).
 *
 * implements: REQ-TOOL-024, REQ-TOOL-025
 */
#include "startup.h"

#include <string.h>

/* The <os> part of the release's asset names: the companion
 * wolfssl-<version>-<os>-x86_64 (REQ-BUILD-005) and the one-file
 * hem-tool-<version>-<os>-x86_64 (REQ-BUILD-009). */
#ifdef _WIN32
#define HEM_RELEASE_OS "windows"
#else
#define HEM_RELEASE_OS "linux"
#endif

int hem_tool_global_init(FILE *err)
{
    ehem_build_info bi;
    ehem_rc rc = ehem_global_init();

    if (rc == EHEM_OK) {
        return 0;
    }
    if (err == NULL) {
        err = stderr;
    }
    if (rc != EHEM_ERR_BACKEND_MISMATCH) {
        fprintf(err, "error: the SDK could not be initialised: %s\n",
                ehem_rc_str(rc));
        return HEM_TOOL_EXIT_INIT;
    }

    /* The refusal carries no context detail (there is no context yet); the
     * versions come from the build information, which works after a refused
     * init (REQ-API-011). */
    ehem_build_info_get(&bi);
    hem_tool_print_refusal(err, &bi);
    return HEM_TOOL_EXIT_INIT;
}

void hem_tool_print_refusal(FILE *err, const ehem_build_info *bi)
{
    /* Equal versions = the struct self-test refused a same-version library
     * built with other options. */
    fprintf(err, "error: hem-tool cannot use the %s library it loaded (%s)\n",
            bi->crypto_backend, ehem_rc_str(EHEM_ERR_BACKEND_MISMATCH));
    fprintf(err, "  %s: built against %s, loaded %s%s\n",
            bi->crypto_backend, bi->crypto_version_compiled,
            bi->crypto_version_loaded,
            strcmp(bi->crypto_version_compiled, bi->crypto_version_loaded) == 0
                ? " - same version, incompatible build" : "");
    if (strcmp(bi->crypto_link, "static") == 0) {
        /* Linked in, so no other library is involved: OpenSSL's configuration
         * (read on Linux) can leave it without the algorithms the self-test
         * needs; another backend linked in is a broken build. */
        if (strcmp(bi->crypto_backend, "OpenSSL") == 0) {
            fputs("  remedy: OpenSSL is linked into hem-tool, so the OpenSSL "
                  "configuration it\n"
                  "          read (OPENSSL_CONF or openssl.cnf) left it without "
                  "the algorithms\n"
                  "          it needs - fix or unset it\n", err);
        } else {
            fprintf(err, "  remedy: %s is linked into hem-tool, so no other "
                         "library is involved -\n"
                         "          this hem-tool build is broken\n",
                    bi->crypto_backend);
        }
    } else if (strcmp(bi->crypto_backend, "wolfSSL") == 0) {
        /* The one-file hem-tool is the plain asset of the release (REQ-BUILD-009,
         * user decision 2026-10-19); this binary is its -shared sibling. */
        fprintf(err, "  remedy: put the wolfSSL library from the release asset "
                     "wolfssl-%s-%s-x86_64\n"
                     "          next to hem-tool, or use the one-file hem-tool "
                     "asset\n"
                     "          hem-tool-%s-%s-x86_64 (1.2.0 and later), which "
                     "needs no separate\n"
                     "          crypto library\n",
                bi->crypto_version_compiled, HEM_RELEASE_OS,
                ehem_version(), HEM_RELEASE_OS);
    } else {
        fprintf(err, "  remedy: install the %s %s library hem-tool was built "
                     "against, or use the\n"
                     "          one-file hem-tool asset hem-tool-%s-%s-x86_64 "
                     "(1.2.0 and later),\n"
                     "          which needs no separate crypto library\n",
                bi->crypto_backend, bi->crypto_version_compiled,
                ehem_version(), HEM_RELEASE_OS);
    }
}

void hem_tool_print_version(FILE *out)
{
    ehem_build_info bi;

    if (out == NULL) {
        out = stdout;
    }
    ehem_build_info_get(&bi);
    fprintf(out, "hem-tool %s\n", ehem_version());
    fprintf(out, "crypto:    %s %s (compiled) / %s (loaded), %s\n",
            bi.crypto_backend, bi.crypto_version_compiled,
            bi.crypto_version_loaded, bi.crypto_link);
    fprintf(out, "transport: %s %s (compiled) / %s (loaded), %s\n",
            bi.transport_backend, bi.transport_version_compiled,
            bi.transport_version_loaded, bi.transport_link);
}
