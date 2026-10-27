# hem-tool — C implementation

Built on the [Encedo HEM C SDK](https://github.com/KajtomatO/encedo-hem-c-api)
through its public API only. The SDK version this tree is built against is
pinned once, in [`cmake/sdk-version.cmake`](cmake/sdk-version.cmake).

## Build and test (submodule mode)

```sh
git submodule update --init          # sdk/ at the pinned SDK tag
./dev build                          # Debug build in build/ (gcc, OpenSSL backend)
./dev test unit                      # the offline suite + gates
./dev ci                             # the unit suite with gcc and clang
./dev test unit --wolfssl            # the other SDK backend (build-wolfssl/)
./dev tool status                    # run hem-tool with the device credentials sourced
```

Dependencies: a C99 compiler, CMake ≥ 3.20, Ninja, libcurl and OpenSSL
development packages, cmocka (`sdk/scripts/install-deps-linux.sh --yes`
installs them on Debian/Ubuntu; Windows: MSYS2 UCRT64 with the same set).
`hem.env` (git-ignored; `EHEM_URL`, `EHEM_PASSPHRASE`, `EHEM_TEST_URL`,
`EHEM_TEST_PASSPHRASE`) holds device credentials for `./dev tool` and the
live tests (`./dev test it`; `-d` adds the disruptive cert-install test).

In submodule mode the SDK is compiled from `sdk/` and linked statically; the
unit tests reach its test seams and fake transport that way. `-DEHEM_CRYPTO_BACKEND=wolfssl`
selects the other SDK backend.

## Package mode (the release build)

```sh
scripts/release/fetch-sdk.sh 1.2.0 linux deps-sdk     # the SDK's static package, checksum-verified
cmake -S . -B build-pkg -G Ninja -DHEM_TOOL_SDK_MODE=package \
      -DCMAKE_PREFIX_PATH=$PWD/deps-sdk/encedo-hem-1.2.0-linux-x86_64 \
      -DOPENSSL_USE_STATIC_LIBS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-pkg                                # one-file hem-tool, no tests
```

`scripts/release/package.sh` does this plus the release asserts and the
archive; [docs/RELEASING.md](docs/RELEASING.md) has the procedure, including
how to bump the SDK pin.

## Layout

```
CMakeLists.txt  cmake/sdk-version.cmake  dev
sdk/            the SDK submodule
src/            the commands (hem-tool-core) + main.c; vendor/qrcodegen/ (MIT, verbatim)
tests/          unit/ (18 tests, the forced-mismatch CLI run, the gates), integration/, disruptive/
scripts/release/  fetch-sdk.sh, package.sh, release-notes.sh
docs/RELEASING.md
```
