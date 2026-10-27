# check_tests_backend_free.cmake — no test knows which crypto backend the SDK
# was built with: nothing under tests/ includes a wolfSSL or OpenSSL header
# (crypto goes through the SDK's public API, or its crypto_shim.h seam).
# Copied from the SDK's gate (there the per-backend crypto_ref modules are the
# one allowed exception; this tree has none).
#
# verifies: REQ-PKG-004 (the suite runs unchanged on both SDK backends)
#
# Run as a CTest (label `unit`):
#   cmake -DEHEM_TESTS_DIR=<repo>/tests -P check_tests_backend_free.cmake

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED EHEM_TESTS_DIR)
  message(FATAL_ERROR "EHEM_TESTS_DIR not set")
endif()

file(GLOB_RECURSE _files "${EHEM_TESTS_DIR}/*.c" "${EHEM_TESTS_DIR}/*.h")
set(_bad "")
set(_allowed "")
foreach(_f IN LISTS _files)
  file(STRINGS "${_f}" _hits
       REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](wolfssl|openssl)/")
  if(_hits)
    get_filename_component(_name "${_f}" NAME)
    get_filename_component(_dir "${_f}" DIRECTORY)
    if(_dir STREQUAL "${EHEM_TESTS_DIR}/support" AND
       _name MATCHES "^crypto_ref_[a-z]+\\.c$")
      list(APPEND _allowed "${_name}")
    else()
      list(APPEND _bad "${_f}")
    endif()
  endif()
endforeach()

if(_bad)
  message(FATAL_ERROR
    "a test includes a crypto backend header - route it through "
    "tests/support/crypto_ref.h: ${_bad}")
endif()
# This tree ships no per-backend reference module: nothing under tests/ may
# include a backend header at all (the SDK's crypto_ref modules live in the
# submodule, outside EHEM_TESTS_DIR).
if(_allowed)
  message(FATAL_ERROR "unexpected crypto_ref module under tests/support: ${_allowed}")
endif()

message(STATUS "tests are crypto-backend-free (no wolfSSL/OpenSSL include under tests/)")
