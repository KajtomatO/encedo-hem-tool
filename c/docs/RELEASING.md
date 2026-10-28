# Releasing the C hem-tool

One asset per platform — the one-file `hem-tool-<version>-<os>-x86_64`
(`.tar.gz` on Linux, `.zip` on Windows; OpenSSL and libcurl linked in) —
plus `SHA256SUMS`, published by `.github/workflows/c-release.yml` from an
annotated `c-v<version>` tag (REQ-PKG-003). The binary is built in
**package mode** against the SDK's released static package of the pinned
version, never against the submodule.

## What the workflow does

| Trigger | Result |
|---|---|
| push to `main`, `workflow_dispatch` | both platforms built and packaged; the archives are **workflow artifacts** labelled `<version>+g<sha>`. No release. |
| annotated tag `c-v<version>` | the same, then a **GitHub Release** with the two archives + `SHA256SUMS`; the release text = the tag message + the checksums. |
| annotated tag `c-v<version>-rc<N>` | the same, as a **pre-release**. |

Per platform (`ubuntu-22.04`, `windows-latest` MSYS2 UCRT64):
`scripts/release/fetch-sdk.sh <sdk-version> <os> deps-sdk` downloads
`encedo-hem-<sdk-version>-<os>-x86_64.{tar.gz|zip}` and `SHA256SUMS` from
the SDK's release, verifies and unpacks; `scripts/release/package.sh
deps-sdk <os> dist` configures `-DHEM_TOOL_SDK_MODE=package`, builds
Release, asserts, and writes the archive and `SHA256SUMS`. No dependency
prefix is built — the SDK package carries `libcurl.a`, `libssl.a`,
`libcrypto.a` and the headers. The release job merges the two `SHA256SUMS`,
checks every file and that no name appears twice.

## Rules the workflow enforces

- **The tag must match the project version** — `project(hem-tool VERSION
  x.y.z)` in `CMakeLists.txt`; `package.sh` accepts only `c-v<version>` or
  `c-v<version>-rc<N>` (N ≥ 1). Anything else fails before the build.
- **The tag must be annotated** (`git tag -a`). Its message is the release
  text (Markdown; the workflow appends the checksums).
- **The SDK pin agrees** — `cmake/sdk-version.cmake`, the submodule's tag
  and the fetched package are the same version (checked in CI).
- **Asserts** before packaging: Linux `ldd` = glibc-family only, no RUNPATH;
  Windows = system DLLs + UCRT only; no wolfSSL symbol or string;
  `--version` prints exactly `hem-tool <version>`, `sdk:       encedo-hem
  <sdk-version>`, the OpenSSL and libcurl lines with `static`; the archive
  holds exactly one `hem-tool[.exe]`; `status` against a closed port exits
  1; a null-provider `OPENSSL_CONF` exits 12 on Linux and 1 on Windows.
- **Checksums are verified** in the publishing job.
- **Every file the workflow calls must be committed** in the tagged commit.

## Version number

`project(VERSION)` in `CMakeLists.txt` is the only definition; `--version`,
the help header and the asset names derive from it. A bug fix is a patch, a
new command or flag a minor, a changed exit code or a removed flag a major.
The SDK version is independent: bumping the pin alone is a patch unless the
SDK change is visible in the tool.

## Bumping the SDK pin

1. Edit `cmake/sdk-version.cmake` (`HEM_TOOL_SDK_VERSION`).
2. `git -C sdk fetch --tags && git -C sdk checkout v<version>`; commit both.
3. `./dev ci` and `./dev test unit --wolfssl` (the seams and fixtures come
   from the submodule — a renamed seam shows up here).
4. `scripts/release/fetch-sdk.sh <version> linux deps-sdk &&
   scripts/release/package.sh deps-sdk linux dist` — the package-mode build
   and its asserts against the real package.
5. Push; the package-mode CI legs repeat step 4 on both platforms.

## Where to tag

Release candidates are tagged on `development` (the branch CI runs on);
the final tag goes on `main` after the merge. Never move or reuse a
published tag.

## Procedure

### 1. Release candidate

```sh
git switch development && git pull
git status                      # clean
git tag -a c-v1.3.0-rc1 -m "hem-tool 1.3.0-rc1 - <what this candidate is for>"
git push origin c-v1.3.0-rc1
```

Wait for the run (Actions → c-release; the `c-v1.3.0-rc1` run of 2026-10-27
took 2:37 end to end — Linux 0:30, Windows 2:18, publish 0:12; the final
`c-v1.3.0` run of 2026-10-28 2:48; C CI runs on the tag as well, 2–6
minutes depending on the wolfSSL leg's cache), then check the pre-release: two
archives + `SHA256SUMS`. Test as a user would, on both platforms:

1. Download the archive for your platform and `SHA256SUMS`; verify
   (`sha256sum -c SHA256SUMS --ignore-missing`, or `CertUtil -hashfile`).
2. Unpack alone into an empty directory. `hem-tool --version` must show the
   tool version, the SDK version and `static` on both library lines.
3. `hem-tool status` against the device with system trust (no `--cacert` /
   `--insecure`); then whatever the milestone needs (`ext list`, a pairing,
   the wizard on a spare unit).

A broken candidate is fixed on `development` and replaced by the next rc
number; delete the bad pre-release in the GitHub UI and its tag if you want
(`git push --delete origin c-v1.3.0-rc1 && git tag -d c-v1.3.0-rc1`).

### 2. Final release

Before tagging: the milestone gate has passed (ARCHITECTURE.md §7) and an
rc of the same code was tested; `CMakeLists.txt` carries the version; the
user-facing docs describe it — the root `README.md` (the implementations
table: version and date; Download), `c/README.md`, `KNOWN-ISSUES.md`; CI is
green. The release text is written last, from the README and the milestone
entry; keep it Markdown.

```sh
# merge development -> main (pull request), then:
git switch main && git pull
git tag -a c-v1.3.0 --cleanup=whitespace -F release-notes-c-1.3.0.md
git push origin c-v1.3.0
```

Keep `--cleanup=whitespace`: without it git drops every line starting with
`#`, and the Markdown headings vanish from the release text. Then check the
run and the release, and smoke-test the published assets once more.
