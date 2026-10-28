# encedo-hem-tool

`hem-tool` — the command-line client of the [Encedo HEM](https://ence.do)
network cryptographic device: status and check-in, key management, signing,
random bytes, logs and self-test, TLS certificate recovery, device
initialisation and wipe, and pairing of the Encedo Auth phone app. One
specification ([requirements/](requirements/), [ARCHITECTURE.md](ARCHITECTURE.md) §3),
implemented per language SDK.

> **Status: hem-tool 1.3.0 (2026-10-28)** — the first release from this
> repository: the C implementation, moved here from
> [encedo-hem-c-api](https://github.com/KajtomatO/encedo-hem-c-api) at its
> 1.2.0 and built on that SDK (milestone T1; gate passed 2026-10-27, tested
> as `c-v1.3.0-rc1` on Linux and Windows). Binaries: the
> [latest release](https://github.com/KajtomatO/encedo-hem-tool/releases/latest)
> here; the SDK's own releases carry no hem-tool from its 1.3.0 on.

| Implementation | Directory | SDK | Version | Platforms |
|---|---|---|---|---|
| C | [`c/`](c/) | [encedo-hem-c-api](https://github.com/KajtomatO/encedo-hem-c-api) 1.2.0 (pinned, `c/cmake/sdk-version.cmake`) | **1.3.0** (`c-v1.3.0`, 2026-10-28) | Linux x86_64, Windows x86_64 |

## Download

Releases are per implementation, tagged `<impl>-v<version>`. The C tool is
**one file**: `hem-tool-<version>-linux-x86_64.tar.gz` /
`hem-tool-<version>-windows-x86_64.zip` (OpenSSL and libcurl linked in,
nothing to install beside it) plus `SHA256SUMS`. Verify before running:

```sh
sha256sum -c SHA256SUMS --ignore-missing   # Linux
CertUtil -hashfile <file> SHA256            # Windows
```

## Usage

Commands are grouped by what they need — none (`status`, `checkin`,
`init-device`, `init-wizard`, `keys types`), any bearer (`keys`, `sign`,
`random`, `logs`, `selftest`, `reboot`, `ext list/login`), or a passphrase
only (`ext pair`, `wipe-device`, `recovery`); `hem-tool --help` shows the
grouped summary, `hem-tool help <command>` the per-command page.

```bash
hem-tool status                     # no --url needed: defaults to https://my.ence.do
hem-tool keys list --mobile         # any bearer command can push to the paired phone
hem-tool keys types                 # what each key type is for (no device needed)
hem-tool --version                  # tool version, the SDK it embeds, crypto and transport
hem-tool recovery                   # diagnose + fix TLS / certificate / clock trouble
hem-tool --url http://<host> init-wizard   # personalise a wiped device, one question at a time
hem-tool ext pair                   # pair the Encedo Auth app from a terminal QR
```

Connection comes from `--url`/`EHEM_URL` (default `https://my.ence.do`),
credentials from `EHEM_PASSPHRASE` or `--mobile` (push confirmation; exit
13 = no answer, 14 = rejected). `--passphrase PW` is deprecated. Exit codes:
0 success, 1 failure, 2 usage error, 12 the crypto library could not be
initialised, and per-command codes listed in each command's help and in
[conformance/exit-codes.json](conformance/exit-codes.json).

## Building from source

Each implementation builds on its own: for C see [c/README.md](c/README.md)
(`git submodule update --init`, then `cd c && ./dev build`).

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) — decisions, the CLI, test policy, the
  implementations, milestones.
- [requirements/](requirements/) — the specification (`REQ-TOOL-*`), the
  per-implementation packaging requirements (`REQ-PKG-*`), the generated
  [TRACE.md](requirements/TRACE.md).
- [conformance/](conformance/) — the vectors every implementation checks.
- [KNOWN-ISSUES.md](KNOWN-ISSUES.md), [BACKLOG.md](BACKLOG.md).
- [REQUIREMENTS-MANAGEMENT.md](REQUIREMENTS-MANAGEMENT.md) — how the work is
  tracked.

## Layout

```
requirements/  conformance/  workplan/  scripts/   the shared layer
c/                                                  the C implementation (CMake; sdk/ submodule)
.github/workflows/                                  c-ci.yml, c-release.yml
```

## License

MIT (see [LICENSE](LICENSE)). The released C binaries bundle the Encedo HEM
C SDK (MIT), libcurl, OpenSSL (Apache-2.0), cJSON, the BIP39 word list and
Project Nayuki's QR encoder (MIT); their notices ship in each archive's
`LICENSES/`.
