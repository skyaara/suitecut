# SuiteCut-path mismatches with passing Chromium controls

62 cases from the main 4,945-case run. These are test-level differences, with overlapping causes; they are not 62 independent missing features. Blocked launcher fixtures, configured policies, resource limits and bootstrap assumptions are excluded here. See [the full report](report.md) for scope and methodology.

The separate serial-case follow-up adds one failed bulk-handle case (`library/heap.spec.ts:304`, `page.$$` reports a detached frame); three worker/stale-handle/frame cleanup cases pass, and three live-object count assertions include the bootstrap page. See [isolated-serial-cases.csv](isolated-serial-cases.csv).

## library/browsercontext-events.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [55: library/browsercontext-events.spec.ts > console event should work in popup 2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-events.spec.ts#L55) | Test timeout of 30000ms exceeded. Error: browserContext.waitForEvent: Test timeout of 30000ms exceeded. |
| [72: library/browsercontext-events.spec.ts > console event should work in immediately closed popup](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-events.spec.ts#L72) | Test timeout of 30000ms exceeded. Error: browserContext.waitForEvent: Test timeout of 30000ms exceeded. |
| [103: library/browsercontext-events.spec.ts > dialog event should work in popup](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-events.spec.ts#L103) | Test timeout of 30000ms exceeded. Error: page.waitForEvent: Test timeout of 30000ms exceeded. =========================== logs =========================== waiting for event "popup" ============================================================ Error: page.evaluate: Test timeout of 30000ms exceeded. |
| [120: library/browsercontext-events.spec.ts > dialog event should work in popup 2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-events.spec.ts#L120) | Test timeout of 30000ms exceeded. Error: browserContext.waitForEvent: Test timeout of 30000ms exceeded. |
| [135: library/browsercontext-events.spec.ts > dialog event should work in immediately closed popup](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-events.spec.ts#L135) | Test timeout of 30000ms exceeded. Error: page.waitForEvent: Test timeout of 30000ms exceeded. =========================== logs =========================== waiting for event "popup" ============================================================ Error: page.evaluate: Test timeout of 30000ms exceeded. |

## library/browsercontext-network-event.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [21: library/browsercontext-network-event.spec.ts > BrowserContext.Events.Request](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-network-event.spec.ts#L21) | Error: expect(received).toEqual(expected) // deep equality - Expected - 2 + Received + 0 Array [ "http://localhost:8991/empty.html", - "http://localhost:8991/one-style.html", - "http://localhost:8991/one-style.css", ] |
| [40: library/browsercontext-network-event.spec.ts > BrowserContext.Events.Response](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-network-event.spec.ts#L40) | Error: expect(received).toEqual(expected) // deep equality - Expected - 2 + Received + 0 Array [ "http://localhost:8995/empty.html", - "http://localhost:8995/one-style.html", - "http://localhost:8995/one-style.css", ] |

## library/browsercontext-page-event.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [129: library/browsercontext-page-event.spec.ts > should have an opener](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-page-event.spec.ts#L129) | Test timeout of 30000ms exceeded. |
| [160: library/browsercontext-page-event.spec.ts > should work with Shift-clicking](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-page-event.spec.ts#L160) | Test timeout of 30000ms exceeded. |
| [173: library/browsercontext-page-event.spec.ts > should work with Ctrl-clicking](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-page-event.spec.ts#L173) | Test timeout of 30000ms exceeded. |

## library/browsercontext-route.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [419: library/browsercontext-route.spec.ts > should fulfill popup main request using alias](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/browsercontext-route.spec.ts#L419) | Error: expect(locator).toHaveText(expected) failed Locator: locator('body') Expected: "hello" Received: "" Timeout: 10000ms Call log: - Expect "toHaveText" with timeout 10000ms - waiting for locator('body') 23 × locator resolved to <body></body> - unexpected value "" |

## library/chromium/oopif.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [353: library/chromium/oopif.spec.ts > should not throw on exposeFunction when oopif detaches](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/chromium/oopif.spec.ts#L353) | Error: page.exposeFunction: Protocol error (Page.addScriptToEvaluateOnNewDocument): Session with given id not found. |

## library/download.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [316: library/download.spec.ts > download event > should report new window downloads](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/download.spec.ts#L316) | Test timeout of 30000ms exceeded. Error: page.waitForEvent: Target page, context or browser has been closed =========================== logs =========================== waiting for event "download" ============================================================ |

## library/har.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [588: library/har.spec.ts > should have popup requests](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/har.spec.ts#L588) | Error: expect(received).toBe(expected) // Object.is equality Expected: 2 Received: 0 |

## library/hit-target.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [366: library/hit-target.spec.ts > should detect overlaid element in a transformed iframe](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/hit-target.spec.ts#L366) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [398: library/hit-target.spec.ts > should click in iframe with padding](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/hit-target.spec.ts#L398) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [422: library/hit-target.spec.ts > should click in iframe with padding 2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/hit-target.spec.ts#L422) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

## library/inspector/cli-codegen-2.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [339: library/inspector/cli-codegen-2.spec.ts > cli codegen > should record open in a new tab with url](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/inspector/cli-codegen-2.spec.ts#L339) | Error: expect(received).toContain(expected) // indexOf Expected substring: "page1" Received string: "const { chromium } = require('playwright');· (async () => { const browser = await chromium.launch({ headless: false }); const context = await browser.newContext(); const page = await context.newPage(); await page.goto('about:blank'); await page.getByRole('link', { name: 'link' }).click({ modifiers: ['ControlOrMeta'] });· // --------------------- await context.close(); await browser.close(); })(); |

## library/inspector/pause.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [207: library/inspector/pause.spec.ts > pause > should highlight pointer, only in main frame](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/inspector/pause.spec.ts#L207) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

## library/page-clock.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [343: library/page-clock.spec.ts > popup > should not run time before popup on pause](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-clock.spec.ts#L343) | Error: expect(received).toBe(expected) // Object.is equality Expected: 1000 Received: 1791745687689 |

## library/page-event-crash.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [42: library/page-event-crash.spec.ts > should emit crash event when page crashes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L42) | Test timeout of 30000ms exceeded. |
| [49: library/page-event-crash.spec.ts > should throw on any action after page crashes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L49) | Error: page.waitForEvent: Target page, context or browser has been closed =========================== logs =========================== waiting for event "crash" ============================================================ Error: browserContext.close: Target page, context or browser has been closed |
| [74: library/page-event-crash.spec.ts > should cancel waitForEvent when page crashes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L74) | Error: browser.newContext: Target page, context or browser has been closed |
| [82: library/page-event-crash.spec.ts > should cancel navigation when page crashes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L82) | Error: expect(received).toContain(expected) // indexOf Expected substring: "page.goto: Page crashed" Received string: "page.goto: Target page, context or browser has been closed Call log: - navigating to \"http://localhost:10647/one-style.html\", waiting until \"load\" " |
| [92: library/page-event-crash.spec.ts > should be able to close context when page crashes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L92) | Error: page.waitForEvent: Target page, context or browser has been closed =========================== logs =========================== waiting for event "crash" ============================================================ Error: browserContext.close: Target page, context or browser has been closed |
| [101: library/page-event-crash.spec.ts > should be able to close page after crash](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/page-event-crash.spec.ts#L101) | Error: page.waitForEvent: Target page, context or browser has been closed =========================== logs =========================== waiting for event "crash" ============================================================ Error: browserContext.close: Target page, context or browser has been closed |

## library/permissions.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [222: library/permissions.spec.ts > storage access](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/permissions.spec.ts#L222) | Error: browserContext.grantPermissions: Protocol error (Browser.grantPermissions): Unsupported native permission: storageAccess |
| [317: library/permissions.spec.ts > can request screen-wake-lock](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/permissions.spec.ts#L317) | Error: browserContext.grantPermissions: Protocol error (Browser.grantPermissions): Unsupported native permission: wakeLockScreen |

## library/popup.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [20: library/popup.spec.ts > should inherit user agent from browser context @smoke](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L20) | Error: expect(received).toBe(expected) // Object.is equality Expected: "hey" Received: "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36" |
| [42: library/popup.spec.ts > should respect routes from browser context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L42) | Error: expect(received).toBe(expected) // Object.is equality Expected: true Received: false |
| [86: library/popup.spec.ts > should inherit http credentials from browser context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L86) | Error: expect(received).toBe(expected) // Object.is equality Expected: "Woof-Woof" Received: "" |
| [102: library/popup.spec.ts > should inherit touch support from browser context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L102) | Error: expect(received).toBe(expected) // Object.is equality Expected: true Received: false |
| [117: library/popup.spec.ts > should inherit viewport size from browser context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L117) | Error: expect(received).toEqual(expected) // deep equality - Expected - 2 + Received + 2 Object { - "height": 500, - "width": 400, + "height": 0, + "width": 0, } |
| [160: library/popup.spec.ts > should respect routes from browser context when using window.open](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L160) | Error: expect(received).toBe(expected) // Object.is equality Expected: true Received: false |
| [177: library/popup.spec.ts > BrowserContext.addInitScript should apply to an in-process popup](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L177) | Error: expect(received).toBe(expected) // Object.is equality Expected: 123 Received: undefined |
| [205: library/popup.spec.ts > should expose function from browser context](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L205) | Error: page.evaluate: TypeError: win.add is not a function at eval (eval at evaluate (:311:30), <anonymous>:3:22) at UtilityScript.evaluate (<anonymous>:313:16) at UtilityScript.<anonymous> (<anonymous>:1:44) |
| [224: library/popup.spec.ts > should not dispatch binding on a closed page](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/popup.spec.ts#L224) | Error: page.evaluate: TypeError: win.add is not a function at eval (eval at evaluate (:311:30), <anonymous>:3:15) at UtilityScript.evaluate (<anonymous>:313:16) at UtilityScript.<anonymous> (<anonymous>:1:44) |

## library/trace-viewer.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [2435: library/trace-viewer.spec.ts > should replace meta content attr that specifies charset](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/trace-viewer.spec.ts#L2435) | Error: expect(received).toBe(expected) // Object.is equality Expected: "<meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\">" Received: "<meta content=\"text/html; charset=utf-8\" http-equiv=\"content-type\">" Call Log: - Timeout 10000ms exceeded while waiting on the predicate |

## library/tracing.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [716: library/tracing.spec.ts > should not flush console events](https://github.com/microsoft/playwright/blob/v1.62.1/tests/library/tracing.spec.ts#L716) | Error: The "path" argument must be of type string. Received undefined Call Log: - Test timeout of 30000ms exceeded |

## page/expect-boolean.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [379: page/expect-boolean.spec.ts > toBeVisible > with frameLocator](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/expect-boolean.spec.ts#L379) | Error: expect(locator).toBeVisible() failed Locator: locator('iframe').contentFrame().locator('input') Expected: visible Error: element(s) not found Call log: - Expect "toBeVisible" with timeout 10000ms - waiting for locator('iframe').contentFrame().locator('input') - Target page, context or browser has been closed Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [391: page/expect-boolean.spec.ts > toBeVisible > with frameLocator 2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/expect-boolean.spec.ts#L391) | Error: expect(locator).toBeVisible() failed Locator: locator('iframe').contentFrame().locator('input') Expected: visible Error: element(s) not found Call log: - Expect "toBeVisible" with timeout 10000ms - waiting for locator('iframe').contentFrame().locator('input') - Target page, context or browser has been closed Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [680: page/expect-boolean.spec.ts > toBeAttached > with frameLocator](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/expect-boolean.spec.ts#L680) | Error: expect(locator).toBeAttached() failed Locator: locator('iframe').contentFrame().locator('input') Expected: attached Error: element(s) not found Call log: - Expect "toBeAttached" with timeout 10000ms - waiting for locator('iframe').contentFrame().locator('input') - Target page, context or browser has been closed Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

## page/locator-misc-2.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [171: page/locator-misc-2.spec.ts > Locator.locator() and FrameLocator.locator() should accept locator](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/locator-misc-2.spec.ts#L171) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

## page/locator-query.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [243: page/locator-query.spec.ts > should allow some, but not all nested frameLocators](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/locator-query.spec.ts#L243) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

## page/page-aria-snapshot-ai.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [70: page/page-aria-snapshot-ai.spec.ts > should snapshot a locator inside an iframe](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-aria-snapshot-ai.spec.ts#L70) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [85: page/page-aria-snapshot-ai.spec.ts > should limit depth across iframe boundary](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-aria-snapshot-ai.spec.ts#L85) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [175: page/page-aria-snapshot-ai.spec.ts > should persist iframe references](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-aria-snapshot-ai.spec.ts#L175) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

## page/page-aria-snapshot.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [699: page/page-aria-snapshot.spec.ts > auto-waits the locator and does not include iframes](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-aria-snapshot.spec.ts#L699) | Error: locator.ariaSnapshot: Target page, context or browser has been closed Call log: - waiting for locator('#target') Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [737: page/page-aria-snapshot.spec.ts > should snapshot a locator inside an iframe](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-aria-snapshot.spec.ts#L737) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

## page/page-click.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [986: page/page-click.spec.ts > should click in an iframe with border](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-click.spec.ts#L986) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [1008: page/page-click.spec.ts > should click in an iframe with border 2](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-click.spec.ts#L1008) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [1030: page/page-click.spec.ts > should click in a transformed iframe](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-click.spec.ts#L1030) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [1090: page/page-click.spec.ts > should click in a transformed iframe with force](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-click.spec.ts#L1090) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |
| [1112: page/page-click.spec.ts > should click in a nested transformed iframe](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-click.spec.ts#L1112) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

## page/page-evaluate.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [894: page/page-evaluate.spec.ts > promise collected](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-evaluate.spec.ts#L894) | Test timeout of 30000ms exceeded. Error: expect(received).toContain(expected) // indexOf Expected substring: "Resulting promise was garbage collected" Received string: "page.evaluate: Test timeout of 30000ms exceeded." |

## page/page-event-popup.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [61: page/page-event-popup.spec.ts > should be able to capture alert](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-event-popup.spec.ts#L61) | Test timeout of 30000ms exceeded. Error: page.waitForEvent: Test timeout of 30000ms exceeded. =========================== logs =========================== waiting for event "popup" ============================================================ Error: page.evaluate: Test timeout of 30000ms exceeded. |

## page/page-event-request.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [198: page/page-event-request.spec.ts > should fire requestfailed when intercepting race](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-event-request.spec.ts#L198) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

## page/page-mouse.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [341: page/page-mouse.spec.ts > should track hover across iframe boundaries](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-mouse.spec.ts#L341) | Error: page.setContent: Target page, context or browser has been closed Call log: - setting frame content, waiting until "load" |

## page/page-network-idle.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [142: page/page-network-idle.spec.ts > should wait for networkidle from the popup](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-network-idle.spec.ts#L142) | Test timeout of 30000ms exceeded. Error: page.waitForLoadState: Test timeout of 30000ms exceeded. |

## page/page-network-request.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [478: page/page-network-request.spec.ts > should not allow to access frame on popup main request](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-network-request.spec.ts#L478) | Test timeout of 30000ms exceeded. Error: browserContext.waitForEvent: Test timeout of 30000ms exceeded. |

## page/page-request-continue.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [590: page/page-request-continue.spec.ts > continue should drop content-length on redirects](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-request-continue.spec.ts#L590) | Error: expect(received).toBeUndefined() Received: "text/plain;charset=UTF-8" |

## page/page-set-input-files.spec.ts

| Case | Missing or differing behavior |
| --- | --- |
| [38: page/page-set-input-files.spec.ts > should upload a folder](https://github.com/microsoft/playwright/blob/v1.62.1/tests/page/page-set-input-files.spec.ts#L38) | Test timeout of 30000ms exceeded. Error: page.evaluate: Target page, context or browser has been closed |
