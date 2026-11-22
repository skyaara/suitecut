# suitecut documentation

suitecut is a native product: the embedded CEF browser engine
(`suitecut-browser`), the C++20 client SDK with its versioned C ABI, the
`suitecut-native-control` command line and MCP server, and the macOS embed kit
for apps. Component READMEs live next to their code:
[native components](../native/README.md), [CEF runtime](../native/browser/README.md),
[C/C++ client SDK](../native/client/README.md) and its
[architecture](../native/client/ARCHITECTURE.md).

## Guides and reference

| Document                                                         | What it covers                                                                 |
| ---------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| [Build requirements and local verification](building.md)         | Building and checking the C++ SDK, the CEF runtime and the embed kit           |
| [Native full runtime](native-full-runtime.md)                    | The Linux x64 archive with the SDK, CLI, engine, CEF and FFmpeg                |
| [Native macOS ARM64 runtime](native-full-runtime-macos.md)       | Building, signing and qualifying the macOS runtime                             |
| [Native continuous publishing](native-publishing.md)             | The engine's `streaming` command: RTMP, RTMPS and TCP publishing, reconnecting |
| [Shared-browser prototype](shared-browser.md)                    | The engine's own-tab-strip window mode and its stdin/stdout control protocol   |
| [CEF real-browser regression suite](cef-regressions.md)          | The local suite that runs the C++ workflows against the real CEF executable    |
| [Benchmarks](benchmarks.md)                                      | Latest capture measurements and the commands that reproduce them               |

The competitor benchmark (suitecut against agent-browser and browser-use) has
its own [README](../benchmarks/README.md).

## Embedding

suitecut as a browser runtime other apps embed, share with an AI agent and
control. The direction's decisions were made by delegation on 2026-11-20;
the API design marks what is built and what is still proposed.

| Document                                                             |
| -------------------------------------------------------------------- |
| [The macOS embed kit](embedding/embed-kit-macos.md)                  |
| [Embedding API and session protocol design](embedding/api-design.md) |
| [Direction: an embeddable browser runtime](embedding/direction.md)   |

## Feature reference

Behavior of individual engine features.

| Feature                                                                            | What it covers                                                                  |
| ---------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| [Native context permissions](features/native-context-permissions.md)               | `Context::grant_permissions`, `clear_permissions` and the CEF permission table  |
| [Native debugger collected-script cache](features/native-debugger-script-cache.md) | The collected-script source cache, now left to V8's own policy                  |
| [Native HAR metadata](features/native-har-metadata.md)                             | HAR `postData` params, compression, cookies and other recorded metadata         |

Notes on the engine's patches to CEF and Chromium (WebSocket routing,
original-widget input receipts and others) are in
[`native/browser/cef-patches`](../native/browser/cef-patches/README.md).

## Internals

[Engineering notes](internals/README.md): design contracts, verification records,
audits and upstream CEF reports. They are not user documentation.
