# SuiteCut: full pinned Chromium browser-suite audit

Completed. Prepared October 12, 2026 (Asia/Kolkata).

Runtime commit: `1f9d8c2d65f472f6db2abd7c09b12a7531908521`. Playwright 1.62.1; CEF 152.0.6 / Chromium 152.0.7977.83; stock headless-shell Chromium 151.0.7922.34.

Collected **4,945 cases in 284 files** from the Chromium library and page projects, including Inspector/codegen. This is the browser compatibility suite discussed in this chat, not every test of Playwright’s entire repository: Firefox/WebKit, Android, Electron, component, test-runner, installation and language-SDK suites are outside this run.

The 285 official library/page spec files match the pinned source archive byte-for-byte. One pre-existing local diagnostic spec is excluded. No upstream test body was changed. The fixture supplies the CEF engine browser, explicitly rejects unsupported independent launch/connect operations, and reports CEF as a full runtime rather than Playwright’s headless-shell product.

## Execution counts

| Backend | Reported cases | Passed execution | Failed execution | Timed out | Skipped |
| --- | ---: | ---: | ---: | ---: | ---: |
| native | 4,945 | 4,412 | 411 | 26 | 96 |
| stock | 4,945 | 4,843 | 14 | 1 | 87 |

Actual execution statuses and expected outcomes differ: an upstream test marked as expected to fail can fail without representing a newly discovered regression; an unexpected pass is not a missing feature. Skips without an explicit skip/fixme annotation are retained separately because serial-group or setup failures can prevent their bodies from running.

## Failure dispositions

| Disposition | Cases |
| --- | ---: |
| Expected outcome | 4,412 |
| Blocked: independent launch/connect fixture | 343 |
| Upstream annotation skip | 89 |
| SuiteCut-path mismatch; stock passed | 62 |
| Skipped without skip annotation / possible serial or setup block | 7 |
| Expected upstream failure | 6 |
| Full-Chrome vs headless-shell unauthorized-navigation expectation | 5 |
| Both backends failed; shared cause not established | 5 |
| Different bootstrap-context semantics | 4 |
| Documented native resource limit | 4 |
| Default graphics path fails; explicit SwiftShader is a separate supported mode | 3 |
| Bootstrap/connection dispatcher-tree expectation | 2 |
| Blocked: proxy fixture identity assertion / worker watchdog | 1 |
| Configured autoplay policy differs from upstream default | 1 |
| Bootstrap object-count expectation / Playwright heap fixture | 1 |

The final inventory has **62 SuiteCut-path mismatches with passing stock controls**, after separating 343 direct/indirect blocked launch/connect cases, bootstrap-object assumptions, configured policies and resource bounds. These are 62 failing test cases, not 62 independent missing features. See [suitecut-mismatches.csv](suitecut-mismatches.csv) for that filtered list. Ten connect/header/URL tests catch the fixture’s rejection and then wait for a request that the fixture never sends; a proxy test hides the same denied launch behind a failing finally-close. They are blocked fixtures, not established network/header defects. The 100 MiB evaluate case exceeds the 16 MiB reply policy even though its public error is a closed transport. Two dispatcher-tree assertions also count the extra bootstrap connection objects.

A stock pass establishes a difference in the SuiteCut/CEF path on this host; it does not by itself identify the faulty layer. These browser builds differ by one Chromium major version. Both-backend failure is not proof that SuiteCut is correct or that the cause is shared. Raw failure messages and annotations are retained for both.

## Missing behavior by feature

| Feature | SuiteCut-path mismatch, stock passed | Other unexpected failures |
| --- | ---: | ---: |
| Popup creation, first-script behavior and events | 18 | 0 |
| Frames and cross-origin targets | 14 | 2 |
| Requests, routing, HTTP, WebSocket and HAR | 8 | 2 |
| Navigation, page lifecycle and crash handling | 6 | 1 |
| Other APIs / internal fixtures | 6 | 34 |
| Playwright video, screencast and tracing workflows | 2 | 11 |
| Downloads and file input | 2 | 9 |
| Permission and geolocation overrides | 2 | 0 |
| Inspector and codegen / internal product fixtures | 2 | 0 |
| JavaScript evaluation, handles, bindings and console | 1 | 1 |
| Clock and scheduling | 1 | 0 |
| Launch, persistent profiles, remote connections and extensions | 0 | 258 |
| Browser/context lifecycle and bootstrap semantics | 0 | 6 |
| Cookies and authentication state | 0 | 2 |
| Proxy, HTTP authentication and client certificates | 0 | 35 |
| Emulation, browser policies and graphics capabilities | 0 | 4 |
| Worker automation and worker network events | 0 | 0 |
| Input, focus, actionability and hit testing | 0 | 0 |
| Screenshots and pixel comparisons | 0 | 4 |
| Accessibility and ARIA snapshots | 0 | 0 |

## Case-by-case missing or blocked tests

Every unexpected failing/timed-out case appears below. Full multiline errors, controls, expected statuses and annotations are in [missing-cases.csv](missing-cases.csv); [cases.csv](cases.csv) contains every collected case, and [skipped-not-run.csv](skipped-not-run.csv) lists incomplete execution. [files.csv](files.csv) provides per-file totals.

