# check_tool_startup.cmake — hem-tool's start of run, on the REAL main.c.
#
# verifies: REQ-TOOL-024 (under a forced REQ-API-010 refusal `status` prints
#           ONE message and exits 12 before any other output or traffic;
#           help, --help, per-command help, --version and `keys types` still
#           work; usage errors keep exit 2; without the refusal the command
#           runs),
#           REQ-TOOL-025 (`--version` / `version` print the three-line
#           report, exit 0; the flag wins over a command),
#           REQ-TOOL-027 (`keys types [TYPE]` offline: no URL, no passphrase,
#           under the forced refusal; unknown TYPE exit 2),
#           REQ-TOOL-028 (`init-wizard` refuses init-device's flags with exit
#           2 and cancels on the end of input with exit 6, zero traffic),
#           REQ-TOOL-020 (rev 2: `--passphrase` is deprecated — exactly one
#           `warning:` line on stderr when the flag is used, none without it;
#           the command still runs)
#
# Run as a CTest (label `unit`):
#   cmake -DEHEM_TOOL=<hem-tool-forced-mismatch> -DEHEM_VERSION=<x.y.z> \
#         -P check_tool_startup.cmake
#
# The tool under test is main.c linked with tool_force_mismatch.c, whose
# constructor reads EHEM_TEST_FORCE_MISMATCH (version | probe). The device
# URL points at a closed local port: a run that got as far as a context would
# fail with exit 1 (unreachable), so exit 12 proves the refusal came first.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED EHEM_TOOL OR NOT EXISTS "${EHEM_TOOL}")
  message(FATAL_ERROR "EHEM_TOOL not set or missing: ${EHEM_TOOL}")
endif()
if(NOT DEFINED EHEM_VERSION)
  message(FATAL_ERROR "EHEM_VERSION not set")
endif()

set(_url "http://127.0.0.1:1")

# run(<force> <args...>) → _rc, _out, _err in the caller's scope. The device
# environment is cleared so a sourced hem.env cannot leak in; stdin is an
# empty file (an interactive command sees the end of input at once).
set(_empty_input "${CMAKE_CURRENT_BINARY_DIR}/tool_startup_empty_input.txt")
file(WRITE "${_empty_input}" "")
function(run force)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
      --unset=EHEM_URL --unset=EHEM_PASSPHRASE --unset=EHEM_MASTER_WORDS
      EHEM_TEST_FORCE_MISMATCH=${force}
      ${EHEM_TOOL} ${ARGN}
    INPUT_FILE "${_empty_input}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
    TIMEOUT 60)
  set(_rc "${rc}" PARENT_SCOPE)
  set(_out "${out}" PARENT_SCOPE)
  set(_err "${err}" PARENT_SCOPE)
  set(_what "[force=${force}] hem-tool ${ARGN}" PARENT_SCOPE)
endfunction()

macro(expect_rc want)
  if(NOT "${_rc}" STREQUAL "${want}")
    message(FATAL_ERROR "${_what}: exit ${_rc}, expected ${want}\n"
                        "stdout:\n${_out}\nstderr:\n${_err}")
  endif()
endmacro()

