# hem-tool — the Encedo HEM device CLI, C implementation.
# implements: REQ-PKG-001
#
# Built against the Encedo HEM C SDK (libencedo-hem) in one of two ways:
#   HEM_TOOL_SDK_MODE=submodule  sdk/ (a git submodule pinned to the SDK release
#                                tag in cmake/sdk-version.cmake) is built from
#                                source and linked statically — the development
#                                and test build: the tests reach the SDK's
#                                hidden test seams and its fake transport.
#   HEM_TOOL_SDK_MODE=package    find_package(encedo-hem) of an installed SDK
#                                package of the SAME version — the release build
#                                (scripts/release/package.sh); no tests.
# Either way hem-tool itself uses the SDK's public API only (REQ-PKG-002).

cmake_minimum_required(VERSION 3.20)

project(hem-tool
  VERSION 1.3.0
  DESCRIPTION "Encedo HEM device CLI"
  LANGUAGES C)

include(GNUInstallDirs)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdk-version.cmake)   # HEM_TOOL_SDK_VERSION

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(MSVC)
  set(HEM_WARNING_FLAGS /W4 /WX)
else()
  set(HEM_WARNING_FLAGS -Wall -Wextra -Werror)
endif()

set(HEM_TOOL_SDK_MODE "submodule" CACHE STRING
  "How the SDK is consumed: submodule (sdk/ built from source) | package (find_package of an installed encedo-hem)")
set_property(CACHE HEM_TOOL_SDK_MODE PROPERTY STRINGS submodule package)
option(HEM_TOOL_BUILD_TESTS "Build the test suite (submodule mode only)" ON)

# The SDK forces Debug when no build type is set; decide here first so both
# projects agree.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

set(HEM_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sdk)

if(HEM_TOOL_SDK_MODE STREQUAL "submodule")
  if(NOT EXISTS ${HEM_SDK_DIR}/CMakeLists.txt)
    message(FATAL_ERROR
      "sdk/ is empty — run `git submodule update --init` (the SDK is pinned at v${HEM_TOOL_SDK_VERSION}).")
  endif()
  # The SDK's switches as normal variables (CMP0077 — they shadow the SDK's
  # option() defaults): no shared library, no SDK test suite, no in-SDK tool
  # (SDK releases up to 1.2.0 still carry one). The crypto backend defaults to
  # the one the released hem-tool ships with; -DEHEM_CRYPTO_BACKEND=wolfssl
  # still works for the CI leg that keeps the other backend honest.
  set(BUILD_TESTING OFF)
  set(EHEM_BUILD_TOOL OFF)
  set(EHEM_BUILD_SHARED OFF)
  set(EHEM_BUILD_STATIC ON)
  set(EHEM_CRYPTO_BACKEND "openssl" CACHE STRING "SDK crypto backend: wolfssl | openssl")
  add_subdirectory(sdk EXCLUDE_FROM_ALL)
  get_directory_property(_hem_sdk_version DIRECTORY sdk DEFINITION PROJECT_VERSION)
  if(NOT _hem_sdk_version STREQUAL HEM_TOOL_SDK_VERSION)
    message(FATAL_ERROR
      "sdk/ is encedo-hem ${_hem_sdk_version} but cmake/sdk-version.cmake pins "
      "${HEM_TOOL_SDK_VERSION} — move the submodule (`git -C sdk checkout "
      "v${HEM_TOOL_SDK_VERSION}`) or update the pin.")
  endif()
  set(HEM_SDK encedo-hem-static)
else()
  find_package(encedo-hem ${HEM_TOOL_SDK_VERSION} EXACT CONFIG REQUIRED)
  set(HEM_SDK encedo-hem::encedo-hem-static)
  if(HEM_TOOL_BUILD_TESTS)
    message(STATUS "hem-tool: package mode — the test suite needs the SDK source (submodule mode); tests OFF")
    set(HEM_TOOL_BUILD_TESTS OFF)
  endif()
endif()

# --- hem-tool-core: every command, shared by the CLI and its tests ----------
set(HEM_TOOL_CORE_SOURCES
  src/cert_install.c
  src/ext_cmd.c
  src/init_cmd.c
  src/keys.c
  src/keytypes.c
  src/logs.c
  src/prompt.c
  src/random.c
  src/recover.c
  src/recovery.c
  src/registry.c
  src/selftest.c
  src/sign.c
  src/startup.c
  src/tool_auth.c
  src/wipe.c
  src/wizard.c
  # Project Nayuki's QR encoder, vendored verbatim (src/vendor/qrcodegen/VENDORED.md).
  src/vendor/qrcodegen/qrcodegen.c)

add_library(hem-tool-core STATIC ${HEM_TOOL_CORE_SOURCES})
target_include_directories(hem-tool-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hem-tool-core PUBLIC ${HEM_SDK})
target_compile_definitions(hem-tool-core PUBLIC HEM_TOOL_VERSION_STRING="${PROJECT_VERSION}")
target_compile_options(hem-tool-core PRIVATE ${HEM_WARNING_FLAGS})

add_executable(hem-tool src/main.c)
target_link_libraries(hem-tool PRIVATE hem-tool-core)
target_compile_options(hem-tool PRIVATE ${HEM_WARNING_FLAGS})

install(TARGETS hem-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(HEM_TOOL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
