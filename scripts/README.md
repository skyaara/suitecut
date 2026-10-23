# Scripts

Maintainer scripts for building, packaging and verifying SuiteCut. Most need a
built CEF executable or local toolchains, and none publish anything. The common
ones are wired to `package.json` scripts; run those rather than the files directly.

| Pattern                                                | Purpose                                                                 | Examples                                                  |
| ------------------------------------------------------ | ----------------------------------------------------------------------- | --------------------------------------------------------- |
| `verify-native-*.mjs`, `verify-native-*.py`            | Real-browser checks of one native feature against the built engine      | `pnpm test:native`, `pnpm test:native:cadence`            |
| `check-native-runner-*.py`                             | Process-level checks of the native test runner (`suitecut-test`)        | `python3 scripts/check-native-runner-repeat-each.py`      |
| `verify-*.mjs`                                         | Package, plugin and release verification                                | `pnpm test:package:built`, `pnpm test:audio-plugin:built` |
| `sdk-*.mjs`                                            | Browser-control SDK fuzzing, hardening and performance reviews          | `scripts/sdk-browser-stress.mjs`                          |
| `package-*.py`, `sign-*.py`, `native_macos_signing.py` | Build relocatable native runtimes and sign macOS bundles                | [macOS runtime](../docs/native-full-runtime-macos.md)     |
| `check-lfs-assets.mjs`, `clean-dist.mjs`               | Build helpers run by `prepack` and `build`                              | `pnpm check:lfs`                                          |
| `run-song-maker-example.py`                            | Pack both SDKs into a temporary consumer and run the Song Maker example | `pnpm example:song-maker`                                 |

Scripts stay in one flat directory because many locate the repository root
relative to their own path. Shared Python helpers (`native_*.py`) are imported by
the `check-native-runner-*` scripts beside them.
