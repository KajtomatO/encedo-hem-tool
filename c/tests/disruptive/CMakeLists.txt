# Disruptive tests (label `disruptive`). Real device; MUTATES + REBOOTS it, so
# gated on EHEM_TEST_URL + EHEM_ALLOW_DISRUPTIVE=1 (exit 77 = skip otherwise).

# REQ-TOOL-003: live cert-install (harvest → skip-if-current → install →
# reboot → verify) through the real hem-tool cert-install code path.
hem_add_live_test(test_cert_install_live disruptive SOURCES test_cert_install_live.c)
