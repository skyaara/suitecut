# suitecut

suitecut is a Chromium browser runtime built on CEF. Programs control it from
C++, C, a command-line tool or MCP, and native apps embed its pages in their own
windows. Everything that ships is native code; Node.js is used only to build the
website and run the benchmark harness.

The earlier Node.js package (recording, narration and rendering, the
`@suitecut/browser-control` and `@suitecut/shared-browser` npm SDKs, and the test
runner) is kept on the `archive/node` branch. It is not developed further.

## What's here

| Path                      | Contents                                                                                                 |
| ------------------------- | -------------------------------------------------------------------------------------------------------- |
| `native/browser/`         | The CEF engine, `suitecut-browser` ([README](native/browser/README.md))                                  |
| `native/client/`          | C++20 client SDK, versioned C ABI, `suitecut-native-control` CLI and MCP server, macOS embed kit         |
| `native/shared/`          | CEF-free DOM and ARIA helpers shared by the engine and the Chrome and Firefox backends                   |
| `native/resource-policy/` | Shared resource policy component                                                                         |
| `tests/cef/`              | Real-browser regression suite for the C++ workflows ([README](tests/cef/README.md))                      |
| `scripts/`                | Packaging, signing and runtime verification scripts in Python ([index](scripts/README.md))               |
| `docs/`                   | Build, embedding and engineering notes ([index](docs/README.md))                                         |
| `benchmarks/`             | Benchmark harness comparing suitecut with agent-browser and browser-use ([README](benchmarks/README.md)) |
| `site/`                   | The website                                                                                              |

## Build

The engine needs the pinned CEF 156.0.30 SDK (Chromium 156.0.8078.96), libyuv,
CMake 3.21+, Ninja and a C++20 compiler. macOS arm64 uses a codec-enabled build
of the same CEF tag, so H.264, AAC and HEVC play there; Linux x64 uses the
official minimal archive. [Build requirements](docs/building.md) and the
[engine README](native/browser/README.md#build) give the full steps.

```sh
cmake -S native/browser -B .suitecut/native-build -G Ninja \
  -DCEF_ROOT="$PWD/.suitecut/cef-sdk" -DLIBYUV_ROOT="$PWD/.suitecut/libyuv" \
  -DPROJECT_ARCH=arm64 -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/native-build --target suitecut-browser --parallel 2
```

The client SDK builds without CEF:

```sh
cmake --preset release -S native/client
cmake --build .suitecut/build/client-release --parallel 2
ctest --test-dir .suitecut/build/client-release --output-on-failure -L unit
```

## Control the browser

From the command line, one session per name:

```sh
suitecut-native-control --session demo --browser /absolute/path/to/suitecut-browser open https://example.com
suitecut-native-control --session demo snapshot
suitecut-native-control --session demo fill e12 'Ada'
suitecut-native-control --session demo click e15
```

`suitecut-native-control --browser /absolute/path/to/suitecut-browser --mcp`
runs the same tools as an MCP server. From C++, `SimpleAgent` offers `observe`,
`act` and `wait`:

```cpp
#include <suitecut/simple_agent.hpp>

suitecut::AgentDriver driver(browser);
suitecut::SimpleAgent agent(driver);
auto observation = agent.call("observe", {{"boxes", true}});
auto result = agent.call("act", {{"action", "fill"}, {"ref", "e1"}, {"value", "Hello"}});
```

The [client README](native/client/README.md) covers the full C++ API, the C ABI,
the Chrome/Chromium (CDP) and Firefox (WebDriver BiDi) backends, and installing
with `find_package(SuiteCutClient)`.

## Embed in a macOS app

The [macOS embed kit](docs/embedding/embed-kit-macos.md) is one folder of signed
files an app copies into its bundle: the engine app, the `suitecut-session`
service the app talks to, licences and a `kit.json` manifest. Napdock's Browser
extension is the first app that uses it. The
[direction](docs/embedding/direction.md) and
[API design](docs/embedding/api-design.md) documents describe where embedding
is going.

## Verification

Engine and client checks run under CTest in their build directories. The
real-browser suite runs five C++ workflows, built with the client's
`client-cef-binaries` target, against an actual engine:

```sh
python3 tests/cef --engine /absolute/path/to/suitecut-browser \
  --client-build .suitecut/build/client-release
```

CI runs these on Linux and macOS for every push and pull request; see
[CI and releases](docs/ci.md).

## License

suitecut is [MIT licensed](LICENSE). CEF, Chromium, libyuv and other
third-party terms are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
