#!/usr/bin/env bash
#
# package.sh — the released hem-tool (REQ-PKG-003): ONE file per platform,
# built in package mode against the SDK's published static package of the
# pinned version (fetch-sdk.sh) — OpenSSL and libcurl linked in from that
# package, the SDK itself too. Linux keeps glibc dynamic; Windows imports
# system DLLs only. The link mode, the absence of wolfSSL and the exact
# --version report are asserted, the binary is packaged with its notices as
# hem-tool-<label>-<os>-x86_64.{tar.gz|zip} and smoke-tested from a directory
# of its own; SHA256SUMS covers every archive in <dist>.
#
# implements: REQ-PKG-003
#
#   scripts/release/package.sh <sdk package dir> <linux|windows> [dist]
#
# RELEASE_TAG (set by the workflow on a tag build) must be c-v<version> or
# c-v<version>-rc<N>; <version> is project(VERSION) of c/CMakeLists.txt.
set -euo pipefail

PKG="$(cd "${1:?usage: package.sh <sdk package dir> <linux|windows> [dist]}" && pwd)"
OS="${2:?linux|windows}"
DIST="${3:-dist}"
BUILD="${BUILD_DIR:-build-release}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

fail() { echo "package.sh: $*"; exit 1; }
native() { if [ "$OS" = "windows" ]; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

# --- versions: the tool's, the tag's, the SDK pin's ---------------------------
VERSION="$(sed -n -E '/^project\(hem-tool/,/\)/{s/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p}' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || fail "cannot read the project version from CMakeLists.txt"
SHA="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [ -n "${RELEASE_TAG:-}" ]; then
  # c-v<version> = a release; c-v<version>-rc<N> = a pre-release of that version.
  if [ "$RELEASE_TAG" != "c-v$VERSION" ] \
     && ! { [[ "$RELEASE_TAG" =~ -rc[1-9][0-9]*$ ]] && [ "${RELEASE_TAG%-rc*}" = "c-v$VERSION" ]; }; then
    fail "tag $RELEASE_TAG does not match the project version: expected c-v$VERSION or c-v$VERSION-rc<N>"
  fi
  LABEL="${RELEASE_TAG#c-v}"
  SRC_REF="tag ${RELEASE_TAG}"
else
  LABEL="${VERSION}+g${SHA}"
  SRC_REF="commit ${SHA}"
fi
PIN="$(sed -n -E 's/^set\(HEM_TOOL_SDK_VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)\).*/\1/p' cmake/sdk-version.cmake)"
[ -n "$PIN" ] || fail "cannot read HEM_TOOL_SDK_VERSION from cmake/sdk-version.cmake"
SDK_VERSION="$(sed -n -E 's/^set\(PACKAGE_VERSION[[:space:]]+"([0-9]+\.[0-9]+\.[0-9]+)"\).*/\1/p' "$PKG/lib/cmake/encedo-hem/encedo-hem-config-version.cmake" | head -1)"
[ -n "$SDK_VERSION" ] || fail "$PKG holds no encedo-hem package (no config-version file)"
[ "$SDK_VERSION" = "$PIN" ] || fail "the SDK package is $SDK_VERSION but cmake/sdk-version.cmake pins $PIN"
[ -f "$PKG/lib/libencedo-hem.a" ] || fail "$PKG is not the STATIC SDK package (no libencedo-hem.a)"
export PKG_CONFIG_PATH="$PKG/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
OPENSSL_VERSION="$(pkg-config --modversion libcrypto)"
CURL_VERSION="$(pkg-config --modversion libcurl)"
NAME="hem-tool-${LABEL}-${OS}-x86_64"
echo "hem-tool $LABEL ($SRC_REF) on encedo-hem $SDK_VERSION, OpenSSL $OPENSSL_VERSION, libcurl $CURL_VERSION"

EXTRA=()
if [ "$OS" = "windows" ]; then
  EXTRA+=(-DCMAKE_EXE_LINKER_FLAGS=-static)        # libgcc, libwinpthread and every archive in
  EXT=zip; X=.exe
else
  EXTRA+=(-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc) # the archives + libgcc in, glibc dynamic
  EXT=tar.gz; X=
fi

# --- build: package mode, Release, everything from the SDK package -----------
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DHEM_TOOL_SDK_MODE=package \
  -DCMAKE_PREFIX_PATH="$PKG" -DOPENSSL_ROOT_DIR="$PKG" -DOPENSSL_USE_STATIC_LIBS=ON "${EXTRA[@]}"
cmake --build "$BUILD"

# --- link mode: nothing but the system's libraries ---------------------------
BIN="$BUILD/hem-tool$X"
if [ "$OS" = "windows" ]; then
  DLLS="$(objdump -p "$BIN" | awk '/DLL Name:/ {print tolower($3)}' | sort -u)"
  echo "imports:"
  # shellcheck disable=SC2001  # indenting a multi-line value
  echo "$DLLS" | sed 's/^/  /'
  OTHER="$(echo "$DLLS" | grep -E -v '^(api-ms-win-[a-z0-9-]+|ucrtbase|kernel32|advapi32|user32|gdi32|ws2_32|crypt32|bcrypt|secur32|iphlpapi|normaliz|wldap32|ntdll|shell32|ole32|rpcrt4|userenv)\.dll$' || true)"
  [ -z "$OTHER" ] || fail "hem-tool.exe imports a DLL that is not a Windows system DLL: $OTHER"
  echo "$DLLS" | grep -E -q '^(api-ms-win-crt-|ucrtbase\.dll)' || fail "hem-tool.exe imports no Universal CRT - not a UCRT64 build"
else
  if readelf -d "$BIN" | grep -E -q '\((RUNPATH|RPATH)\)'; then
    fail "hem-tool has a RUNPATH/RPATH - it must find nothing beside itself"
  fi
  echo "shared objects:"; ldd "$BIN" | sed 's/^/  /'
  OTHER="$(ldd "$BIN" | awk '{print $1}' | sed 's|.*/||' \
    | grep -E -v '^(linux-vdso\.so\.1|(libc|libm|libpthread|libdl|librt|libresolv)\.so\.[0-9]+|ld-linux-x86-64\.so\.2)$' || true)"
  [ -z "$OTHER" ] || fail "hem-tool needs a library outside glibc: $OTHER"
fi

# --- no wolfSSL anywhere ------------------------------------------------------
if nm "$BIN" 2> /dev/null | grep -E -q ' (wolfSSL_|wolfCrypt_|wc_)[A-Za-z]'; then
  fail "wolfSSL symbols in $BIN"
fi
if strings -a "$BIN" | grep -E -q '(wolfSSL_|wolfCrypt_|(^|[^A-Za-z0-9_])wc_)[A-Za-z]'; then
  fail "wolfSSL names in the strings of $BIN"
fi
echo "no wolfSSL symbol or name in $BIN"

# --- the --version report, exactly (REQ-TOOL-025 rev 6) ----------------------
"$BIN" help > /dev/null
VER="$("$BIN" --version | tr -d '\r')"
# shellcheck disable=SC2001
echo "$VER" | sed 's/^/  /'
[ "$(echo "$VER" | wc -l)" -eq 4 ] || fail "--version is not four lines"
echo "$VER" | sed -n 1p | grep -q -x "hem-tool ${VERSION}" || fail "--version line 1 is not 'hem-tool ${VERSION}'"
echo "$VER" | sed -n 2p | grep -q -x "sdk:       encedo-hem ${SDK_VERSION}" || fail "--version line 2 is not 'sdk:       encedo-hem ${SDK_VERSION}'"
echo "$VER" | grep -q -x "crypto:    OpenSSL ${OPENSSL_VERSION} (compiled) / ${OPENSSL_VERSION} (loaded), static" \
  || fail "--version does not report OpenSSL ${OPENSSL_VERSION} compiled and loaded, static"
echo "$VER" | grep -q -x "transport: libcurl ${CURL_VERSION} (compiled) / ${CURL_VERSION} (loaded), static" \
  || fail "--version does not report libcurl ${CURL_VERSION} compiled and loaded, static"

# --- package: one file + notices + README ------------------------------------
STAGE="$DIST/$NAME"
rm -rf "$STAGE"; mkdir -p "$STAGE/LICENSES"
cp "$BIN" "$STAGE/"
cp ../LICENSE "$STAGE/LICENSES/LICENSE.hem-tool"
for f in LICENSE.encedo-hem LICENSE.openssl COPYING.curl LICENSE.cjson LICENSE.bip39-wordlist; do
  [ -f "$PKG/LICENSES/$f" ] || fail "the SDK package carries no LICENSES/$f"
  cp "$PKG/LICENSES/$f" "$STAGE/LICENSES/$f"
done
# qrcodegen's MIT text exists only as the header block of qrcodegen.c (its
# VENDORED.md); ship that block verbatim and FAIL if it is not there.
sed -n '1,/^ \*\/$/p' src/vendor/qrcodegen/qrcodegen.c > "$STAGE/LICENSES/LICENSE.qrcodegen"
grep -q 'Permission is hereby granted' "$STAGE/LICENSES/LICENSE.qrcodegen" || fail "qrcodegen MIT header not found"
if [ "$OS" = "windows" ]; then
  NEEDS="Windows system DLLs only - nothing to install beside it."
  TRUST="Certificates: the Windows certificate store (or --cacert FILE)."
  CONF="OpenSSL configuration: none - hem-tool reads neither openssl.cnf nor the
file OPENSSL_CONF names, so nothing on disk changes how it uses TLS."
else
  GLIBC_MIN="$(objdump -T "$BIN" | sed -n -E 's/.*GLIBC_([0-9]+\.[0-9]+(\.[0-9]+)?).*/\1/p' | sort -V | tail -1)"
  [ -n "$GLIBC_MIN" ] || fail "no glibc symbol version in $BIN"
  NEEDS="the system C library only (glibc ${GLIBC_MIN} or later) - nothing to install
beside it."
  TRUST="Certificates: /etc/ssl/certs/ca-certificates.crt and /etc/ssl/certs (the
Debian/Ubuntu locations; elsewhere pass --cacert FILE)."
  CONF="OpenSSL configuration: /etc/ssl/openssl.cnf (or the file OPENSSL_CONF names),