macro(expect_in var text)
  string(FIND "${${var}}" "${text}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "${_what}: '${text}' not in ${var}:\n${${var}}")
  endif()
endmacro()

macro(expect_not_in var text)
  string(FIND "${${var}}" "${text}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "${_what}: '${text}' unexpectedly in ${var}:\n${${var}}")
  endif()
endmacro()

macro(expect_empty var)
  if(NOT "${${var}}" STREQUAL "")
    message(FATAL_ERROR "${_what}: ${var} not empty:\n${${var}}")
  endif()
endmacro()

# The --version report: exactly three lines, `hem-tool <version>` first.
macro(expect_version_report)
  expect_rc(0)
  expect_empty(_err)
  string(REGEX MATCHALL "\n" _nl "${_out}")
  list(LENGTH _nl _lines)
  if(NOT _lines EQUAL 3)
    message(FATAL_ERROR "${_what}: ${_lines} lines, expected 3:\n${_out}")
  endif()
  string(FIND "${_out}" "hem-tool ${EHEM_VERSION}\n" _pos)
  if(NOT _pos EQUAL 0)
    message(FATAL_ERROR "${_what}: first line is not 'hem-tool ${EHEM_VERSION}':\n${_out}")
  endif()
  expect_in(_out "\ncrypto:    ")
  expect_in(_out " (compiled) / ")
  expect_in(_out "\ntransport: libcurl ")
endmacro()

# --- the refusal: one message, exit 12, nothing before it ------------------
# No --url and no EHEM_URL: if anything ran before the init, the default-URL
# notice would be on stderr.
run(version status)
expect_rc(12)
expect_empty(_out)
expect_not_in(_err "notice:")
string(REGEX MATCHALL "error:" _errors "${_err}")
list(LENGTH _errors _n)
if(NOT _n EQUAL 1)
  message(FATAL_ERROR "${_what}: ${_n} error lines, expected ONE message:\n${_err}")
endif()
expect_in(_err "EHEM_ERR_BACKEND_MISMATCH")
expect_in(_err "built against")
expect_in(_err "loaded")
expect_in(_err "remedy:")

run(probe --url ${_url} status)
expect_rc(12)
expect_empty(_out)
expect_in(_err "same version, incompatible build")

run(version --url ${_url} keys list)
expect_rc(12)
expect_in(_err "EHEM_ERR_BACKEND_MISMATCH")

# --- what works without the SDK: help and the version report ---------------
run(version help)
expect_rc(0)
expect_empty(_err)
expect_in(_out "usage: hem-tool")
expect_in(_out "--version")
expect_in(_out "exit 12")

run(version --help)
expect_rc(0)
expect_in(_out "usage: hem-tool")

run(version status --help)
expect_rc(0)
expect_in(_out "hem-tool status")

run(version help keys rm)
expect_rc(0)
expect_in(_out "--label-prefix")

run(version --version)
expect_version_report()
set(_report "${_out}")

run(version version)
expect_version_report()
if(NOT "${_out}" STREQUAL "${_report}")
  message(FATAL_ERROR "`version` and `--version` differ:\n${_out}\n---\n${_report}")
endif()

run(version --version status)          # the flag wins over a command
expect_version_report()

run(version status --version)
expect_version_report()

# `keys types` is local documentation: no URL, no passphrase (the env is
# cleared), any crypto library.
run(version keys types)
expect_rc(0)
expect_empty(_err)
expect_in(_out "SECP256R1")
expect_in(_out "MLDSA87")
expect_not_in(_out "notice:")

run(probe keys types ED25519)
expect_rc(0)
expect_in(_out "ED25519 - ")
expect_not_in(_out "ED448")

run(version keys types NOPE)
expect_rc(2)
expect_empty(_out)
expect_in(_err "'NOPE'")
expect_not_in(_err "EHEM_ERR_BACKEND_MISMATCH")

run(version keys types ED25519 extra)  # one TYPE at most
expect_rc(2)

# --passphrase is deprecated (REQ-TOOL-020 rev 2): ONE warning line on stderr
# when the flag is used — the command still runs (the `keys types` run above
# proves stderr stays empty without the flag).
run(version --passphrase x keys types)
expect_rc(0)
expect_in(_out "SECP256R1")
expect_in(_err "warning: --passphrase is deprecated")
expect_in(_err "EHEM_PASSPHRASE")
string(REGEX MATCHALL "warning:" _warns "${_err}")
list(LENGTH _warns _nwarn)
if(NOT _nwarn EQUAL 1)
  message(FATAL_ERROR "${_what}: ${_nwarn} warning lines, expected one:\n${_err}")
endif()
run(version --passphrase=x keys types)   # the = spelling warns the same way
expect_rc(0)
expect_in(_err "warning: --passphrase is deprecated")

# --- usage errors stay exit 2 (reported before the init) -------------------
run(version version extra)
expect_rc(2)

run(version)
expect_rc(2)

run(version --mobile cert-install)    # passphrase-only command: auth class
expect_rc(2)
expect_not_in(_err "EHEM_ERR_BACKEND_MISMATCH")

# --- no forced refusal: the init passes and the command runs ---------------
run(none --url ${_url} status)
expect_rc(1)                          # unreachable — the command DID run
expect_not_in(_err "EHEM_ERR_BACKEND_MISMATCH")
expect_in(_err "system status")

# init-wizard (REQ-TOOL-028): init-device's flags are refused (exit 2); the
# end of input at the first prompt cancels — exit 6, and nothing reached the
# device (a request to the closed port would have failed with exit 1).
run(none --url ${_url} init-wizard --user alice)
expect_rc(2)
expect_in(_err "init-device")

run(none --url ${_url} init-wizard)
expect_rc(6)
expect_in(_out "cancelled - nothing was sent to the device")
expect_not_in(_err "EHEM_ERR_")

run(none --version)
expect_version_report()
if(NOT "${_out}" STREQUAL "${_report}")
  message(FATAL_ERROR "the version report changed under the forced refusal:\n"
                      "${_out}\n---\n${_report}")
endif()

message(STATUS "hem-tool startup: refusal, pre-init commands and version report OK")
