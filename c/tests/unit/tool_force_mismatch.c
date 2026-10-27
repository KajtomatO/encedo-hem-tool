/*
 * tool_force_mismatch.c — linked into a TEST build of the real hem-tool
 * main.c (target hem-tool-forced-mismatch, tests/unit/CMakeLists.txt).
 * Before main() runs it forces the REQ-API-010 refusal named by
 * EHEM_TEST_FORCE_MISMATCH: "version" or "probe" (anything else, or unset,
 * leaves the real check). The production hem-tool never contains this file,
 * and main.c is compiled unchanged.
 *
 * supports: REQ-TOOL-024, REQ-TOOL-025 (driven by check_tool_startup.cmake)
 */
#include <stdlib.h>
#include <string.h>

#include "crypto_shim.h"      /* internal: ehem_crypto_test_force_mismatch */

/* GCC/Clang constructor (ELF .init_array, MinGW .ctors): runs before main. */
__attribute__((constructor))
static void force_mismatch_from_env(void)
{
    const char *mode = getenv("EHEM_TEST_FORCE_MISMATCH");

    if (mode == NULL) {
        return;
    }
    if (strcmp(mode, "version") == 0) {
        ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_VERSION);
    } else if (strcmp(mode, "probe") == 0) {
        ehem_crypto_test_force_mismatch(EHEM_CRYPTO_FORCE_PROBE);
    }
}
