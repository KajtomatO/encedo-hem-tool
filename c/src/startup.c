/*
 * startup.c — explicit SDK initialisation and the --version report (see
 * startup.h).
 *
 * implements: REQ-TOOL-024, REQ-TOOL-025
 */
#include "startup.h"

#include <string.h>

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
    } else {
        /* A dynamic library: a from-source build picked up another one. The
         * released hem-tool has its crypto library linked in (REQ-PKG-003),
         * so it never reaches this branch. */
        fprintf(err, "  remedy: install the %s %s library hem-tool was built "
                     "against; a released\n"
                     "          hem-tool has its crypto library linked in and "
                     "needs no separate one\n",
                bi->crypto_backend, bi->crypto_version_compiled);
    }
}

const char *hem_tool_version(void)
{
    return HEM_TOOL_VERSION_STRING;
}

void hem_tool_print_version(FILE *out)
{
    ehem_build_info bi;

    if (out == NULL) {
        out = stdout;
    }
    ehem_build_info_get(&bi);
    fprintf(out, "hem-tool %s\n", hem_tool_version());
    fprintf(out, "sdk:       encedo-hem %s\n", ehem_version());
    fprintf(out, "crypto:    %s %s (compiled) / %s (loaded), %s\n",
            bi.crypto_backend, bi.crypto_version_compiled,
            bi.crypto_version_loaded, bi.crypto_link);
    fprintf(out, "transport: %s %s (compiled) / %s (loaded), %s\n",
            bi.transport_backend, bi.transport_version_compiled,
            bi.transport_version_loaded, bi.transport_link);
}
