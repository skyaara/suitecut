# Browser Control

`@suitecut/browser-control` is the standalone Node.js SDK for SuiteCut's native CEF browser, installed Chromium browsers through CDP, and Firefox through WebDriver BiDi. The CEF backend also includes a live viewer with human takeover. It does not depend on the SuiteCut narration, render orchestration, or test runner package, and the protocol adapters have no external browser-automation dependencies.

Requires Node.js 22 or newer. Use an installed Chrome, Chromium, Edge, Brave or Firefox executable for protocol control, or the matching SuiteCut native runtime for CEF. The npm package contains the JavaScript SDK, declarations and native viewer; it does **not** bundle or download browsers or their runtimes.

For applications that do not use Node.js, the separate [native C++ SDK and C ABI](../../native/client/README.md) control SuiteCut's CEF engine or installed Chrome/Chromium directly. That standalone library does not require CEF headers to build and is distributed separately from this npm package and browser runtimes.

The package is built and packed locally; it has not been published to npm. Install the supplied tarball in your application:

```sh
npm install /absolute/path/to/suitecut-browser-control-2.0.0.tgz
```

## Installed browsers

`launchBrowser()` defaults to CEF. Set `backend: 'cdp'` to launch an installed Chrome, Chromium, Edge or Brave executable, or `backend: 'bidi'` for Firefox:

```js
import { launchBrowser } from '@suitecut/browser-control'

const browser = await launchBrowser({
  backend: 'cdp',
  executablePath: '/path/to/google-chrome',
  headless: true,
  viewport: { width: 1440, height: 900 },
})

try {
  const context = await browser.newContext()
  const page = await context.newPage()
  await page.goto('https://example.com')
  console.log(await page.getByRole('heading').innerText())
  await page.setViewportSize({ width: 1280, height: 720 })
  await page.screenshot({ path: 'page.png' })
} finally {
  await browser.close()
}
```

For applications using only protocol control, the lighter `@suitecut/browser-control/remote` entry exports `launchProtocolBrowser()` and `connectBrowser()` without loading the CEF SDK module graph:

```js
import { launchProtocolBrowser } from '@suitecut/browser-control/remote'

const firefox = await launchProtocolBrowser({
  backend: 'bidi',
  executablePath: '/path/to/firefox',
  headless: true,
  viewport: { width: 1280, height: 720 },
})
try {
  const page = await firefox.newPage()
  await page.goto('https://example.com')
  console.log(await page.title())
} finally {
  await firefox.close()
}
```

Owned launches use a unique temporary profile and a loopback debugging endpoint. Closing waits for browser exit and removes that profile. Launch arguments cannot replace the managed profile or debugging endpoint. Browser sandboxing stays enabled unless you explicitly pass an argument such as `args: ['--no-sandbox']` for an environment that requires it.

Attach to an already running browser using its debugging endpoint:

```js
import { connectBrowser } from '@suitecut/browser-control/remote'

const chrome = await connectBrowser({
  backend: 'cdp',
  endpointURL: 'http://127.0.0.1:9222',
  // Or: 'ws://127.0.0.1:9222/devtools/browser/<browser-id>'
  timeoutMs: 15_000,
})
console.log((await chrome.pages()).length)
await chrome.close()

const firefox = await connectBrowser({
  backend: 'bidi',
  endpointURL: 'ws://127.0.0.1:9223/session',
})
console.log((await firefox.pages()).length)
await firefox.close()
```

These endpoints must already be enabled by the browser. Attaching defaults to disconnecting on `close()` without sending browser shutdown or context-removal commands. CDP pages and contexts, including contexts created through the SDK, remain in the running browser; explicitly close any contexts you want removed. Set `closeBrowserOnClose: true` to request browser shutdown instead. CDP accepts an HTTP origin or browser-level WebSocket URL; Firefox BiDi requires the WebSocket session endpoint printed by Firefox, with `/session` appended if its printed URL has no path.

