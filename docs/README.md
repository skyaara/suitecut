# suitecut documentation

Start with the [guide](guide.md). Component READMEs live next to their code:
[native components](../native/README.md), [CEF runtime](../native/browser/README.md),
[C/C++ client SDK](../native/client/README.md),
[browser-control package](../packages/browser-control/README.md) and
[shared-browser package](../packages/shared-browser/README.md).

## Guides and reference

| Document                                                                  | What it covers                                                                                      |
| ------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| [suitecut native guide](guide.md)                                         | Setup, recording, narration, rendering and streaming with the Node API                              |
| [Build requirements and local verification](building.md)                  | Building the JavaScript package, C++ SDK and CEF runtime, and checking each                         |
| [Native browser-control SDK](native-browser-sdk.md)                       | The `@suitecut/browser-control` API for the native CEF browser                                      |
| [Live browser viewer and human handover](native-browser-viewer.md)        | The local viewer, taking control from an agent and changing the resolution                          |
| [Shared browser: SDK and MCP](shared-browser.md)                          | `@suitecut/shared-browser` (preview): its control protocol, SDK, MCP server and bot-detection notes |
| [Native continuous publishing](native-publishing.md)                      | RTMP/RTMPS and WebM publishing from the native browser                                              |
| [Native tracing and coverage](native-tracing.md)                          | Trace sessions, captures and coverage recorded in C++                                               |
| [Native full runtime](native-full-runtime.md)                             | The Linux x64 archive with the SDK, CLI, engine, CEF and FFmpeg                                     |
| [Native macOS ARM64 runtime](native-full-runtime-macos.md)                | Building, signing and qualifying the macOS runtime                                                  |
| [Benchmarks](benchmarks.md)                                               | Latest capture measurements and the commands that reproduce them                                    |
| [CEF real-browser regression suite](cef-regressions.md)                   | The local suite that runs the real CEF executable                                                   |
| [Playwright feature matching specification](playwright-feature-parity.md) | Behavior reference and feature checklist, with backend limits                                       |

## Feature reference

Behavior of individual native browser and runner features.

| Feature                                                                                     | What it covers                                                      |
| ------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| [Native API context storage](features/native-api-storage.md)                                | Origin state kept by an independent `NativeAPIRequestContext`       |
| [Native context permissions](features/native-context-permissions.md)                        | `grantPermissions`, `clearPermissions` and the CEF permission table |
| [Native debugger collected-script cache](features/native-debugger-script-cache.md)          | The collected-script source cache, now left to V8's own policy      |
| [Native HAR metadata](features/native-har-metadata.md)                                      | HAR `postData` params and other recorded metadata                   |
| [Native locator handlers](features/native-locator-handlers.md)                              | `addLocatorHandler` and `removeLocatorHandler` for overlays         |
| [Native locator suggestions](features/native-locator-suggestions.md)                        | `locator.suggest()` and `elementHandle.suggestLocator()`            |
| [Native directory discovery and coroutine cohorts](features/native-multi-file-discovery.md) | Test discovery from a `testDir` with `testMatch` and `testIgnore`   |
| [Native page close and beforeunload](features/native-page-before-unload.md)                 | `page.close({ runBeforeUnload: true })` and its dialog              |
| [Native Page and Frame form selectors](features/native-page-form-selectors.md)              | `check`, `selectOption`, `setInputFiles` and their `strict` option  |
| [Native pointer input](features/native-pointer-input.md)                                    | Hover targeting, actionability and its options                      |
| [Native child renderer recovery](features/native-renderer-recovery.md)                      | `page.reload()` after a child page's renderer crashes               |
| [Native assertion-deadline receipts](features/native-soft-assertion-deadline.md)            | Telling a deadline with no observation from a mismatch              |
| [Native soft assertion bridge](features/native-soft-assertions.md)                          | `expectNative.soft()` for locators and pages                        |
| [Targeted native keyboard ownership](features/native-targeted-keyboard.md)                  | Locator and element `press`, `type` and `sendKeys`                  |
| [Native callback assertions](features/native-to-pass.md)                                    | `expectNative(callback).toPass()`                                   |
| [Native WebSocket transport control](features/native-websocket-routing.md)                  | `webSocketRouting` on a custom CEF build                            |
| [Original-widget mouse control](features/native-widget-mouse.md)                            | The isolated Chromium mouse-lease extension                         |
| [Native BackForwardCache launch policy](features/native-worker-cache-policy.md)             | The `backForwardCache: false` launch option                         |

## Internals

[Engineering notes](internals/README.md): design contracts, verification records,
audits and upstream CEF reports. They are not user documentation.
