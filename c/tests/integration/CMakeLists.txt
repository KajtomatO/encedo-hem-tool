# Integration tests (label `integration`). Real device; skipped unless
# EHEM_TEST_URL is set. Both mutate EHEMTEST-prefixed keys only.

# REQ-TOOL-010: live hardware-RNG harvest + the tool's transient-key hygiene.
hem_add_live_test(test_random_live integration SOURCES test_random_live.c CMOCKA)

# REQ-TOOL-006: live `keys rm --label-prefix` on EHEMTEST keys.
hem_add_live_test(test_keys_rm_live integration SOURCES test_keys_rm_live.c CMOCKA)
