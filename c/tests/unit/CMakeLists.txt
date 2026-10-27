# Unit tests of the C implementation (label `unit`): every command through
# hem-tool-core and the SDK's fake transport, plus the source gates.

# REQ-TOOL-003: cert-install orchestration.
hem_add_unit_test(test_cert_install SOURCES test_cert_install.c)
# REQ-TOOL-004/005: protected-key classifier + `keys list`.
hem_add_unit_test(test_keys SOURCES test_keys.c)
# REQ-TOOL-007: `keys pub`.
hem_add_unit_test(test_keys_pub SOURCES test_keys_pub.c)
# REQ-TOOL-008: `sign`.
hem_add_unit_test(test_sign_tool SOURCES test_sign_tool.c)
# REQ-TOOL-009: `keys gen`.
hem_add_unit_test(test_keys_gen SOURCES test_keys_gen.c)
# REQ-TOOL-010: `random`.
hem_add_unit_test(test_random_tool SOURCES test_random_tool.c)
# REQ-TOOL-011/012/013: `keys update` / `logs` / `selftest`.
hem_add_unit_test(test_tool_m7 SOURCES test_tool_m7.c)
# REQ-TOOL-015: `tls-recover`.
hem_add_unit_test(test_recover SOURCES test_recover.c)
# REQ-TOOL-022: `wipe-device`.
hem_add_unit_test(test_wipe SOURCES test_wipe.c)
# REQ-TOOL-021: `init-device`.
hem_add_unit_test(test_init_tool SOURCES test_init_tool.c)
# REQ-TOOL-028: `init-wizard` and the no-echo prompt helper.
hem_add_unit_test(test_wizard SOURCES test_wizard.c)
hem_add_unit_test(test_prompt SOURCES test_prompt.c)
# REQ-TOOL-023: `recovery`.
hem_add_unit_test(test_recovery SOURCES test_recovery.c)
# REQ-TOOL-016: `ext` pair/list/login + the terminal QR.
hem_add_unit_test(test_ext_tool SOURCES test_ext_tool.c)
# REQ-TOOL-018: the shared login chokepoint + --mobile outcome exits.
hem_add_unit_test(test_tool_auth SOURCES test_tool_auth.c)
# REQ-TOOL-017/019/020: command registry, help rendering, URL resolution.
hem_add_unit_test(test_registry SOURCES test_registry.c)
# REQ-TOOL-027: the key-type table behind `keys gen -h` / `keys types`.
hem_add_unit_test(test_keytypes SOURCES test_keytypes.c)
# REQ-TOOL-024/025: the start of run — SDK init refusal + exit 12, --version.
hem_add_unit_test(test_tool_startup SOURCES test_tool_startup.c)

# REQ-TOOL-024/025 on the REAL main.c: a test build of hem-tool whose
# constructor forces the SDK's crypto-library refusal named by
# EHEM_TEST_FORCE_MISMATCH; the script drives the command line.
add_executable(hem-tool-forced-mismatch
  ${PROJECT_SOURCE_DIR}/src/main.c tool_force_mismatch.c)
target_link_libraries(hem-tool-forced-mismatch PRIVATE hem-tool-core hem_test_support)
target_compile_options(hem-tool-forced-mismatch PRIVATE ${HEM_WARNING_FLAGS})
add_test(NAME tool_startup_cli
  COMMAND ${CMAKE_COMMAND}
    -DEHEM_TOOL=$<TARGET_FILE:hem-tool-forced-mismatch>
    -DEHEM_VERSION=${HEM_TOOL_SDK_VERSION}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_tool_startup.cmake)
set_tests_properties(tool_startup_cli PROPERTIES LABELS unit)

# --- source gates -----------------------------------------------------------

# Every string/character literal in the tool sources is ASCII (comments keep
# their typography; vendored code is third-party and excluded).
file(GLOB_RECURSE HEM_ASCII_SOURCES CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/src/*.c ${PROJECT_SOURCE_DIR}/src/*.h)
list(FILTER HEM_ASCII_SOURCES EXCLUDE REGEX "/vendor/")
add_executable(check_ascii_strings check_ascii_strings.c)
target_compile_options(check_ascii_strings PRIVATE ${HEM_WARNING_FLAGS})
add_test(NAME ascii_strings COMMAND check_ascii_strings ${HEM_ASCII_SOURCES})
set_tests_properties(ascii_strings PROPERTIES LABELS unit)

# No test knows which crypto backend the SDK was built with (REQ-PKG-004).
add_test(NAME tests_backend_free
  COMMAND ${CMAKE_COMMAND}
    -DEHEM_TESTS_DIR=${PROJECT_SOURCE_DIR}/tests
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_tests_backend_free.cmake)
set_tests_properties(tests_backend_free PROPERTIES LABELS unit)

# REQ-PKG-002: the tool sources include the SDK's public headers only.
add_test(NAME public_api_only
  COMMAND ${CMAKE_COMMAND}
    -DHEM_SRC_DIR=${PROJECT_SOURCE_DIR}/src
    -DHEM_SDK_DIR=${HEM_SDK_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_public_api.cmake)
set_tests_properties(public_api_only PROPERTIES LABELS unit)

# REQ-TOOL-030: the C implementation agrees with the shared conformance vectors.
add_test(NAME conformance_sync
  COMMAND ${CMAKE_COMMAND}
    -DHEM_C_DIR=${PROJECT_SOURCE_DIR}
    -DHEM_CONFORMANCE_DIR=${PROJECT_SOURCE_DIR}/../conformance
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_conformance_sync.cmake)
set_tests_properties(conformance_sync PROPERTIES LABELS unit)
