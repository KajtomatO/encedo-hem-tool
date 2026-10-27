/*
 * wizard.h — hem-tool `init-wizard`: attended, prompt-driven device
 * initialisation over the init-device flow.
 *
 * implements: REQ-TOOL-028
 *
 * Collects every init-device input through explained prompts — the cfg
 * fields with Encedo Manager's defaults, the passphrase hidden and typed
 * twice, the master secret as 24 generated words (shown once, backup checked
 * by re-typing three of them) or a typed existing mnemonic — shows a summary
 * behind the typed hostname, then runs hem_init_device_run() (init_cmd.h), so
 * the POST is init-device's by construction; when the device asks for a
 * reboot, the wizard asks too. Nothing reaches the device before the typed
 * confirmation. ATTENDED-ONLY (REQ-TEST-007): the unit test scripts the
 * dialogue; the real run is the M12 gate's.
 */
#ifndef HEM_TOOL_WIZARD_H
#define HEM_TOOL_WIZARD_H

#include <stdio.h>

#include "ehem/ehem.h"

/* Exit codes: 0-5 exactly as init-device (init_cmd.h HEM_INIT_*), plus: */
#define HEM_WIZARD_CANCELLED 6   /* the operator cancelled (end of input, or a
                                  * wrong hostname at the confirmation) */

typedef struct {
    const char *url;              /* the device URL (intro and summary) */
    FILE       *in;               /* answers (NULL → stdin) */
    FILE       *out;              /* questions, words, summary, result (NULL → stdout) */
    FILE       *err;              /* errors, notices (NULL → stderr) */
    unsigned    poll_attempts;    /* reboot wait, as hem_init_opts (0 → default) */
    unsigned    poll_delay_ms;    /* VERBATIM, as hem_init_opts */
    /* Test seams — the CLI leaves both NULL: */
    const char     *test_words;     /* "generated" words instead of
                                     * ehem_mnemonic_generate() */
    const unsigned *test_positions; /* the 3 one-based backup-check positions
                                     * instead of random ones */
} hem_wizard_opts;

/*
 * Run the wizard against `ctx` (created for the device's URL; no traffic
 * happens before the typed confirmation). Returns hem_init_device_run()'s
 * exit code, HEM_WIZARD_CANCELLED, or HEM_INIT_RUNTIME when the words cannot
 * be generated. The passphrase and the words are scrubbed before returning.
 */
int hem_init_wizard_run(ehem_ctx *ctx, const hem_wizard_opts *o);

#endif /* HEM_TOOL_WIZARD_H */
