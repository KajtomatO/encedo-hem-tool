#!/usr/bin/env bash
#
# fetch-sdk.sh — the SDK package the release binary is built against
# (REQ-PKG-001/003): downloads encedo-hem-<version>-<os>-x86_64.{tar.gz|zip}
# and SHA256SUMS from the SDK's GitHub release, verifies the archive against
# the checksums, unpacks it into a NEW directory of its own and prints the
# package's path (the CMAKE_PREFIX_PATH for HEM_TOOL_SDK_MODE=package).
#
# implements: REQ-PKG-001
#
#   scripts/release/fetch-sdk.sh [<version>] <linux|windows> <dir>
#
# <version> defaults to the pin in cmake/sdk-version.cmake — the only version
# a release may use; CI and package.sh refuse any other.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PIN="$(sed -n -E 's/^set\(HEM_TOOL_SDK_VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)\).*/\1/p' "$ROOT/cmake/sdk-version.cmake")"
[ -n "$PIN" ] || { echo "fetch-sdk.sh: cannot read HEM_TOOL_SDK_VERSION from cmake/sdk-version.cmake" >&2; exit 1; }

if [ "$#" -eq 2 ]; then
  VERSION="$PIN"; OS="$1"; DIR="$2"
else
  VERSION="${1:?usage: fetch-sdk.sh [<version>] <linux|windows> <dir>}"; OS="${2:?linux|windows}"; DIR="${3:?dir}"
fi
case "$OS" in linux) EXT=tar.gz ;; windows) EXT=zip ;; *) echo "fetch-sdk.sh: os must be linux or windows" >&2; exit 1 ;; esac
[ "$VERSION" = "$PIN" ] || echo "fetch-sdk.sh: WARNING: $VERSION is not the pinned SDK version $PIN (package.sh will refuse it)" >&2

REPO="${HEM_SDK_REPO:-KajtomatO/encedo-hem-c-api}"
BASE="https://github.com/$REPO/releases/download/v$VERSION"
NAME="encedo-hem-$VERSION-$OS-x86_64"

if [ -e "$DIR" ] && [ -n "$(ls -A "$DIR")" ]; then
  echo "fetch-sdk.sh: $DIR exists and is not empty - downloads go into a directory of their own" >&2
  exit 1
fi
mkdir -p "$DIR"
cd "$DIR"
echo "fetching $NAME.$EXT and SHA256SUMS from $BASE"
curl -fsSL -o "$NAME.$EXT" "$BASE/$NAME.$EXT"
curl -fsSL -o SHA256SUMS "$BASE/SHA256SUMS"
grep -q -- " \*\?$NAME\.$EXT\$" SHA256SUMS || { echo "fetch-sdk.sh: $NAME.$EXT is not listed in the release's SHA256SUMS" >&2; exit 1; }
sha256sum -c --ignore-missing SHA256SUMS
if [ "$OS" = windows ]; then unzip -q "$NAME.zip"; else tar xzf "$NAME.tar.gz"; fi
[ -f "$NAME/lib/cmake/encedo-hem/encedo-hem-config.cmake" ] \
  || { echo "fetch-sdk.sh: $NAME holds no encedo-hem CMake package" >&2; exit 1; }
rm -f "$NAME.$EXT"
echo "SDK package: $(pwd)/$NAME"
