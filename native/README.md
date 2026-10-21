# Native components

Each component is a separate CMake project with a specific runtime boundary.
The [CEF runtime architecture](browser/ARCHITECTURE.md) describes its compiled
protocol, engine, page and process modules, controller domains and ownership.

| Directory          | Responsibility                                                        |
| ------------------ | --------------------------------------------------------------------- |
| `browser/`         | Embedded CEF runtime, native control and capture implementation       |
| `client/`          | Standalone C/C++ SDK and command-line examples; builds without CEF    |
| `runner/`          | Native test runner                                                    |
| `resource-policy/` | Shared resource policy component                                      |
| `tools/`           | Native media/tool packaging                                           |
| `shared/`          | CEF-free DOM/ARIA helpers shared by the runtime and external backends |

The C++ SDK uses `include/suitecut/` for installed headers, `src/` for private
implementation, `examples/` for consumers, and `tests/{unit,integration,support}`
for separately built test targets. Its install/export contract remains
`find_package(SuiteCutClient)` and `SuiteCut::Client`.
Implementation modules and source distribution are documented in the
[SDK architecture guide](client/ARCHITECTURE.md). The SDK source archive includes
the shared helpers and licenses without CEF sources or browser binaries.

```sh
cmake --preset release -S native/client
cmake --build .suitecut/build/client-release --parallel 2
ctest --test-dir .suitecut/build/client-release --output-on-failure -L unit
```

Presets also provide `sanitizers` for Debug ASan/UBSan checks. They are local
commands; no CI jobs are installed. Unit checks are labeled `client;unit` and
have hard timeouts. Browser integrations run through the isolated
[CEF regression package](../tests/cef/README.md), with an explicitly supplied
runtime. Build its five C++ dependencies using target `client-cef-binaries`.