### library/beforeunload.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [20: should close browser with beforeunload page](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/beforeunload.spec.ts#L20) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/browser-server.spec.ts (2 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [28: should start and stop pipe server](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browser-server.spec.ts#L28) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [66: should start ws server with host/port and produce well-formed endpoint](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browser-server.spec.ts#L66) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/browser.spec.ts (3 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [20: should return browserType](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browser.spec.ts#L20) | Blocked: proxy fixture identity assertion / worker watchdog | passed | Error: worker process exited unexpectedly (code=null, signal=SIGKILL) |
| [24: should create new page @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browser.spec.ts#L24) | Different bootstrap-context semantics | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: 1 Received: 2 |
| [54: should dispatch page.on(close) upon browser.close and reject evaluate](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browser.spec.ts#L54) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/browsercontext-add-cookies.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [248: should isolate cookies between launches](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-add-cookies.spec.ts#L248) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/browsercontext-base-url.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [37: should construct a new URL when a baseURL in browserType.launchPersistentContext is passed to page.goto](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-base-url.spec.ts#L37) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/browsercontext-basic.spec.ts (3 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [23: should create new context @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-basic.spec.ts#L23) | Different bootstrap-context semantics | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: 0 Received: 1 |
| [36: should be able to click across browser contexts](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-basic.spec.ts#L36) | Different bootstrap-context semantics | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: 0 Received: 1 |
| [118: should isolate localStorage and cookies @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-basic.spec.ts#L118) | Different bootstrap-context semantics | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: 0 Received: 1 |

### library/browsercontext-cookies-third-party.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [560: should(not) block third party cookies - persistent context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-cookies-third-party.spec.ts#L560) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/browsercontext-credentials.spec.ts (5 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [26: should fail without credentials](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-credentials.spec.ts#L26) | Full-Chrome vs headless-shell unauthorized-navigation expectation | passed | Error: expect(received).toContain(expected) // indexOf Matcher error: received value must not be null nor undefined Received has value: undefined |
| [37: should work with setHTTPCredentials](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-credentials.spec.ts#L37) | Full-Chrome vs headless-shell unauthorized-navigation expectation | passed | Error: expect(received).toContain(expected) // indexOf Matcher error: received value must not be null nor undefined Received has value: undefined |
| [111: should fail with correct credentials and mismatching scheme](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-credentials.spec.ts#L111) | Full-Chrome vs headless-shell unauthorized-navigation expectation | passed | Error: expect(received).toContain(expected) // indexOf Matcher error: received value must not be null nor undefined Received has value: undefined |
| [125: should fail with correct credentials and mismatching hostname](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-credentials.spec.ts#L125) | Full-Chrome vs headless-shell unauthorized-navigation expectation | passed | Error: expect(received).toContain(expected) // indexOf Matcher error: received value must not be null nor undefined Received has value: undefined |
| [141: should fail with correct credentials and mismatching port](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-credentials.spec.ts#L141) | Full-Chrome vs headless-shell unauthorized-navigation expectation | passed | Error: expect(received).toContain(expected) // indexOf Matcher error: received value must not be null nor undefined Received has value: undefined |

### library/browsercontext-events.spec.ts (5 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [55: console event should work in popup 2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-events.spec.ts#L55) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. Error: browserContext.waitForEvent: Test timeout of 30000ms exceeded. |
| [72: console event should work in immediately closed popup](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-events.spec.ts#L72) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. Error: browserContext.waitForEvent: Test timeout of 30000ms exceeded. |
| [103: dialog event should work in popup](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-events.spec.ts#L103) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. Error: page.waitForEvent: Test timeout of 30000ms exceeded. =========================== logs =========================== waiting for event "popup" ============================================================ Error: page.evaluate: Test timeout of 30000ms exceeded. |
| [120: dialog event should work in popup 2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-events.spec.ts#L120) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. Error: browserContext.waitForEvent: Test timeout of 30000ms exceeded. |
| [135: dialog event should work in immediately closed popup](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-events.spec.ts#L135) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. Error: page.waitForEvent: Test timeout of 30000ms exceeded. =========================== logs =========================== waiting for event "popup" ============================================================ Error: page.evaluate: Test timeout of 30000ms exceeded. |

### library/browsercontext-fetch.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [1335: should work with connectOverCDP](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-fetch.spec.ts#L1335) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/browsercontext-network-event.spec.ts (2 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [21: BrowserContext.Events.Request](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-network-event.spec.ts#L21) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toEqual(expected) // deep equality - Expected - 2 + Received + 0 Array [ "http://localhost:8991/empty.html", - "http://localhost:8991/one-style.html", - "http://localhost:8991/one-style.css", ] |
| [40: BrowserContext.Events.Response](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-network-event.spec.ts#L40) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toEqual(expected) // deep equality - Expected - 2 + Received + 0 Array [ "http://localhost:8995/empty.html", - "http://localhost:8995/one-style.html", - "http://localhost:8995/one-style.css", ] |

### library/browsercontext-page-event.spec.ts (3 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [129: should have an opener](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-page-event.spec.ts#L129) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. |
| [160: should work with Shift-clicking](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-page-event.spec.ts#L160) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. |
| [173: should work with Ctrl-clicking](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-page-event.spec.ts#L173) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. |

### library/browsercontext-proxy.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [25: should work when passing the proxy only on the context level](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-proxy.spec.ts#L25) | Blocked: independent launch/connect fixture | passed | TypeError: Cannot read properties of undefined (reading 'close') |

### library/browsercontext-reuse.spec.ts (22 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [97: should re-add binding after reset](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L97) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [117: should reset serviceworker](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L117) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [155: should reset serviceworker that hangs in importScripts](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L155) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [200: should not cache resources](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L200) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [255: should ignore binding from beforeunload](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L255) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [275: should reset mouse position](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L275) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [309: should reset tracing](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L309) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [326: should work with clock emulation](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L326) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [339: should continue issuing events after closing the reused page](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L339) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [361: should work with routeWebSocket](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L361) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [412: should update viewport and media](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L412) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [97: should re-add binding after reset](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L97) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [117: should reset serviceworker](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L117) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [155: should reset serviceworker that hangs in importScripts](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L155) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [200: should not cache resources](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L200) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [255: should ignore binding from beforeunload](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L255) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [275: should reset mouse position](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L275) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [309: should reset tracing](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L309) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [326: should work with clock emulation](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L326) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [339: should continue issuing events after closing the reused page](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L339) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [361: should work with routeWebSocket](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L361) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [412: should update viewport and media](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-reuse.spec.ts#L412) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/browsercontext-route.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [419: should fulfill popup main request using alias](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-route.spec.ts#L419) | SuiteCut-path mismatch; stock passed | passed | Error: expect(locator).toHaveText(expected) failed Locator: locator('body') Expected: "hello" Received: "" Timeout: 10000ms Call log: - Expect "toHaveText" with timeout 10000ms - waiting for locator('body') 23 × locator resolved to <body></body> - unexpected value "" |

### library/browsertype-connect.spec.ts (107 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [1155: should refuse connecting when versions do not match](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L1155) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Playwright version mismatch" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [1165: should timeout after redirect when connecting over http](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L1165) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Timeout 2000ms exceeded." Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [80: should connect over wss](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L80) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [117: should print HTTP error](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L117) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "401" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [123: should print ws error](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L123) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "<ws connecting> ws://does-not-exist.problem-domain:10987/" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [131: should print custom ws close error](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L131) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "browserType.connect: Oh my!" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [141: should be able to reconnect to a browser](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L141) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [161: should not crash on malformed json frame](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L161) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [177: should be able to visit ipv6](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L177) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [189: should ignore page.pause when headed](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L189) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [204: should be able to visit ipv6 through localhost](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L204) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [216: should be able to connect two browsers at the same time](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L216) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [238: should timeout in socket while connecting](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L238) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "browserType.connect: Timeout 1000ms exceeded" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [245: should timeout in connect while connecting](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L245) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "browserType.connect: Timeout 100ms exceeded" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [252: should send extra headers with connect request](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L252) | Blocked: independent launch/connect fixture | passed | Test timeout of 90000ms exceeded. |
| [267: should send extra headers with connect request in object form](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L267) | Blocked: independent launch/connect fixture | passed | Test timeout of 90000ms exceeded. |
| [282: should send default User-Agent and X-Playwright-Browser headers with connect request](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L282) | Blocked: independent launch/connect fixture | passed | Test timeout of 90000ms exceeded. |
| [299: should support slowmo option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L299) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [309: disconnected event should be emitted when browser is closed or server is closed](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L309) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [338: disconnected event should have browser as argument](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L338) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [348: should handle exceptions during connect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L348) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Dummy" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [355: should set the browser connected state](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L355) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [363: should throw when used after isConnected returns false](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L363) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [376: should throw when calling waitForNavigation after disconnect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L376) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [389: should reject navigation when browser closes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L389) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [401: should reject waitForSelector when browser closes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L401) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [416: should emit close events on pages and contexts](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L416) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [430: should terminate network waiters](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L430) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [446: should reject waitForEvent before browser.close finishes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L446) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [457: should reject waitForEvent before browser.onDisconnect fires](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L457) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [469: should respect selectors](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L469) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [514: should not throw on close after disconnect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L514) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [525: should saveAs videos from remote browser](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L525) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [545: should save videos to artifactsDir](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L545) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [566: should name video file after page guid when connecting to remote browser with artifactsDir](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L566) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [583: should be able to connect 20 times to a single server without warnings](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L583) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [601: should save download](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L601) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [625: should error when saving download after deletion](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L625) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [647: should properly disconnect when connection closes from the client side](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L647) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [668: should be able to connect when the wsEndpoint is passed as an option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L668) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [680: should save har](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L680) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [701: should filter launch options](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L701) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [714: should record trace with sources](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L714) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [738: should fulfill with global fetch result](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L738) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [754: should upload large file](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L754) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [807: should upload a folder](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L807) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [845: setInputFiles should preserve lastModified timestamp](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L845) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [865: should connect over http](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L865) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [80: should connect over wss](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L80) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [117: should print HTTP error](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L117) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "401" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [123: should print ws error](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L123) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "<ws connecting> ws://does-not-exist.problem-domain:10987/" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [131: should print custom ws close error](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L131) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "browserType.connect: Oh my!" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [141: should be able to reconnect to a browser](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L141) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [161: should not crash on malformed json frame](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L161) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [177: should be able to visit ipv6](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L177) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [189: should ignore page.pause when headed](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L189) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [204: should be able to visit ipv6 through localhost](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L204) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [216: should be able to connect two browsers at the same time](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L216) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [238: should timeout in socket while connecting](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L238) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "browserType.connect: Timeout 1000ms exceeded" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [245: should timeout in connect while connecting](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L245) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "browserType.connect: Timeout 100ms exceeded" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [252: should send extra headers with connect request](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L252) | Blocked: independent launch/connect fixture | passed | Test timeout of 90000ms exceeded. |
| [267: should send extra headers with connect request in object form](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L267) | Blocked: independent launch/connect fixture | passed | Test timeout of 90000ms exceeded. |
| [282: should send default User-Agent and X-Playwright-Browser headers with connect request](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L282) | Blocked: independent launch/connect fixture | passed | Test timeout of 90000ms exceeded. |
| [299: should support slowmo option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L299) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [309: disconnected event should be emitted when browser is closed or server is closed](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L309) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [338: disconnected event should have browser as argument](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L338) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [348: should handle exceptions during connect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L348) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Dummy" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [355: should set the browser connected state](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L355) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [363: should throw when used after isConnected returns false](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L363) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [376: should throw when calling waitForNavigation after disconnect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L376) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [389: should reject navigation when browser closes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L389) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [401: should reject waitForSelector when browser closes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L401) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [416: should emit close events on pages and contexts](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L416) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [430: should terminate network waiters](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L430) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [446: should reject waitForEvent before browser.close finishes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L446) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [457: should reject waitForEvent before browser.onDisconnect fires](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L457) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [469: should respect selectors](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L469) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [514: should not throw on close after disconnect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L514) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [525: should saveAs videos from remote browser](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L525) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [545: should save videos to artifactsDir](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L545) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [566: should name video file after page guid when connecting to remote browser with artifactsDir](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L566) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [583: should be able to connect 20 times to a single server without warnings](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L583) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [601: should save download](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L601) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [625: should error when saving download after deletion](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L625) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [647: should properly disconnect when connection closes from the client side](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L647) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [668: should be able to connect when the wsEndpoint is passed as an option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L668) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [680: should save har](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L680) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [701: should filter launch options](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L701) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [714: should record trace with sources](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L714) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [738: should fulfill with global fetch result](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L738) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [754: should upload large file](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L754) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [807: should upload a folder](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L807) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [845: setInputFiles should preserve lastModified timestamp](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L845) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [865: should connect over http](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L865) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [877: should forward non-forwarded requests](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L877) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [891: should proxy localhost requests @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L891) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [922: should proxy ipv6 localhost requests @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L922) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [953: should proxy requests from fetch api](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L953) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [984: should proxy requests from fetch api over ipv6](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L984) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [1015: should proxy local.playwright requests](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L1015) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [1030: should lead to the error page for forwarded requests when the connection is refused](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L1030) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [1044: should proxy based on the pattern](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L1044) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [1075: should check proxy pattern on the client](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L1075) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [1103: should work with cluster](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L1103) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [1110: should properly disconnect when connection closes from the server side](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L1110) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [1132: should be able to reconnect to a browser 12 times without warnings](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L1132) | Both backends failed; shared cause not established | timedOut | Test timeout of 90000ms exceeded. |
| [1148: cannot launch another browser](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-connect.spec.ts#L1148) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/browsertype-launch-server.spec.ts (12 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [23: should work](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L23) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [29: should validate options](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L29) | Blocked: independent launch/connect fixture | passed | Error: expect(received).rejects.toThrow(expected) Expected pattern: /channel: expected string, got object/ Received message: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" 221 \| if(process.env.COMPAT_BACKEND==='chromium') return use(original); 222 \| await use(new Proxy |
| [33: should work with host](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L33) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [40: should work with port](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L40) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [47: should work with wsPath](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L47) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [54: should work when wsPath is missing leading slash](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L54) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [61: should default to random wsPath](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L61) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [67: should provide an error when ws endpoint is incorrect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L67) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [74: should fire "close" event during kill](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L74) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [88: should return child_process instance](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L88) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [94: should fire close event](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L94) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [105: should log protocol](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch-server.spec.ts#L105) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/browsertype-launch.spec.ts (14 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [22: should reject all promises when browser is closed](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L22) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [34: should throw if userDataDir option is passed](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L34) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "userDataDir option is not supported in 'browserType.launch'. Use 'browserType.launchPersistentContext' instead" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [40: should throw if userDataDir is passed as an argument](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L40) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Pass userDataDir parameter to 'browserType.launchPersistentContext" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [46: should throw if port option is passed](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L46) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Cannot specify a port without launching as a server." Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [51: should throw if port option is passed for persistent context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L51) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Cannot specify a port without launching as a server." Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [56: should throw if page argument is passed](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L56) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "can not specify page" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [64: should reject if launched browser fails immediately](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L64) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toMatch(expected) Expected pattern: /browserType\.launch(.\|\n)*(spawn UNKNOWN\|spawn EFTYPE\|Browser logs:)/gim Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [72: should reject if executable path is invalid](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L72) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Failed to launch" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [78: should handle timeout](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L78) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "browserType.launch: Timeout 5000ms exceeded." Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [88: should handle exception and report launch log](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L88) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Dummy" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [98: should accept objects as options](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L98) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [104: should fire close event for all contexts](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L104) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [113: should be callable twice](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L113) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [122: should allow await using](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsertype-launch.spec.ts#L122) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/capabilities.spec.ts (3 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [105: should support webgl @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/capabilities.spec.ts#L105) | Default graphics path fails; explicit SwiftShader is a separate supported mode | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: true Received: false |
| [113: should support webgl 2 @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/capabilities.spec.ts#L113) | Default graphics path fails; explicit SwiftShader is a separate supported mode | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: true Received: false |
| [441: should not auto play audio](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/capabilities.spec.ts#L441) | Configured autoplay policy differs from upstream default | passed | Error: expect(locator).toHaveText(expected) failed Locator: locator('#log') Expected: "State: suspended" Received: "State: running" Timeout: 10000ms Call log: - Expect "toHaveText" with timeout 10000ms - waiting for locator('#log') 6 × locator resolved to <div id="log"></div> - unexpected value "" 17 × locator resolved |

### library/channels.spec.ts (6 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [40: should scope context handles](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/channels.spec.ts#L40) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [95: should scope CDPSession handles](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/channels.spec.ts#L95) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [139: should scope browser handles](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/channels.spec.ts#L139) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [184: should not generate dispatchers for subresources w/o listeners](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/channels.spec.ts#L184) | Bootstrap/connection dispatcher-tree expectation | passed | Error: expect(received).toEqual(expected) // deep equality - Expected - 0 + Received + 26 @@ -31,10 +31,36 @@ "objects": Array [ Object { "_guid": "frame", "objects": Array [], }, + ], + }, + Object { + "_guid": "request-context", + "objects": Array [], + }, + Object { + "_guid": "tracing", + "objects": Array [], + },  |
| [224: should work with the domain module](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/channels.spec.ts#L224) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [252: exposeFunction should not leak](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/channels.spec.ts#L252) | Bootstrap/connection dispatcher-tree expectation | passed | Error: expect(received).toEqual(expected) // deep equality - Expected - 0 + Received + 26 @@ -28,10 +28,36 @@ }, Object { "_guid": "page", "objects": Array [ Object { + "_guid": "frame", + "objects": Array [], + }, + ], + }, + Object { + "_guid": "request-context", + "objects": Array [], + }, + Object { + "_guid": "tra |

### library/chromium/chromium.spec.ts (4 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [151: should close service worker together with the context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/chromium.spec.ts#L151) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [167: should pass args with spaces](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/chromium.spec.ts#L167) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [628: should throw when connecting twice to an already running persistent context (--remote-debugging-port)](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/chromium.spec.ts#L628) | Blocked: independent launch/connect fixture | skipped | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [646: should throw when connecting twice to an already running persistent context (--remote-debugging-pipe)](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/chromium.spec.ts#L646) | Blocked: independent launch/connect fixture | skipped | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/chromium/connect-over-cdp.spec.ts (32 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [28: should connect to an existing cdp session](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L28) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [45: should cleanup artifacts dir after connectOverCDP disconnects due to ws close](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L45) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [64: should write traces to provided artifactsDir on connectOverCDP](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L64) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [101: should connectOverCDP and manage downloads in default context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L101) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [137: should connect to an existing cdp session twice](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L137) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [171: should connect to existing page with iframe and navigate](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L171) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [192: should connect to existing service workers](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L192) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [221: should connect over a ws endpoint](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L221) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [253: should send extra headers with connect request](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L253) | Blocked: independent launch/connect fixture | passed | Test timeout of 30000ms exceeded. |
| [286: should keep URL parameters when adding json/version](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L286) | Blocked: independent launch/connect fixture | passed | Test timeout of 30000ms exceeded. Error: Request /browser/json/version/?foo=bar was not received before the test finished. |
| [298: should append /json/version with a slash if there isnt one](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L298) | Blocked: independent launch/connect fixture | passed | Test timeout of 30000ms exceeded. Error: Request /browser/json/version/?foo=bar was not received before the test finished. |
| [310: should send default User-Agent header with connect request](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L310) | Blocked: independent launch/connect fixture | passed | Test timeout of 30000ms exceeded. |
| [327: should report all pages in an existing browser](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L327) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [353: should connect via https](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L353) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [388: should return valid browser from context.browser()](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L388) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [410: should report an expected error when the endpointURL returns a non-expected status code](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L410) | Blocked: independent launch/connect fixture | passed | Error: expect(received).rejects.toThrowError(expected) Expected substring: "browserType.connectOverCDP: Unexpected status 404 when connecting to http://localhost:9367/json/version/" Received message: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" 221 \| if(process.env.C |
| [422: should report an expected error when the endpoint URL JSON webSocketDebuggerUrl is undefined](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L422) | Blocked: independent launch/connect fixture | passed | Error: expect(received).rejects.toThrowError(expected) Expected substring: "browserType.connectOverCDP: Invalid URL" Received message: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" 221 \| if(process.env.COMPAT_BACKEND==='chromium') return use(original); 222 \| await use |
| [433: should connect to an existing cdp session when passed as a first argument](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L433) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [448: should use proxy with connectOverCDP](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L448) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [470: should use env proxy with connectOverCDP discovery request](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L470) | Blocked: independent launch/connect fixture | failed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Unexpected status 404 when connecting to http://localhost:9383/json/version/" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [489: should send target Host header when using env HTTP proxy with connectOverCDP](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L489) | Blocked: independent launch/connect fixture | failed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Unexpected status 404 when connecting to http://localhost:9387/json/version/" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [508: should be able to connect via localhost](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L508) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [523: emulate media should not be affected by second connectOverCDP with noDefaults](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L523) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [550: should allow tracing over cdp session](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L550) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [574: setInputFiles should preserve lastModified timestamp](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L574) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [604: setInputFiles should use local path when isLocal is set](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L604) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [620: should print custom ws close error](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L620) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Browser logs:· Oh my! " Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [630: should not reuse utility worlds between two clients](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L630) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [658: should get title and URL of existing page](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L658) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [695: should skip default overrides with noDefaults](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L695) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [728: noDefaults should not affect new contexts](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L728) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [753: should connect over CDP using a ConnectionTransport](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/connect-over-cdp.spec.ts#L753) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/chromium/extensions.spec.ts (6 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [45: should support service worker stop and restart lifecycle](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/extensions.spec.ts#L45) | Blocked: independent launch/connect fixture | skipped | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [93: should give access to the service worker](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/extensions.spec.ts#L93) | Blocked: independent launch/connect fixture | skipped | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [105: should give access to the service worker when recording video](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/extensions.spec.ts#L105) | Blocked: independent launch/connect fixture | skipped | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [120: should support request/response events in the service worker](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/extensions.spec.ts#L120) | Blocked: independent launch/connect fixture | skipped | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [155: should report console messages from content script](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/extensions.spec.ts#L155) | Blocked: independent launch/connect fixture | skipped | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [168: should use custom userAgent in service worker fetch requests](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/extensions.spec.ts#L168) | Blocked: independent launch/connect fixture | skipped | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/chromium/launcher.spec.ts (3 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [19: should throw with remote-debugging-pipe argument](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/launcher.spec.ts#L19) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Playwright manages remote debugging connection itself" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [28: should not throw with remote-debugging-port argument](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/launcher.spec.ts#L28) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [37: should not create pages automatically](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/launcher.spec.ts#L37) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/chromium/oopif.spec.ts (3 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [233: should report google.com frame with headed](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/oopif.spec.ts#L233) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [353: should not throw on exposeFunction when oopif detaches](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/oopif.spec.ts#L353) | SuiteCut-path mismatch; stock passed | passed | Error: page.exposeFunction: Protocol error (Page.addScriptToEvaluateOnNewDocument): Session with given id not found. |
| [372: should allow to re-connect to OOPIFs with CDP when iframes were there already](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/oopif.spec.ts#L372) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/client-certificates.spec.ts (5 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [450: should pass with matching certificates and when a http proxy is used from env](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/client-certificates.spec.ts#L450) | Both backends failed; shared cause not established | failed | Error: expect(received).toEqual(expected) // deep equality - Expected - 3 + Received + 1 - Array [ - "127.0.0.1:41529", - ] + Array [] |
| [802: support http2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/client-certificates.spec.ts#L802) | Both backends failed; shared cause not established | failed | Error: page.goto: net::ERR_CONNECTION_CLOSED at https://local.playwright:46605/ Call log: - navigating to "https://local.playwright:46605/", waiting until "load" |
| [827: support http2 if the browser only supports http1.1](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/client-certificates.spec.ts#L827) | Blocked: independent launch/connect fixture | failed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [903: validate input](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/client-certificates.spec.ts#L903) | Blocked: independent launch/connect fixture | passed | Error: expect(received).rejects.toThrow(expected) Expected substring: "None of cert, key, passphrase or pfx is specified" Received message: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" 221 \| if(process.env.COMPAT_BACKEND==='chromium') return use(original); 222 \| awai |
| [909: should pass with matching certificates](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/client-certificates.spec.ts#L909) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/debug-controller.spec.ts (10 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [85: should pick element](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/debug-controller.spec.ts#L85) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [116: should report pages](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/debug-controller.spec.ts#L116) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [144: should reset for reuse](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/debug-controller.spec.ts#L144) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [162: should highlight all](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/debug-controller.spec.ts#L162) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [176: should record](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/debug-controller.spec.ts#L176) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [211: should record expect signal](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/debug-controller.spec.ts#L211) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [240: should record custom data-testid](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/debug-controller.spec.ts#L240) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [275: should reset routes before reuse](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/debug-controller.spec.ts#L275) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [299: should highlight inside iframe](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/debug-controller.spec.ts#L299) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [325: should highlight aria template](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/debug-controller.spec.ts#L325) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/defaultbrowsercontext-1.spec.ts (11 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [28: context.cookies() should work @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-1.spec.ts#L28) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [48: context.addCookies() should work](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-1.spec.ts#L48) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [70: context.clearCookies() should work](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-1.spec.ts#L70) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [89: should support viewport option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-1.spec.ts#L89) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [96: should support deviceScaleFactor option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-1.spec.ts#L96) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [101: should support userAgent option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-1.spec.ts#L101) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [111: should support bypassCSP option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-1.spec.ts#L111) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [119: should support javascriptEnabled option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-1.spec.ts#L119) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [130: should support httpCredentials option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-1.spec.ts#L130) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [137: should support offline option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-1.spec.ts#L137) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [143: should support acceptDownloads option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-1.spec.ts#L143) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/defaultbrowsercontext-2.spec.ts (30 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [24: should support hasTouch option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L24) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [30: should work in persistent context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L30) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [36: should support colorScheme option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L36) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [42: should support reducedMotion option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L42) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [48: should support forcedColors option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L48) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [54: should support contrast option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L54) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [60: should support timezoneId option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L60) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [65: should support locale option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L65) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [70: should support geolocation and permissions options](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L70) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [79: should support ignoreHTTPSErrors option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L79) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [87: should support extraHTTPHeaders option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L87) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [96: should accept userDataDir](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L96) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [104: should accept relative userDataDir](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L104) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [111: should restore state from userDataDir](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L111) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [139: should create userDataDir if it does not exist](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L139) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [146: should goto about:blank on relaunched persistent context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L146) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [165: should have default URL when launching browser](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L165) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [171: should throw if page argument is passed](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L171) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "can not specify page" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [179: should have passed URL when launching with ignoreDefaultArgs: true](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L179) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [195: should handle timeout](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L195) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "browserType.launchPersistentContext: Timeout 5000ms exceeded." Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [201: should handle exception](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L201) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "Dummy" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [208: should fire close event for a persistent context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L208) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [216: coverage should work](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L216) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [228: should respect selectors](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L228) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [245: should connect to a browser with the default page](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L245) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [252: should support har option](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L252) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [263: user agent is up to date](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L263) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [270: dialog.accept should work](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L270) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [285: CacheStorage entry should survive page.reload()](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L285) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [306: exposes browser](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/defaultbrowsercontext-2.spec.ts#L306) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/download.spec.ts (4 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [316: should report new window downloads](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/download.spec.ts#L316) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. Error: page.waitForEvent: Target page, context or browser has been closed =========================== logs =========================== waiting for event "download" ============================================================ |
| [377: should delete downloads on browser gone](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/download.spec.ts#L377) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [399: should save downloads to artifactsDir](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/download.spec.ts#L399) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [476: should throw if browser dies](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/download.spec.ts#L476) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/downloads-path.spec.ts (6 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [32: should keep downloadsPath folder](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/downloads-path.spec.ts#L32) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [48: should delete downloads when context closes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/downloads-path.spec.ts#L48) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [63: should report downloads in downloadsPath folder](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/downloads-path.spec.ts#L63) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [77: should report downloads in downloadsPath folder with a relative path](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/downloads-path.spec.ts#L77) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [91: should accept downloads in persistent context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/downloads-path.spec.ts#L91) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [105: should delete downloads when persistent context closes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/downloads-path.spec.ts#L105) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/emulation-focus.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [201: should trigger hover state concurrently](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/emulation-focus.spec.ts#L201) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/fetch-proxy.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [23: context request should pick up proxy credentials](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/fetch-proxy.spec.ts#L23) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/har.spec.ts (2 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [80: should have pages in persistent context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/har.spec.ts#L80) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [588: should have popup requests](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/har.spec.ts#L588) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: 2 Received: 0 |

### library/heap.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [172: should not leak fixtures w/ page](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/heap.spec.ts#L172) | Bootstrap object-count expectation / Playwright heap fixture | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: 1 Received: 2 |

### library/hit-target.spec.ts (3 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [366: should detect overlaid element in a transformed iframe](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/hit-target.spec.ts#L366) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [398: should click in iframe with padding](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/hit-target.spec.ts#L398) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [422: should click in iframe with padding 2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/hit-target.spec.ts#L422) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

### library/inspector/cli-codegen-2.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [339: should record open in a new tab with url](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/inspector/cli-codegen-2.spec.ts#L339) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "page1" Received string: "const { chromium } = require('playwright');· (async () => { const browser = await chromium.launch({ headless: false }); const context = await browser.newContext(); const page = await context.newPage(); await page.goto(' |

### library/inspector/pause.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [207: should highlight pointer, only in main frame](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/inspector/pause.spec.ts#L207) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

### library/launcher.spec.ts (2 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [30: should kill browser process on timeout after close](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/launcher.spec.ts#L30) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [45: should throw a friendly error if its headed and there is no xserver on linux running](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/launcher.spec.ts#L45) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toMatch(expected) Expected pattern: /Looks like you launched a headed browser without having a XServer running./ Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |

### library/logger.spec.ts (2 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [19: should log @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/logger.spec.ts#L19) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [34: should log context-level](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/logger.spec.ts#L34) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/multiclient.spec.ts (17 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [64: should connect two clients](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L64) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [97: should fire context event on remote newContext](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L97) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [110: should have separate default timeouts](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L110) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [123: should receive viewport size changes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L123) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [135: should not allow parallel js coverage and cleanup upon disconnect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L135) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [148: should not allow parallel css coverage](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L148) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [161: should unpause clock](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L161) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [170: last emulateMedia wins](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L170) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [180: should chain routes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L180) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [248: should remove exposed bindings upon disconnect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L248) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [276: should unroute websockets](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L276) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [297: should remove init scripts upon disconnect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L297) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [321: should remove locator handlers upon disconnect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L321) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [356: should launch persistent](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L356) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [364: should avoid side effects upon disconnect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L364) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [385: screencast should deliver cached last frame to a new client](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L385) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [409: should stop tracing upon disconnect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/multiclient.spec.ts#L409) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/page-clock.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [343: should not run time before popup on pause](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-clock.spec.ts#L343) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: 1000 Received: 1791745687689 |

### library/page-event-crash.spec.ts (6 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [42: should emit crash event when page crashes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L42) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. |
| [49: should throw on any action after page crashes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L49) | SuiteCut-path mismatch; stock passed | passed | Error: page.waitForEvent: Target page, context or browser has been closed =========================== logs =========================== waiting for event "crash" ============================================================ Error: browserContext.close: Target page, context or browser has been closed |
| [74: should cancel waitForEvent when page crashes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L74) | SuiteCut-path mismatch; stock passed | passed | Error: browser.newContext: Target page, context or browser has been closed |
| [82: should cancel navigation when page crashes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L82) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "page.goto: Page crashed" Received string: "page.goto: Target page, context or browser has been closed Call log: - navigating to \"http://localhost:10647/one-style.html\", waiting until \"load\" " |
| [92: should be able to close context when page crashes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L92) | SuiteCut-path mismatch; stock passed | passed | Error: page.waitForEvent: Target page, context or browser has been closed =========================== logs =========================== waiting for event "crash" ============================================================ Error: browserContext.close: Target page, context or browser has been closed |
| [101: should be able to close page after crash](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L101) | SuiteCut-path mismatch; stock passed | passed | Error: page.waitForEvent: Target page, context or browser has been closed =========================== logs =========================== waiting for event "crash" ============================================================ Error: browserContext.close: Target page, context or browser has been closed |

### library/permissions.spec.ts (2 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [222: storage access](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/permissions.spec.ts#L222) | SuiteCut-path mismatch; stock passed | passed | Error: browserContext.grantPermissions: Protocol error (Browser.grantPermissions): Unsupported native permission: storageAccess |
| [317: can request screen-wake-lock](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/permissions.spec.ts#L317) | SuiteCut-path mismatch; stock passed | passed | Error: browserContext.grantPermissions: Protocol error (Browser.grantPermissions): Unsupported native permission: wakeLockScreen |

### library/playwright-client.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [26: should connect from a page and drive the same browser](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/playwright-client.spec.ts#L26) | Both backends failed; shared cause not established | failed | Error: ENOENT: no such file or directory, open '/tmp/suitecut-playwright-upstream/playwright-1.62.1/packages/playwright-client/lib/index.mjs' Error: page.evaluate: TypeError: Failed to fetch dynamically imported module: http://localhost:10183/playwright-client.mjs |

### library/popup.spec.ts (9 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [20: should inherit user agent from browser context @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L20) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: "hey" Received: "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36" |
| [42: should respect routes from browser context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L42) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: true Received: false |
| [86: should inherit http credentials from browser context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L86) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: "Woof-Woof" Received: "" |
| [102: should inherit touch support from browser context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L102) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: true Received: false |
| [117: should inherit viewport size from browser context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L117) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toEqual(expected) // deep equality - Expected - 2 + Received + 2 Object { - "height": 500, - "width": 400, + "height": 0, + "width": 0, } |
| [160: should respect routes from browser context when using window.open](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L160) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: true Received: false |
| [177: BrowserContext.addInitScript should apply to an in-process popup](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L177) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: 123 Received: undefined |
| [205: should expose function from browser context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L205) | SuiteCut-path mismatch; stock passed | passed | Error: page.evaluate: TypeError: win.add is not a function at eval (eval at evaluate (:311:30), <anonymous>:3:22) at UtilityScript.evaluate (<anonymous>:313:16) at UtilityScript.<anonymous> (<anonymous>:1:44) |
| [224: should not dispatch binding on a closed page](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L224) | SuiteCut-path mismatch; stock passed | passed | Error: page.evaluate: TypeError: win.add is not a function at eval (eval at evaluate (:311:30), <anonymous>:3:15) at UtilityScript.evaluate (<anonymous>:313:16) at UtilityScript.<anonymous> (<anonymous>:1:44) |

### library/proxy.spec.ts (23 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [21: should throw for bad server value](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L21) | Blocked: independent launch/connect fixture | passed | Error: expect(received).toContain(expected) // indexOf Expected substring: "proxy.server: expected string, got number" Received string: "SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness" |
| [29: should use proxy @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L29) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [42: should use proxy for second page](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L42) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [61: should work with IP:PORT notion](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L61) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [125: should allow bypassing localhost requests](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L125) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [125: should allow bypassing 127.0.0.1 requests](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L125) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [144: should authenticate](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L144) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [165: should reconnect with credentials after CONNECT 407 closes the socket](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L165) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [203: should work with authenticate followed by redirect](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L203) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [235: should exclude patterns](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L235) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [288: should bypass proxy for localhost when localhost is in bypass list](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L288) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [288: should bypass proxy for 127.0.0.1 when 127.0.0.1 is in bypass list](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L288) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [306: should use socks proxy](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L306) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [316: should use socks proxy in second page](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L316) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [332: does launch without a port](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L332) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [369: should use SOCKS proxy for websocket requests](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L369) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [404: should use http proxy for websocket requests](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L404) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [91: localhost](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L91) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [91: loopback address](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L91) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [91: link-local](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L91) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [91: localhost](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L91) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [91: loopback address](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L91) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [91: link-local](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/proxy.spec.ts#L91) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/screencast.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [179: start/stop twice without path creates two files in artifactsDir](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/screencast.spec.ts#L179) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/screenshot.spec.ts (2 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [123: should throw if screenshot size is too large with device scale factor](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/screenshot.spec.ts#L123) | Documented native resource limit | passed | Error: page.screenshot: Protocol error (Page.captureScreenshot): CEF screenshot exceeds the 8,294,400-pixel or 16,384-axis limit Call log: - taking page screenshot - waiting for fonts to load... - fonts loaded |
| [144: should work with large size](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/screenshot.spec.ts#L144) | Documented native resource limit | passed | Error: page.screenshot: Protocol error (Page.captureScreenshot): CEF screenshot exceeds the 8,294,400-pixel or 16,384-axis limit Call log: - taking page screenshot - waiting for fonts to load... - fonts loaded |

### library/signals.spec.ts (9 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [25: should close the browser when the node process closes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/signals.spec.ts#L25) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [45: should remove temp dir on process.exit](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/signals.spec.ts#L45) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [62: should report browser close signal 2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/signals.spec.ts#L62) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [78: should close the browser on SIGINT](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/signals.spec.ts#L78) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [86: should close the browser on SIGTERM](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/signals.spec.ts#L86) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [94: should close the browser on SIGHUP](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/signals.spec.ts#L94) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [102: should kill the browser on double SIGINT and remove temp dir](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/signals.spec.ts#L102) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [117: should kill the browser on SIGINT + SIGTERM](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/signals.spec.ts#L117) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [127: should kill the browser on SIGTERM + SIGINT](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/signals.spec.ts#L127) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/trace-viewer.spec.ts (2 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [2435: should replace meta content attr that specifies charset](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/trace-viewer.spec.ts#L2435) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toBe(expected) // Object.is equality Expected: "<meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\">" Received: "<meta content=\"text/html; charset=utf-8\" http-equiv=\"content-type\">" Call Log: - Timeout 10000ms exceeded while waiting on the predicate |
| [2539: take trace paths via stdin](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/trace-viewer.spec.ts#L2539) | Both backends failed; shared cause not established | failed | Error: expect(locator).toContainText(expected) failed Locator: locator('.action-title') Timeout: 10000ms - Expected - 3 + Received + 1 - Array [ - /Create page/, - ] + Array [] Call log: - Expect "toContainText" with timeout 10000ms - waiting for locator('.action-title') 24 × locator resolved to 0 elements |

### library/tracing.spec.ts (5 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [209: should respect tracesDir and name](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/tracing.spec.ts#L209) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [358: should not crash when browser closes mid-trace](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/tracing.spec.ts#L358) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [367: should survive browser.close with auto-created traces dir](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/tracing.spec.ts#L367) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [716: should not flush console events](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/tracing.spec.ts#L716) | SuiteCut-path mismatch; stock passed | passed | Error: The "path" argument must be of type string. Received undefined Call Log: - Test timeout of 30000ms exceeded |
| [780: should not emit after w/o before](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/tracing.spec.ts#L780) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### library/video.spec.ts (5 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [495: should capture static page in persistent context @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/video.spec.ts#L495) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [543: should throw on browser close](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/video.spec.ts#L543) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [563: should throw if browser dies](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/video.spec.ts#L563) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [584: should wait for video to finish if page was closed](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/video.spec.ts#L584) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |
| [610: should close ffmpeg even if there were no frames](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/video.spec.ts#L610) | Blocked: independent launch/connect fixture | passed | Error: SuiteCut CEF upstream adapter: independent browser launching/connecting is outside this harness |

### page/expect-boolean.spec.ts (3 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [379: with frameLocator](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/expect-boolean.spec.ts#L379) | SuiteCut-path mismatch; stock passed | passed | Error: expect(locator).toBeVisible() failed Locator: locator('iframe').contentFrame().locator('input') Expected: visible Error: element(s) not found Call log: - Expect "toBeVisible" with timeout 10000ms - waiting for locator('iframe').contentFrame().locator('input') - Target page, context or browser has been closed Err |
| [391: with frameLocator 2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/expect-boolean.spec.ts#L391) | SuiteCut-path mismatch; stock passed | passed | Error: expect(locator).toBeVisible() failed Locator: locator('iframe').contentFrame().locator('input') Expected: visible Error: element(s) not found Call log: - Expect "toBeVisible" with timeout 10000ms - waiting for locator('iframe').contentFrame().locator('input') - Target page, context or browser has been closed Err |
| [680: with frameLocator](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/expect-boolean.spec.ts#L680) | SuiteCut-path mismatch; stock passed | passed | Error: expect(locator).toBeAttached() failed Locator: locator('iframe').contentFrame().locator('input') Expected: attached Error: element(s) not found Call log: - Expect "toBeAttached" with timeout 10000ms - waiting for locator('iframe').contentFrame().locator('input') - Target page, context or browser has been closed  |

### page/locator-misc-2.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [171: Locator.locator() and FrameLocator.locator() should accept locator](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/locator-misc-2.spec.ts#L171) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

### page/locator-query.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [243: should allow some, but not all nested frameLocators](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/locator-query.spec.ts#L243) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

### page/page-aria-snapshot-ai.spec.ts (3 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [70: should snapshot a locator inside an iframe](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-aria-snapshot-ai.spec.ts#L70) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [85: should limit depth across iframe boundary](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-aria-snapshot-ai.spec.ts#L85) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [175: should persist iframe references](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-aria-snapshot-ai.spec.ts#L175) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

### page/page-aria-snapshot.spec.ts (2 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [699: auto-waits the locator and does not include iframes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-aria-snapshot.spec.ts#L699) | SuiteCut-path mismatch; stock passed | passed | Error: locator.ariaSnapshot: Target page, context or browser has been closed Call log: - waiting for locator('#target') Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [737: should snapshot a locator inside an iframe](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-aria-snapshot.spec.ts#L737) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

### page/page-click.spec.ts (5 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [986: should click in an iframe with border](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-click.spec.ts#L986) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [1008: should click in an iframe with border 2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-click.spec.ts#L1008) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [1030: should click in a transformed iframe](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-click.spec.ts#L1030) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [1090: should click in a transformed iframe with force](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-click.spec.ts#L1090) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [1112: should click in a nested transformed iframe](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-click.spec.ts#L1112) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

### page/page-evaluate.spec.ts (2 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [582: should transfer 100Mb of data from page to node.js](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-evaluate.spec.ts#L582) | Documented native resource limit | passed | Error: page.evaluate: Target page, context or browser has been closed Browser logs: CEF transport closed |
| [894: promise collected](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-evaluate.spec.ts#L894) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. Error: expect(received).toContain(expected) // indexOf Expected substring: "Resulting promise was garbage collected" Received string: "page.evaluate: Test timeout of 30000ms exceeded." |

### page/page-event-popup.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [61: should be able to capture alert](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-event-popup.spec.ts#L61) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. Error: page.waitForEvent: Test timeout of 30000ms exceeded. =========================== logs =========================== waiting for event "popup" ============================================================ Error: page.evaluate: Test timeout of 30000ms exceeded. |

### page/page-event-request.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [198: should fire requestfailed when intercepting race](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-event-request.spec.ts#L198) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

### page/page-mouse.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [341: should track hover across iframe boundaries](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-mouse.spec.ts#L341) | SuiteCut-path mismatch; stock passed | passed | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

### page/page-network-idle.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [142: should wait for networkidle from the popup](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-network-idle.spec.ts#L142) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. Error: page.waitForLoadState: Test timeout of 30000ms exceeded. |

### page/page-network-request.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [478: should not allow to access frame on popup main request](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-network-request.spec.ts#L478) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. Error: browserContext.waitForEvent: Test timeout of 30000ms exceeded. |

### page/page-request-continue.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [590: continue should drop content-length on redirects](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-request-continue.spec.ts#L590) | SuiteCut-path mismatch; stock passed | passed | Error: expect(received).toBeUndefined() Received: "text/plain;charset=UTF-8" |

### page/page-screenshot.spec.ts (2 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [969: should throw if screenshot size is too large](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-screenshot.spec.ts#L969) | Documented native resource limit | passed | Error: page.screenshot: Protocol error (Page.captureScreenshot): CEF screenshot exceeds the 8,294,400-pixel or 16,384-axis limit Call log: - taking page screenshot - waiting for fonts to load... - fonts loaded |
| [394: should work for webgl](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-screenshot.spec.ts#L394) | Default graphics path fails; explicit SwiftShader is a separate supported mode | passed | Error: expect(Buffer).toMatchSnapshot(expected) failed 38160 pixels (ratio 0.13 of all image pixels) are different. Snapshot: screenshot-webgl.png |

### page/page-set-input-files.spec.ts (1 cases)

| Line / upstream case | Disposition | Stock status | Symptom |
| --- | --- | --- | --- |
| [38: should upload a folder](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-set-input-files.spec.ts#L38) | SuiteCut-path mismatch; stock passed | passed | Test timeout of 30000ms exceeded. Error: page.evaluate: Target page, context or browser has been closed |

## Seven serial-blocked heap cases: isolated follow-up

The main inventory skipped seven cases after the first serial heap assertion failed. They are attempted one at a time in fresh workers, without changing test bodies. These repeated cases are supplementary evidence, not seven additional unique tests.

| Line / case | Original status | Isolated status | Symptom |
| --- | --- | --- | --- |
| 178: should not leak fixtures w/o page | serial-blocked | failed | Error: expect(received).toBe(expected) // Object.is equality Expected: 0 Received: 1 |
| 184: should not leak server-side objects | serial-blocked | failed | Error: expect(received).toBe(expected) // Object.is equality Expected: 1 Received: 2 |
| 191: should not leak dispatchers after closing page | serial-blocked | failed | Error: expect(received).toBe(expected) // Object.is equality Expected: 5 Received: 6 |
| 223: should not leak workers | serial-blocked | passed |  |
| 304: cycle handles | serial-blocked | failed | Error: page.$$: Frame has been detached. |
| 244: should collect stale handles | serial-blocked | passed |  |
| 270: should collect frames | serial-blocked | passed |  |

The failed initial heap assertion counts live Playwright JavaScript objects, including SuiteCut’s intentional bootstrap page/context. Receiving two pages rather than the launched-browser expectation of one is not by itself evidence of an accumulating leak. These tests do not inspect CEF/FFmpeg native allocations or establish whole-runtime memory safety.

## Test environment and interpretation

Two workers per backend share CPU affinity 0–3 and a four-CPU cgroup quota, with a 16 GiB host memory cgroup. Native and stock inventories run concurrently; these are functional inventories, not performance comparisons or the earlier four-CPU/eight-GiB benchmark. Both use zero retries, no failure-count cutoff, no suite-wide deadline, 30-second ordinary test timeouts and unchanged upstream slow-test multipliers. Screenshot baseline updates are disabled.

A hung native worker spent over 500 seconds in a browser-type identity assertion. The fixture wraps BrowserType in a Proxy while browser.browserType() returns the underlying object; strict identity differs. GDB sampled V8 string equality while the assertion failed to service its timer or Inspector commands. The worker was terminated, then the suite continued. Later workers receive an external watchdog after 200 seconds of sustained CPU activity. These actions and stacks are recorded explicitly; they are not normal browser test timeouts or evidence of a CEF memory leak.

The native browser exposes an existing bootstrap context/page. Upstream tests assuming a freshly launched browser with zero contexts therefore have different counts. Unauthorized-navigation tests also branch on full Chrome versus headless-shell behavior: CEF returns a 401 response in cases expecting full Chrome to throw ERR_INVALID_AUTH_CREDENTIALS. Those five expectation differences do not demonstrate failure of valid HTTP credentials.

Launcher, server, Inspector/codegen, internal-debugger and API-request tests can execute Playwright or stock Chromium infrastructure outside CEF. Their passes must not be advertised as CEF rendering coverage. The harness currently blocks some direct launch/connect requests, so those failures do not establish that all corresponding SuiteCut external-browser APIs are unsupported.

The frozen helper and adapter hashes are recorded. A completed passing 600 MiB HAR fixture for each backend is retained gzip-compressed with its original SHA-256 to preserve temporary disk space. Raw test results and attachments remain separate from product code. No fixes or release publication are part of this inventory run.
