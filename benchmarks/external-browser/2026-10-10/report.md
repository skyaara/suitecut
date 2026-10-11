# External browser attachment: implementation and verification

Saved 2026-10-10T18:54:50.169317+05:30 (IST).

`connectBrowser()` now attaches normal Playwright automation to independently managed browser endpoints. The implementation validates settings, chooses CDP or Playwright protocol and returns the original Playwright Browser. No native CEF/media rewrite is involved in this change.

## Installed versions before this change

SuiteCut package version 2.0.0 is an unpublished development checkout. Active codec-enabled CEF is 152.0.6 / Chromium 152.0.7977.83; retained stock CEF is 154.0.34 / Chromium 154.0.8037.98. OBS 32.2.0 is the real embedded-CEF profiling installation, with OBS-patched Chromium 127.0.6533.120 (`6533-fix-stutter-and-osr-extra-info`, version from the saved profiling report). The earlier OBS external-browser control installation is 30.2.3.1-3. Both OBS executable versions were freshly verified with their private dependency paths.

Standalone Chromium is 151.0.7922.34. Playwright 1.62.1, FFmpeg 7.1.5 (system and native dependencies), Node 24.19.0, TypeScript 5.9.3, actual pnpm 11.19.0 (package declares 11.21.0), CMake 4.4.4 and GCC 14.2.0. `versions.json`, `versions-raw.json`, the native CMake cache and FFmpeg header preserve the evidence and active configuration.

## Public API

Exported from `suitecut`, `suitecut/engine`, `suitecut/playwright`, and the internal native-default adapter. `BrowserConnectionOptions` supplies endpoint, protocol, browser name, headers, connection timeout and slow-motion delay. CDP defaults to `noDefaults: true` to preserve existing context settings. Playwright protocol additionally forwards `exposeNetwork` rules. Invalid protocol/browser combinations, URL schemes, extra fields and numeric settings fail validation before a connection attempt.

CDP accepts HTTP/HTTPS/WS/WSS and Chromium only. Playwright server attachment accepts WS/WSS and Chromium/Firefox/WebKit. Actual external-browser interaction tests use local Chromium, not SuiteCut's CEF or an in-process mock. Both browser processes retain their renderer sandbox; no `--no-sandbox` flag was used.

## Results

**27 real-browser checks passed** in the final integration run. **30 SDK unit tests passed** (17 new connection tests plus 13 existing native-browser tests). Build, typecheck, full repository lint and format checks passed. Logs are saved under `logs/`.

Three transports were exercised: Chromium HTTP CDP, Chromium WebSocket CDP and a Playwright Chromium server WebSocket. Navigation, input fill and keyboard input, auto-waited clicks, iframe interaction, popup creation/closure, wheel and locator scrolling, JS evaluation, storage, screenshots and request interception passed. A deliberately unresponsive discovery server caused the configured connection timeout. A separate context started with isolated storage.

CDP disconnect left the external process and existing tab running; reattachment retained input and localStorage state. Twenty-five additional connect/evaluate/disconnect cycles retained exactly one host tab. The Playwright server survived disconnect and accepted reconnection; contexts owned by the disconnected connection were cleaned up. Browser owners stopped their processes at the end, and the local fixture server was closed.

| Check | Result | Duration (ms) |
| --- | --- | --- |
| connection timeout: unresponsive HTTP discovery endpoint | pass | 267.70 |
| cdp-http: navigation | pass | 61.95 |
| cdp-http: typing and auto-waited click | pass | 946.62 |
| cdp-http: iframe | pass | 124.61 |
| cdp-http: popup and tab lifecycle | pass | 173.62 |
| cdp-http: wheel and locator scroll | pass | 86.84 |
| cdp-http: JavaScript, storage and screenshot | pass | 55.11 |
| cdp-http: network interception | pass | 29.54 |
| cdp-http: existing host tab | pass | 1478.51 |
| cdp-http: isolated context | pass | 170.77 |
| cdp: disconnect preserves host and existing tab state | pass | 68.38 |
| cdp-ws: navigation | pass | 49.52 |
| cdp-ws: typing and auto-waited click | pass | 908.32 |
| cdp-ws: iframe | pass | 109.11 |
| cdp-ws: popup and tab lifecycle | pass | 142.26 |
| cdp-ws: wheel and locator scroll | pass | 90.13 |
| cdp-ws: JavaScript, storage and screenshot | pass | 70.06 |
| cdp-ws: network interception | pass | 16.05 |
| cdp: 25 attach/disconnect cycles without accumulating tabs | pass | 782.57 |
| playwright-ws: navigation | pass | 40.10 |
| playwright-ws: typing and auto-waited click | pass | 907.10 |
| playwright-ws: iframe | pass | 66.43 |
| playwright-ws: popup and tab lifecycle | pass | 90.08 |
| playwright-ws: wheel and locator scroll | pass | 65.07 |
| playwright-ws: JavaScript, storage and screenshot | pass | 36.85 |
| playwright-ws: network interception | pass | 11.36 |
| playwright: disconnect preserves server and reconnect works | pass | 67.02 |

These are functional-test elapsed times from one final run, including browser startup, fixture waiting, auto-waiting and I/O where applicable. They are not an OBS comparison or a representative throughput benchmark. Nested existing-tab checks include the durations of their child checks, so do not sum all rows.

## Memory and limitations

The cycle test saves five controller memory samples in `integration/results.json`, after optional explicit garbage collection. This is a short lifecycle smoke test, not a memory leak proof; it does not measure all Chromium process memory or replace long-running native capture profiling. No new memory-safety or audio-gap conclusion is claimed.

Firefox/WebKit dispatch and authentication-header forwarding are unit tested only; live Firefox/WebKit servers and HTTPS/WSS authentication are unverified here. Remote endpoint reachability depends on host networking. CDP has lower fidelity than the Playwright server protocol; full upstream Playwright conformance is not claimed. Playwright server/client major and minor versions must match.

This API is for ordinary browser automation. External pages are not native CEF capture sources and cannot be recorded or streamed by another SuiteCut engine. The separately launched browser must expose a reachable automation endpoint; an arbitrary existing browser without one cannot be attached.

## Reproduction and retained evidence

Run `pnpm test:browser:connect`; output defaults to `.suitecut/external-browser`, or set `SUITECUT_CONNECT_DIRECTORY`. The collector uses fresh profiles, local fixture servers and owned browser processes with cleanup. The first successful 26-check run is retained under `first-integration/`; the final 27-check run adds the real connection timeout test and explicit keyboard input.

Saved evidence includes exact source and built-SDK snapshots, package/lock/config files, version outputs, final and first-run JSON results, three screenshots per run, Chromium stderr and test Chrome profiles. Previously sealed benchmark payloads were verified unchanged: 8243 files across 14 checksum manifests. A new checksum manifest seals this report and its evidence; a separately verified archive is saved under `/workspace/shared/`.
