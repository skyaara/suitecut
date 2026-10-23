# SuiteCut documentation

Start with the [guide](guide.md). Component READMEs live next to their code:
[native components](../native/README.md), [CEF runtime](../native/browser/README.md),
[C/C++ client SDK](../native/client/README.md) and
[browser-control package](../packages/browser-control/README.md).

## Guides and reference

| Document                                                                  |
| ------------------------------------------------------------------------- |
| [SuiteCut native guide](guide.md)                                         |
| [Build requirements and local verification](building.md)                  |
| [Native browser-control SDK](native-browser-sdk.md)                       |
| [Live browser viewer and human handover](native-browser-viewer.md)        |
| [Native continuous publishing](native-publishing.md)                      |
| [Native tracing and coverage](native-tracing.md)                          |
| [Native full runtime](native-full-runtime.md)                             |
| [Native macOS ARM64 runtime](native-full-runtime-macos.md)                |
| [Benchmarks](benchmarks.md)                                               |
| [CEF real-browser regression suite](cef-regressions.md)                   |
| [Playwright feature matching specification](playwright-feature-parity.md) |

## Feature reference

Behavior of individual native browser and runner features.

| Feature                                                                                     |
| ------------------------------------------------------------------------------------------- |
| [Native API context storage](features/native-api-storage.md)                                |
| [Native context permissions](features/native-context-permissions.md)                        |
| [Native debugger collected-script cache](features/native-debugger-script-cache.md)          |
| [Native HAR metadata](features/native-har-metadata.md)                                      |
| [Native locator handlers](features/native-locator-handlers.md)                              |
| [Native locator suggestions](features/native-locator-suggestions.md)                        |
| [Native directory discovery and coroutine cohorts](features/native-multi-file-discovery.md) |
| [Native page close and beforeunload](features/native-page-before-unload.md)                 |
| [Native Page and Frame form selectors](features/native-page-form-selectors.md)              |
| [Native pointer input](features/native-pointer-input.md)                                    |
| [Native child renderer recovery](features/native-renderer-recovery.md)                      |
| [Native assertion-deadline receipts](features/native-soft-assertion-deadline.md)            |
| [Native soft assertion bridge](features/native-soft-assertions.md)                          |
| [Targeted native keyboard ownership](features/native-targeted-keyboard.md)                  |
| [Native callback assertions](features/native-to-pass.md)                                    |
| [Native WebSocket transport control](features/native-websocket-routing.md)                  |
| [Original-widget mouse control](features/native-widget-mouse.md)                            |
| [Native BackForwardCache launch policy](features/native-worker-cache-policy.md)             |

## Internals

[Engineering notes](internals/README.md): design contracts, verification records,
audits and upstream CEF reports. They are not user documentation.
