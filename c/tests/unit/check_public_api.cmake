# check_public_api.cmake — the tool sources use the SDK's public API only:
# every #include in c/src (vendored code excluded) names an `ehem/*.h` public
# header, a header of the tool itself, the vendored QR encoder, or a system
# header — never a file of the SDK's internal `src/` or test trees, in either
# quote or angle form. The package-mode build (no SDK source on the include
# path) is the second half of the proof.
#
# implements: REQ-PKG-002
# verifies: REQ-PKG-002
#
# Run as a CTest (label `unit`):
#   cmake -DHEM_SRC_DIR=<c>/src -DHEM_SDK_DIR=<c>/sdk -P check_public_api.cmake

cmake_minimum_required(VERSION 3.20)

foreach(_v HEM_SRC_DIR HEM_SDK_DIR)
  if(NOT DEFINED ${_v})
    message(FATAL_ERROR "${_v} not set")
  endif()
endforeach()

# The SDK's non-public header names: anything under its src/ and tests/.
file(GLOB_RECURSE _sdk_private RELATIVE "${HEM_SDK_DIR}"
     "${HEM_SDK_DIR}/src/*.h" "${HEM_SDK_DIR}/tests/*.h")
set(_private_names "")
foreach(_h IN LISTS _sdk_private)
  get_filename_component(_n "${_h}" NAME)
  list(APPEND _private_names "${_n}")
endforeach()
list(REMOVE_DUPLICATES _private_names)

# The tool's own headers.
file(GLOB_RECURSE _tool_headers RELATIVE "${HEM_SRC_DIR}" "${HEM_SRC_DIR}/*.h")
list(FILTER _tool_headers EXCLUDE REGEX "^vendor/")

file(GLOB_RECURSE _sources "${HEM_SRC_DIR}/*.c" "${HEM_SRC_DIR}/*.h")
list(FILTER _sources EXCLUDE REGEX "/vendor/")

set(_bad "")
set(_count 0)
foreach(_f IN LISTS _sources)
  file(STRINGS "${_f}" _incs REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
  foreach(_line IN LISTS _incs)
    math(EXPR _count "${_count} + 1")
    string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"].*$" "\\1" _path "${_line}")
    string(REGEX MATCH "^[ \t]*#[ \t]*include[ \t]*\"" _quoted "${_line}")
    get_filename_component(_name "${_path}" NAME)
    if(_path MATCHES "^ehem/[A-Za-z0-9_]+\\.h$")
      continue()                                   # the SDK's public API
    elseif(_path STREQUAL "vendor/qrcodegen/qrcodegen.h")
      continue()                                   # the vendored QR encoder
    elseif(_quoted AND "${_path}" IN_LIST _tool_headers)
      continue()                                   # a header of the tool itself
    elseif(NOT _quoted AND NOT "${_name}" IN_LIST _private_names)
      continue()                                   # a system header
    endif()
    file(RELATIVE_PATH _rel "${HEM_SRC_DIR}" "${_f}")
    list(APPEND _bad "${_rel}: ${_line}")
  endforeach()
endforeach()

if(_bad)
  list(JOIN _bad "\n  " _bad_txt)
  message(FATAL_ERROR
    "the tool reaches past the SDK's public API (REQ-PKG-002) - "
    "only ehem/*.h, the tool's own headers, vendor/qrcodegen/qrcodegen.h and system headers are allowed:\n  ${_bad_txt}")
endif()
message(STATUS "public API only: ${_count} includes in ${HEM_SRC_DIR} checked, none private")
