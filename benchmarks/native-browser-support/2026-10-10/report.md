# SuiteCut core web-platform support and native browser audit

Human-readable timestamps use Asia/Calcutta (IST); original machine logs retain UTC/monotonic timestamps. Final feature run began **2026-10-10T17:42:12.350000+05:30** on October 10, 2026.

## Outcome

**17 functional checks passed**, including all **15 required** checks. WebGPU had no adapter and is explicitly unavailable. The requested WebSockets, WebAssembly, IndexedDB, dedicated/shared/service workers, WebRTC and WebGL 1/2 are supported by the tested Linux CEF binary. SwiftShader was explicitly selected for WebGL on this CPU-only host. These APIs already exist in Chromium; the implementation adds a native graphics choice and real regression coverage rather than separate replacement web engines.

- WebSockets: actual local text echo.
- WASM: module instantiation and execution returning 42.
- IndexedDB: transaction write/read and read after document reload. Profiles remain temporary; this is not persistence across engine restarts.
- Workers: actual dedicated/shared message round trips; service-worker registration, control and fetch interception; CacheStorage read/write.
- WebRTC: actual local two-peer data channel and canvas-video transmission/decoding. Decoded pixel `[20,180,41,255]` matched the source within video color-conversion tolerance. Remote ICE/STUN/TURN, real camera/microphone/screen-sharing and full worker-target automation are unverified.
- WebGL 1/2: compile/link actual shaders, draw full-surface triangles and read exact red/green pixels. Renderers identified ANGLE Vulkan SwiftShader. Both shaders continued drawing for more than 100 callbacks.
- Capture: composited screenshot pixels and decoded native FFV1 recording pixels match both canvases. Complete recorded file decoding passed.
- HTML-to-PDF and permission-granted clipboard: actual export/write/read; saved PDF was parsed and expected document text verified.

The three-second 640×360 30-FPS GL recording produced **92 encoded frames / 3.0667 seconds**, **19 repeated frames**, maximum reported encode time **59.12 ms**, empty stopped queues and no output error. It intentionally had no audio. A static-color shader/redraw test verifies capture correctness; repeated output frames are not proof of video loss or high graphics throughput. This short functional run is not a performance comparison, long-duration memory soak or audio-continuity benchmark.

## Implementation

`src/native-browser.ts` validates and forwards `graphics: 'auto' | 'swiftshader'`; `launchEngine` and root `launchBrowser` forward the same setting. C++ `App::OnBeforeCommandLineProcessing` owns graphics selection. Unknown SDK modes fail before spawning, and the native entry rejects unknown modes as well.

Auto retains Chromium graphics defaults. Explicit SwiftShader sets `--use-gl=angle`, `--use-angle=swiftshader` and `--enable-unsafe-swiftshader`. Chromium requires the last opt-in because software WebGL fallback has reduced security protections. It is not silently enabled globally. No renderer-sandbox bypass was added. Hardware graphics remains unverified. The host's default path and an ANGLE/Mesa OpenGL trial could not create a GL context; the SwiftShader path did. WebGPU stayed unavailable in every profile.

A separate real adapter defect was fixed during the broader browser audit: `Browser.setWindowBounds` previously replied successfully without resizing, and all targets reported window ID 1. It now uses real per-target virtual IDs, applies page dimensions, preserves sibling dimensions and rejects unsupported native window states/placements and invalid bounds. Resizing active fixed-size raw subscriptions is rejected. A new regression demonstrably fails against the saved old executable and passes after the change.

Playwright was initially adding desktop Chrome toolbar insets to the windowless surface. Private `Browser.getVersion` metadata now identifies this surface as headless, so Playwright uses its actual viewport dimensions. This private automation metadata does not change the website/HTTP user agent or browser product/version. An intermediate run failed 14/16 tests before this correction; those logs remain saved.

## Validation and retained failures

- Final functional web-platform run: 17 passed, one optional WebGPU unavailable (`features-final-core/features.json`).
- Playwright CEF integration: **16/16 passed**, including scrolls, input, iframes, network routes, popup/dialog/download/upload flows, isolation, resize, recording/pause/selection and shutdown (`cef-integration-final.log`).
- Native CTest: **7/7 passed** (`native-tests-final.log`).
- Focused SDK lifecycle/session/recording/broadcast/page tests: **29/29 passed** (`sdk-tests-final.log`).
- TypeScript build/typecheck, ESLint, Prettier and whitespace validation passed.
- Progressive H.264/AAC and MSE playback regression evidence is in `playback-final-loaded/` and its log.

Initial WebSocket fixture failure is retained: the test server mistakenly echoed binary frames, then was corrected to text. Default/Mesa unavailable GL probes are retained separately from the passing software-mode runs. An initial playback rerun stalled during evaluation in the startup blank document; it was interrupted after more than 80 seconds. The collector now navigates to an HTTP document before probing. This establishes the rerun precondition, not a complete diagnosis/fix of possible startup-document CDP races. `playback-final/harness-interruption.json` records the interruption.

The evidence snapshot initially entered the TypeScript project under a new folder name. It was moved to the established excluded `tested-sources` convention and final typecheck/lint rerun. The packaging-related failure log remains saved.

## Reproduction

Build/install instructions and exact pinned CEF/library digests are in the saved native source and `runtime-dependencies.json`. On this headless Linux host, the existing owned Xvfb/PulseAudio harness runs:

```sh
MEMORY_DIRECTORY=$PWD/.suitecut/core-features \
SUITECUT_GRAPHICS=swiftshader \
SUITECUT_RUN2026-10-10T17:42:12.350000+05:30_EXECUTABLE=$PWD/.suitecut/native-build/suitecut-browser \
python3 benchmarks/native-memory/runtime.py benchmarks/native-browser-support/features.mjs
```

With display/audio already available, `SUITECUT_GRAPHICS=swiftshader pnpm test:native:features` builds the SDK and runs the same collector. Set `SUITECUT_NATIVE_EXECUTABLE` to the native executable. Auto is the default for suitable graphics hosts. WebGL is required by the final collector; `SUITECUT_ALLOW_MISSING_GL=1` permits diagnostic availability runs only. No unavailable GL probe is called a rendering pass.

## Saved evidence and practical limits

This directory contains all trial and final logs, screenshots/PDFs, decoded/recorded media, host details, before/after executables, current source/SDK snapshots, workspace patch, native graphics libraries and dependency digests. `checksums.sha256` seals payloads. The companion archive is `/workspace/shared/suitecut-browser-core-features-2026-10-10.tar.gz`. The pinned full CEF SDK is already preserved in the previous codec archive; `runtime-dependencies.json` names that dependency and its checksum, avoiding duplicating the SDK here.

All **6,954** previously sealed payloads were verified unchanged (`previous-seals.json`). The full capability/limit matrix is `native/browser/BROWSER_SUPPORT.md`.

Linux x64 codec-enabled CEF **152.0.6 / Chromium 152.0.7977.83** is a pinned prebuilt third-party distribution, not an independently rebuilt/audited newer browser. macOS/Windows and GPU encoding remain unverified in this work. Extensions/Chrome UI and licensed DRM remain separate integrations. Full upstream Playwright compatibility, graphics/device breadth and dependency sanitizer coverage remain incomplete. Prior uncovered source audio is still an independent open issue; the silent graphics recording does not resolve it. No new OBS comparison, memory-safety guarantee, leak-free long-run claim or performance-superiority claim is made.