as for the system's OpenSSL. One that leaves OpenSSL without the algorithms
hem-tool needs makes it stop with one message and exit code 12."
fi
cat > "$STAGE/README.txt" <<EOF
hem-tool ${LABEL} (${OS}, x86_64) - the Encedo HEM device CLI as one file.

Built on the Encedo HEM C SDK ${SDK_VERSION}. The SDK, OpenSSL ${OPENSSL_VERSION}
(the crypto library and libcurl's TLS backend) and libcurl ${CURL_VERSION}
(HTTP/HTTPS only) are linked INTO hem-tool: it needs ${NEEDS}

${TRUST}
${CONF}

Usage: hem-tool help            (commands grouped by what they need)
       hem-tool help <command>  (per-command options)
       hem-tool --version       (the tool, the SDK, the libraries built against and running)
Device URL via --url or EHEM_URL (default https://my.ence.do); passphrase via
EHEM_PASSPHRASE (--passphrase is deprecated: it stays in the shell history and
shows in the process list; it will be removed). Verify downloads against
SHA256SUMS:
  sha256sum -c SHA256SUMS      (Linux)
  CertUtil -hashfile <file> SHA256   (Windows)

Licenses: see LICENSES/ - hem-tool and the Encedo HEM C SDK are MIT; OpenSSL
(Apache License 2.0), libcurl (curl license), cJSON (MIT), the BIP39 wordlist
(MIT) and qrcodegen (MIT) are compiled in. Source:
https://github.com/KajtomatO/encedo-hem-tool (${SRC_REF}); the SDK package is
https://github.com/KajtomatO/encedo-hem-c-api release v${SDK_VERSION}.
EOF
if [ "$OS" = "windows" ]; then
  cat >> "$STAGE/README.txt" <<'EOF'

Windows: hem-tool.exe uses the Universal C Runtime (built in MSYS2 UCRT64) -
part of Windows 10 / Server 2016 and later; older Windows needs update
KB2999226.
EOF
fi

mkdir -p "$DIST"
( cd "$DIST" && rm -f "$NAME.tar.gz" "$NAME.zip" )
if [ "$OS" = "windows" ]; then
  ( cd "$DIST" && zip -qr "$NAME.zip" "$NAME" )
else
  ( cd "$DIST" && tar czf "$NAME.tar.gz" "$NAME" )
fi
echo "packaged: $DIST/$NAME.$EXT"

# --- smoke test: the archive alone, as a user unpacks it ----------------------
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
if [ "$OS" = "windows" ]; then unzip -q "$DIST/$NAME.zip" -d "$T"; else tar xzf "$DIST/$NAME.tar.gz" -C "$T"; fi
EXE="$T/$NAME/hem-tool$X"
[ "$(find "$T/$NAME" -maxdepth 1 -type f -name 'hem-tool*' | wc -l)" -eq 1 ] || fail "the archive holds more than the one hem-tool file"
"$EXE" help > /dev/null
"$EXE" keys types > /dev/null
status_rc() {  # runs `status` against a closed port with the given env; prints stderr, returns the exit code
  local rc=0
  env "$@" "$EXE" --url https://127.0.0.1:1 status > /dev/null 2> "$T/err" || rc=$?
  tr -d '\r' < "$T/err" | sed 's/^/  /'
  return "$rc"
}
RC=0; status_rc HEM_SMOKE=1 || RC=$?
echo "status against a closed port: exit $RC"
[ "$RC" -eq 1 ] || fail "expected exit 1 (initialised, then unreachable)"
cat > "$T/null-provider.cnf" <<'EOF'
openssl_conf = openssl_init
[openssl_init]
providers = provider_sect
[provider_sect]
null = null_sect
[null_sect]
activate = 1
EOF
CNF="$(native "$T/null-provider.cnf")"
RC=0; status_rc "OPENSSL_CONF=$CNF" || RC=$?
echo "with OPENSSL_CONF=$CNF (null provider only): exit $RC"
if [ "$OS" = "windows" ]; then
  [ "$RC" -eq 1 ] || fail "the Windows one-file hem-tool read OPENSSL_CONF (exit $RC, expected 1)"
else
  [ "$RC" -eq 12 ] || fail "the Linux one-file hem-tool ignored OPENSSL_CONF (exit $RC, expected 12)"
  grep -q 'OpenSSL is linked into hem-tool' "$T/err" || fail "the refusal does not name the OpenSSL configuration"
fi
echo "one-file hem-tool smoke OK: $NAME.$EXT"

# --- SHA256SUMS over every archive in dist -----------------------------------
( cd "$DIST" && find . -maxdepth 1 \( -name '*.tar.gz' -o -name '*.zip' \) -printf '%f\n' | sort | xargs -r sha256sum > SHA256SUMS )
echo "SHA256SUMS:"; sed 's/^/  /' "$DIST/SHA256SUMS"
