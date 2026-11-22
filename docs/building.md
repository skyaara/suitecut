# Build requirements and local verification

Choose the component you need. The standalone C++ SDK and the embedded CEF
runtime have separate build and distribution boundaries. Browser
binaries, build directories, local signing policies and qualification reports
are not source dependencies committed to Git.

| Component                 | Requirements                                                                                                                                                                                                                                                                |
| ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Standalone C/C++ SDK      | CMake 3.20 or newer; C++20 compiler and C toolchain; threads, OpenSSL and ZLIB development libraries                                                                                                                                                                        |
| Embedded CEF runtime      | CMake 3.21 or newer; C++20 toolchain; Ninja for the commands below; exact platform CEF SDK matching `native/browser/cef-build.json`; libyuv source pinned in the browser build guide; curl 7.88 or newer, OpenSSL 3 or newer, libpsl, ZLIB and Brotli development libraries |
| macOS runtime             | Apple Silicon build uses the matching ARM64 CEF SDK, Apple Clang with Objective-C++, and Apple frameworks including Metal, IOSurface and VideoToolbox; Python 3 for signing                                                                                                 |
| macOS embed kit           | The macOS runtime requirements, plus static curl, OpenSSL, libpsl and brotli built for macOS 14 by `scripts/build-embed-kit-deps-macos.py`; see [the embed kit](embedding/embed-kit-macos.md)                                                                    |
| CEF regression suite      | Python 3.11 or newer, OpenSSL CLI, an actual CEF app, the five built C++ workflows, FFmpeg and FFprobe                                                                                                                                                                      |

The standalone SDK requires no CEF headers. Browser control requires
a separately installed supported browser. Recording requires media tools; the
full-runtime archive bundles these. The CEF suite needs VP9 and Opus
support in its media tools. See [SDK setup](../native/client/README.md),
[browser setup](../native/browser/README.md#build),
[macOS packaging](native-full-runtime-macos.md) and the
[embed kit recipe](embedding/embed-kit-macos.md#recipe) for the exact recipes.

## Standalone C++ SDK

```sh
cmake --preset release -S native/client
cmake --build .suitecut/build/client-release --parallel 2
ctest --test-dir .suitecut/build/client-release --output-on-failure -L unit -j 1
cmake --install .suitecut/build/client-release --prefix "$PWD/.suitecut/sdk-install"
```

On macOS, set `CMAKE_PREFIX_PATH` to the prefix containing your OpenSSL/ZLIB
installation if CMake cannot discover it. Use `-DBUILD_SHARED_LIBS=ON` with a
separate build directory for a shared SDK. Consumers use
`find_package(SuiteCutClient CONFIG REQUIRED)` and `SuiteCut::Client`.
See the [SDK architecture](../native/client/ARCHITECTURE.md) for source archives,
relocation checks and embedding. The `sanitizers` preset enables ASan/UBSan;
LeakSanitizer availability is platform dependent.

## CEF runtime and regressions

Follow the platform browser recipe with explicit `CEF_ROOT` and `LIBYUV_ROOT`.
On macOS the engine is the executable inside the complete `.app` bundle.
Use `cmake --build BUILD_DIR --target suitecut-browser --parallel 1` on a small
Mac. Keep compiler jobs and regression workers bounded; avoid concurrent heavy
builds while measuring capture throughput. The optional
`SUITECUT_BROWSER_PRECOMPILED_HEADERS=OFF` switch removes the build optimization.

```sh
python3 -m unittest discover -s tests/cef/unit -p 'test_*.py'
python3 tests/cef --list
python3 tests/cef --engine /absolute/path/to/suitecut-browser \
  --client-build .suitecut/cef-regression-client --workers 1
```

The [full execution guide](cef-regressions.md) lists the required C++ workflows,
media tools, case selection, repetition and evidence contracts. Full runs retain
JSON, HTML, JUnit and per-case logs under `.suitecut/cef-regressions/`. A missing
dependency, timeout, nonzero shutdown, evidence mismatch or cleanup failure
fails the run. The CEF suite remains local; no CEF CI job is enabled.

## Platform and signing requirements

Linux x64 and macOS ARM64 are separately qualified; Windows runtime packaging
and Safari/WebKit are unsupported. CEF is pinned to the 156.0.30 stable release;
macOS arm64 uses a third-party codec-enabled build of it. The latest
local macOS checks ran on macOS 27.0.1. Homebrew dylibs may require newer macOS
than the engine's compilation target; older systems and Intel need their own
dependency builds and qualification.

Pin a locally installed signing certificate in
`~/Library/Application Support/SuiteCut/macos-signing.json`, or the ignored
project override `.suitecut/macos-signing.json`. Engine builds and packaging
reuse that identity and fail if the configured certificate is unavailable.
Other developers can use ad-hoc signing when no identity is configured. Never
commit private keys, exported certificates or Keychain databases. See the
[signing policy](native-full-runtime-macos.md) for persistent-profile access and
trusted timestamps. Signing does not imply notarization or publication.
