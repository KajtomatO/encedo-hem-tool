# Test wiring. CTest labels partition the suites (the SDK's ARCHITECTURE.md §9
# policy, carried over):
#   ctest -L unit         offline, always run
#   ctest -L integration  real device, gated on EHEM_TEST_URL
#   ctest -L disruptive   real device, mutates + reboots it; gated on
#                         EHEM_TEST_URL + EHEM_ALLOW_DISRUPTIVE=1
#
# implements: REQ-PKG-004

# --- cmocka (unit-test framework) -----------------------------------------
find_package(cmocka CONFIG QUIET)
if(NOT TARGET cmocka::cmocka)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(CMOCKA IMPORTED_TARGET QUIET cmocka)
    if(TARGET PkgConfig::CMOCKA)
      add_library(cmocka::cmocka ALIAS PkgConfig::CMOCKA)
    endif()
  endif()
endif()
if(NOT TARGET cmocka::cmocka)
  message(FATAL_ERROR
    "cmocka not found — install it (sdk/scripts/install-deps-linux.sh / .ps1) "
    "or set CMAKE_PREFIX_PATH to a cmocka install.")
endif()

# --- the SDK's test support, built from the submodule ----------------------
# The fake transport the unit tests inject through ehem_options.transport, plus
# the include paths the tests need: the SDK's internal headers (the hidden
# test seams: proto_auth.h, proto_ext.h, crypto_shim.h, ejwt.h, transport.h,
# json.h), tests/support (fake_transport.h, integration_env.h,
# ehem_test_keys.h) and tests/unit (the fixtures/ the tool tests share with
# the SDK's own tests). Linking the static SDK from source is what makes the
# hidden-visibility seams reachable (REQ-PKG-001).
add_library(hem_test_support STATIC ${HEM_SDK_DIR}/tests/support/fake_transport.c)
target_include_directories(hem_test_support PUBLIC
  ${HEM_SDK_DIR}/src
  ${HEM_SDK_DIR}/tests/support
  ${HEM_SDK_DIR}/tests/unit)
target_link_libraries(hem_test_support PUBLIC ${HEM_SDK})
target_compile_options(hem_test_support PRIVATE ${HEM_WARNING_FLAGS})

# hem_add_unit_test(<name> SOURCES a.c ...): offline, label `unit`.
function(hem_add_unit_test name)
  cmake_parse_arguments(A "" "" "SOURCES" ${ARGN})
  add_executable(${name} ${A_SOURCES})
  target_link_libraries(${name} PRIVATE hem-tool-core hem_test_support cmocka::cmocka)
  target_compile_options(${name} PRIVATE ${HEM_WARNING_FLAGS})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS unit)
endfunction()

# hem_add_live_test(<name> <label> SOURCES a.c ... [CMOCKA]): a real-device
# test; the test's main() exits 77 (skip) when its gate is not met.
function(hem_add_live_test name label)
  cmake_parse_arguments(A "CMOCKA" "" "SOURCES" ${ARGN})
  add_executable(${name} ${A_SOURCES})
  target_link_libraries(${name} PRIVATE hem-tool-core)
  if(A_CMOCKA)
    target_link_libraries(${name} PRIVATE cmocka::cmocka)
  endif()
  target_include_directories(${name} PRIVATE ${HEM_SDK_DIR}/tests/support)
  target_compile_options(${name} PRIVATE ${HEM_WARNING_FLAGS})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS ${label} SKIP_RETURN_CODE 77)
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(disruptive)
