/*
 * test_cert_install_live.c — the hem-tool `cert-install` flow against the real
 * dev device (REQ-TOOL-003), driven through the exact shared tool code
 * (hem-tool-core, public API only).
 *
 * DISRUPTIVE: on a device whose served certificate does not already match the
 * cloud-delivered one, this authenticates, installs, and REBOOTS it. It is
 * therefore gated on EHEM_TEST_URL + EHEM_ALLOW_DISRUPTIVE=1 and excluded from
 * `-L integration`; without the gate it skips (exit 77).
 *
 * A success is either "already current" (no reboot) or a completed rotation —
 * both exit HEM_CERT_OK. Configure the device passphrase in EHEM_TEST_PASSPHRASE
 * and the TLS mode via EHEM_TEST_INSECURE / EHEM_TEST_CACERT (see
 * integration_env.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ehem/ehem.h"
#include "cert_install.h"
#include "integration_env.h"

int main(void)
{
    ehem_ctx *ctx = NULL;
    hem_cert_install_opts o;
    const char *insecure = getenv("EHEM_TEST_INSECURE");
    ehem_rc crc;
    int rc;

    ehem_require_disruptive();

    crc = ehem_test_ctx(&ctx);
    if (crc != EHEM_OK) {
        fprintf(stderr, "context create failed: %s\n", ehem_rc_str(crc));
        return 1;
    }

    memset(&o, 0, sizeof o);
    o.passphrase    = ehem_test_passphrase();
    o.insecure      = (insecure != NULL && insecure[0] == '1');
    o.poll_attempts = HEM_CERT_DEFAULT_POLL_ATTEMPTS;
    o.poll_delay_ms = HEM_CERT_DEFAULT_POLL_DELAY_MS;
    o.out           = stdout;
    o.err           = stderr;

    rc = hem_cert_install_run(ctx, &o);

    ehem_ctx_destroy(ctx);
    ehem_global_cleanup();

    if (rc != HEM_CERT_OK) {
        fprintf(stderr, "cert-install exited %d\n", rc);
        return 1;
    }
    return 0;
}
