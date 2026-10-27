# The ONE declared pin of the Encedo HEM C SDK this implementation is built
# against. Three things must agree with it:
# implements: REQ-PKG-001
#   - the submodule sdk/ (git -C sdk checkout v<version>)
#   - the SDK package the release job downloads (encedo-hem-<version>-<os>-x86_64)
#   - the `sdk:` line of `hem-tool --version`
# Bumping: edit this value, move the submodule to the matching tag, run the
# test suite, then the package-mode build (c/docs/RELEASING.md).
set(HEM_TOOL_SDK_VERSION 1.2.0)
