# check_conformance_sync.cmake — the C implementation agrees with the shared
# conformance vectors (conformance/*.json, REQ-TOOL-030):
#   qr-payload.json   every vector's `hash` is asserted by a test of this build —
#                     the tool's own (tests/unit/test_ext_tool.c: the pass-through
#                     of the scripted request) or the SDK's known-answer test of
#                     the function that composes the payload (sdk/tests/unit/
#                     test_notify.c at the pinned SDK version);
#   exit-codes.json   every command's own exit codes exist as values of that
#                     command's enumeration in its header, and the common codes
#                     12/13/14 appear where the tool documents them.
# Negative test: change one value in either JSON file; the gate must fail.
#
# implements: REQ-TOOL-030
# verifies: REQ-TOOL-030
#
#   cmake -DHEM_C_DIR=<c> -DHEM_CONFORMANCE_DIR=<repo>/conformance -P check_conformance_sync.cmake

cmake_minimum_required(VERSION 3.20)

foreach(_v HEM_C_DIR HEM_CONFORMANCE_DIR)
  if(NOT DEFINED ${_v})
    message(FATAL_ERROR "${_v} not set")
  endif()
endforeach()

set(_bad "")

# --- qr-payload.json ---------------------------------------------------------
file(READ "${HEM_CONFORMANCE_DIR}/qr-payload.json" _qr)
file(READ "${HEM_C_DIR}/tests/unit/test_ext_tool.c" _ext_test)
file(READ "${HEM_C_DIR}/sdk/tests/unit/test_notify.c" _sdk_test)
string(JSON _n LENGTH "${_qr}" vectors)
math(EXPR _last "${_n} - 1")
foreach(_i RANGE ${_last})
  string(JSON _name GET "${_qr}" vectors ${_i} name)
  string(JSON _hash GET "${_qr}" vectors ${_i} hash)
  string(FIND "${_ext_test}" "${_hash}" _p1)
  string(FIND "${_sdk_test}" "${_hash}" _p2)
  if(_p1 EQUAL -1 AND _p2 EQUAL -1)
    list(APPEND _bad "qr-payload.json/${_name}: hash ${_hash} is asserted by no test of this build")
  endif()
endforeach()
# The scripted request's payload is asserted verbatim by the tool test, as a
# C string literal split over several lines - so the check is field-wise: the
# `hash` field of vector 1's `expected`, in its C-literal form.
string(JSON _hash1 GET "${_qr}" vectors 1 hash)
string(FIND "${_ext_test}" "\\\"hash\\\":\\\"${_hash1}\\\"," _p3)
if(_p3 EQUAL -1)
  list(APPEND _bad "qr-payload.json/scripted-request-r.j.w: the hash field of `expected` is not asserted in test_ext_tool.c")
endif()
# Key order: the JSON `expected` strings put link, hash, user, email, hostname in that order.
foreach(_i RANGE ${_last})
  string(JSON _e GET "${_qr}" vectors ${_i} expected)
  if(NOT _e MATCHES "^\\{\"link\":.*\"hash\":.*\"user\":.*\"email\":.*\"hostname\":[^{]*\\}$")
    list(APPEND _bad "qr-payload.json vector ${_i}: `expected` does not keep the link, hash, user, email, hostname order")
  endif()
endforeach()

# --- exit-codes.json ---------------------------------------------------------
file(READ "${HEM_CONFORMANCE_DIR}/exit-codes.json" _ec)
# command -> the header whose enumeration carries its codes
set(_hdr_cert-install   cert_install.h)
set(_hdr_tls-recover    recover.h)
set(_hdr_recovery       recovery.h)
set(_hdr_selftest       selftest.h)
set(_hdr_random         random.h)
set(_hdr_ext_pair       ext_cmd.h)
set(_hdr_ext_login      ext_cmd.h)
set(_hdr_init-device    init_cmd.h)
set(_hdr_init-wizard    "init_cmd.h;wizard.h")
set(_hdr_keys_types     keytypes.h)
set(_hdr_wipe-device    wipe.h)
string(JSON _ncmd LENGTH "${_ec}" commands)
math(EXPR _lastc "${_ncmd} - 1")
foreach(_i RANGE ${_lastc})
  string(JSON _cmd MEMBER "${_ec}" commands ${_i})
  string(REPLACE " " "_" _key "${_cmd}")
  if(NOT DEFINED _hdr_${_key})
    continue()                                   # commands with the common codes only
  endif()
  string(JSON _codes GET "${_ec}" commands "${_cmd}")
  string(JSON _ncodes LENGTH "${_codes}")
  if(_ncodes EQUAL 0)
    continue()
  endif()
  set(_text "")
  foreach(_h IN LISTS _hdr_${_key})
    file(READ "${HEM_C_DIR}/src/${_h}" _t)
    string(APPEND _text "${_t}")
  endforeach()
  list(JOIN _hdr_${_key} " + " _hdr_txt)
  math(EXPR _lastk "${_ncodes} - 1")
  foreach(_j RANGE ${_lastk})
    string(JSON _code MEMBER "${_codes}" ${_j})
    # an enumerator `= N,` or a `#define HEM_... N` (the wizard's and keytypes' codes)
    if(_code MATCHES "^[0-9]+$" AND
       NOT _text MATCHES "(=|#define[ \t]+HEM_[A-Z_]+)[ \t]*${_code}[ \t]*([,/\n]|$)")
      list(APPEND _bad "exit-codes.json/${_cmd}: code ${_code} is not a value of ${_hdr_txt}")
    endif()
  endforeach()
endforeach()
# The common codes the tool documents itself.
file(READ "${HEM_C_DIR}/src/startup.h" _startup)
file(READ "${HEM_C_DIR}/src/registry.c" _registry)
if(NOT _startup MATCHES "HEM_TOOL_EXIT_INIT[ \t]+12")
  list(APPEND _bad "exit-codes.json/common: 12 is not HEM_TOOL_EXIT_INIT in startup.h")
endif()
if(NOT _registry MATCHES "exit 13 = no answer, 14 = rejected")
  list(APPEND _bad "exit-codes.json/common: the --mobile codes 13/14 are not documented in registry.c")
endif()

if(_bad)
  list(JOIN _bad "\n  " _bad_txt)
  message(FATAL_ERROR "the C implementation and conformance/ disagree (REQ-TOOL-030):\n  ${_bad_txt}")
endif()
message(STATUS "conformance in sync: ${_n} QR vectors, ${_ncmd} commands' exit codes")