For Firefox, the SDK creates an automation session and ends that session on a normal attachment `close()`, preserving the browser, tabs, user contexts and cookies. Reconnect through the original endpoint and rediscover pages; Firefox assigns new page identifiers to a new automation session. An endpoint occupied by another active automation session is rejected explicitly. Always close attachments cleanly: a dropped WebSocket alone does not end Firefox's session.

The protocol API supports tabs, isolated contexts where advertised, cookies, navigation, evaluation, screenshots, viewport changes, CSS/role/text/label/test-id locators, form controls, and browser-dispatched keyboard and pointer input. Check `browser.capabilities()` for backend capabilities, and use `browser.sendCommand()` for protocol-specific commands. These adapters do not provide CEF media capture or the native viewer, and do not claim parity with every advanced CEF API. Safari/WebKit support has not been verified.

CDP also supports same-origin and cross-origin iframe controls, file-path uploads, downloads, JavaScript dialogs, popup events, hover, dragging and scrolling. Frame scopes share the parent page's action queue. Native input gestures share a bounded browser focus queue, and each gesture activates its target tab before dispatch, and URL reads use browser metadata rather than evaluating each background tab. Explicit title reads query the browser process because target-change notifications can contain stale titles. These extensions are advertised individually by `browser.capabilities()`; they have not been added to the BiDi adapter.

```js
await page.frameLocator('iframe[title="Billing"]').getByLabel('Account number').fill('12345')
await page.getByLabel('Supporting document').setInputFiles('/absolute/path/document.pdf')
const pending = page.waitForEvent('download')
await page.getByRole('button', { name: 'Export' }).click()
const download = await pending
await download.saveAs('/absolute/path/export.csv')
page.once('dialog', async dialog => { await dialog.accept() })
```

Uploads use local file paths without sending file bytes through the protocol. Passing `[]` clears an input. Download files stay in managed temporary storage until browser close; use `saveAs()` to preserve them. Downloads begin after an explicit download subscription (`on` or `waitForEvent`) is installed. Subscribe before triggering the download. Dialogs without listeners are dismissed automatically; subscribing transfers responsibility for answering to the listener. Closing a page rejects its pending event waits, and browser close cleans up managed download storage.

Commands share one persistent, multiplexed WebSocket connection, and page sessions are cached. Input, navigation and viewport actions are serialized per page to preserve ordering. Native input gestures share browser focus ordering across pages; locator readiness, navigation and evaluation on separate pages can progress concurrently. Pending requests, queued actions and protocol bytes have explicit limits. `page.setDefaultTimeout()` controls locator retries; `timeoutMs` on connection or launch controls protocol requests and navigation. The lightweight entry avoids loading CEF control, viewer image codecs and native-only dependencies into a protocol-only process.

## Native CEF viewer

Native `launchBrowser()` defaults to `frameTransport: 'on-demand'`: raw pixels cross into Node only while a page has `onFrame` subscribers, including its viewer. The first subscription requests a current paint; delivery resumes asynchronously, and removing the last subscriber releases the SDK's cached frame. Browser rendering, screenshots, native recording and publishing continue independently. Set `frameTransport: 'always'` for continuous raw delivery and cached-frame replay; low-level `launchNativeBrowser()` retains that default. Use a matching SDK and native helper; newer SDKs fall back to continuous delivery with helpers that lack subscription support.

Use an absolute path to an installed SuiteCut native binary and keep its adjacent runtime assets together:

```sh
npx browser-control view https://example.com \
  --executable /absolute/path/to/suitecut-browser \
  --resolution auto
```

Open the printed private local URL. Use **Take control** to pause AI commands and interact in the same browser session, then **Resume AI** to return control. Forward the local port when running on a remote machine. Treat the URL as a credential.

Use `--resolution auto` to fit the browser viewport to the available viewer area **while a human owns control**. It does not resize the AI's session while AI control is active. Choose a fixed size such as `--resolution 1920x1080`, or use the viewer's resolution selector during human control. A resize changes the page's actual CSS viewport and responsive layout, rather than only scaling the displayed image.

