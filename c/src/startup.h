/*
 * startup.h — the start of every hem-tool run: the explicit SDK
 * initialisation with its own exit code, and the --version report.
 *
 * implements: REQ-TOOL-024 (initialise up front; refuse a foreign crypto
 *             library with one message and exit 12),
 *             REQ-TOOL-025 (`--version` / `version`)
 */
#ifndef HEM_STARTUP_H
#define HEM_STARTUP_H

#include <stdio.h>

#include "ehem/ehem.h"

/*
 * Exit code: the SDK could not be initialised — in practice the crypto library
 * is missing or is not the one hem-tool was built against. Distinct from 2
 * (usage) so a script can tell a broken installation from a wrong command
 * line.
 */
#define HEM_TOOL_EXIT_INIT 12

/* The tool's own version, "1.3.0" — project(VERSION) of c/CMakeLists.txt
 * (HEM_TOOL_VERSION_STRING); the SDK's is ehem_version(). */
const char *hem_tool_version(void);

/*
 * Run ehem_global_init() — main.c calls this once, after parsing and the
 * commands that work without the SDK (help, --version), before any other SDK
 * call, output or traffic. Returns 0 on success; otherwise prints ONE message
 * to `err` (NULL → stderr) and returns HEM_TOOL_EXIT_INIT. For
 * EHEM_ERR_BACKEND_MISMATCH the message names the crypto backend, the version
 * hem-tool was built against, the version it loaded and the remedy; for any
 * other code, its ehem_rc_str() name.
 */
int hem_tool_global_init(FILE *err);

/*
 * The EHEM_ERR_BACKEND_MISMATCH message hem_tool_global_init() prints, built
 * from `bi` (REQ-API-011) — separate so the unit test can feed it the build
 * information of other builds. The remedy follows the link: a dynamic library
 * (a from-source build) is replaced by the one hem-tool was built against —
 * the released hem-tool has its crypto library linked in (REQ-PKG-003) and
 * never reaches that branch; a library linked in leaves only OpenSSL's
 * configuration as the cause (REQ-TOOL-024 rev 5), and any other backend
 * linked in is a broken build.
 */
void hem_tool_print_refusal(FILE *err, const ehem_build_info *bi);

/*
 * The --version report, four lines on `out` (NULL → stdout):
 *   hem-tool <tool version>
 *   sdk:       encedo-hem <SDK version>
 *   crypto:    <backend> <compiled> (compiled) / <loaded> (loaded), <link>
 *   transport: libcurl <compiled> (compiled) / <loaded> (loaded), <link>
 * Needs no SDK initialisation, so it works on any crypto library.
 */
void hem_tool_print_version(FILE *out);

#endif /* HEM_STARTUP_H */
