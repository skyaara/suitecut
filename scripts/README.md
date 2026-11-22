# Scripts

Maintainer scripts for packaging, signing and verifying suitecut's native
runtime. They are Python 3 and use only the standard library. Most need a built
CEF executable or local toolchains, and none publish anything.

| Pattern                                                       | Purpose                                                              |
| ------------------------------------------------------------- | -------------------------------------------------------------------- |
| `package-native-runtime*.py`, `verify-native-runtime*.py`     | Build and check relocatable native runtimes (Linux x64, macOS arm64) |
| `package-embed-kit-macos.py`, `build-embed-kit-deps-macos.py` | Build the signed macOS embed kit and its dependencies                |
| `verify-embed-kit-macos.py`, `verify-embed-sample-macos.py`   | Check the embed kit and the sample app that uses it                  |
| `package-napdock-browser-macos.py`                            | Package the engine for Napdock's Browser extension                   |
| `sign-native-macos.py`, `native_macos_signing.py`             | Sign macOS bundles with the shared signing policy                    |
| `verify-native-macos-recording.py`                            | Measure encoded frames and audio from a native runtime's recording   |
| `run-cef-regressions.py`, `cef_regression_processes.py`       | Compatibility entry point and process helpers for `tests/cef`        |
| `verify-cef-upstream.py`, `check-cef-fix-bundle.py`           | Run CEF's own parser and string tests; check CEF patch bundles       |
| `fuzz-native-control.py`                                      | Fuzz the native session IPC with a supplied test engine              |

Scripts stay in one flat directory because several locate the repository root
relative to their own path.