The native capture canvas stays at its launch dimensions. The helper fits the current viewport into that canvas and supplies its content rectangle; the viewer removes letterboxing. Enlarging the page beyond the capture canvas does not add captured detail. Use appropriate launch dimensions when capture fidelity matters.

The viewer allows CSS viewport dimensions up to 3840 × 2160, subject to the current device scale. The physical viewport must also fit within 3840 × 2160: at device scale 2, the maximum is 1920 × 1080 CSS pixels. Automatic mode clamps to these limits; unsupported fixed sizes are rejected. Live resolution changes require the matching updated native helper with frame geometry metadata.

Run `browser-control view --help` for all options. `SUITECUT_NATIVE_EXECUTABLE` supplies the executable path when `--executable` is omitted; the development default is `.suitecut/native-build/suitecut-browser` relative to the working directory. Once published, the same package can be installed by name using `npm install @suitecut/browser-control`.

```js
import { launchBrowser, createBrowserViewer } from '@suitecut/browser-control'

const browser = await launchBrowser({
  executablePath: '/absolute/path/to/suitecut-browser',
  headless: true,
  width: 1280,
  height: 720,
  pixelFormat: 'bgra',
  framesPerSecond: 60,
})
const page = await browser.newPage()
await page.goto('https://example.com')
const viewer = await createBrowserViewer({ browser, page })
console.log(viewer.url)

// Use the SDK from your application or AI agent:
const snapshot = await page.ariaSnapshotJSON({ mode: 'ai' })
console.log(snapshot)
// await page.getByRef(observedElementRef).click()

// When finished:
// await viewer.close()
// await browser.close()
```

This is an ESM package. Its public API is exported from `@suitecut/browser-control`. With CEF, the browser runs offscreen; the viewer streams the same session and forwards human input. The AI decision loop is supplied by your application.

The current native runtime is pinned to CEF `156.0.7+g47dbc1d+chromium-156.0.8078.17`. Features requiring custom CEF patches remain subject to the installed native backend's capabilities. This SDK does not add desktop-wide OS control or bypass those limits.

From the repository, build only this package with `pnpm --filter @suitecut/browser-control build`, then create a tarball with `pnpm --filter @suitecut/browser-control pack --pack-destination /absolute/output/directory`. Publishing is a separate operation; building or packing does not publish it.

The SDK and CEF runtime have separate size budgets. The earlier native-only Linux x64 measurement was about 270 KiB for the SDK tarball (1.3 MiB unpacked), with installed production dependencies occupying about 24.3 MiB of file contents; these figures predate the protocol adapters. Most dependency bytes belong to the viewer's Sharp/libvips image encoder. Sharp loads when creating a viewer, and the YAML parser loads when parsing textual accessibility snapshots; normal SDK import and JSON accessibility snapshots do not initialize them. Both remain installed dependencies so the viewer and existing snapshot APIs work after a normal install. Zod remains required by the native SDK's input and protocol validation.

The build cleans stale output and includes JavaScript reachable from the public SDK and CLI entries, including literal dynamic imports. It retains all transitive declarations. Test runner modules referenced only by types do not need runtime JavaScript in this package.

Measure the current package, installed dependency contents, runtime import graph and fresh-process import timings with:

```sh
pnpm --filter @suitecut/browser-control build
pnpm --filter @suitecut/browser-control run size:audit
```

The report is written to `.suitecut/action-performance/package-audit.json`. Dependency sizes vary by platform; import timings exclude process startup and use a warm filesystem cache.

Distribute the CEF executable and its runtime assets separately. To create a smaller Linux production runtime from the existing build, preserving the development build and every adjacent asset:

```sh
node scripts/package-browser-runtime.mjs \
  --source .suitecut/native-build \
  --output .suitecut/browser-runtime
```

This copies the runtime and strips debug symbols from the copied executable and shared libraries. It keeps locales, licenses, resources, sandbox and GPU support files. The output directory must be new. Pass `--strip false` for an unstripped copy. Point `executablePath` or `--executable` at the resulting `suitecut-browser`; keep the copied assets alongside it.
