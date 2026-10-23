# Native browser-control SDK

Import browser automation from the standalone `@suitecut/browser-control`
package. It exposes native CEF process, context, page, frame, locator, handle,
input, network, worker, storage,
permission, screenshot/PDF, video, tracing, coverage, and debugger controls.
It does not export the test runner, assertions, fixtures, or SuiteCut's narration,
rendering, broadcast, and publishing orchestration. Existing `suitecut/browser`
and `suitecut/native` imports remain compatible.

The standalone ESM package requires Node.js 22 or newer and contains the SDK,
type declarations, authenticated viewer and `browser-control` CLI. It does not
bundle or download the native executable or CEF. Install the matching native
runtime separately and keep its adjacent assets together.

From this repository, run `pnpm --filter @suitecut/browser-control build`, then
install the local package or a locally packed `.tgz` in your application. Local
builds and packs do not publish to npm; publication is a separate operation and
has not been performed as part of this change.

`createBrowserViewer({ browser, page })` adds a local live viewer and human/AI
handover to the same native session. The standalone command is
`browser-control view https://example.com --executable /absolute/path/to/suitecut-browser`.
The default viewer resolution mode is fixed; `--resolution 1920x1080` selects an
initial size and `--resolution auto` follows the controlling human viewer's
available area. SDK applications set `resolutionMode` to `'fixed'` or `'auto'`
on `createBrowserViewer`. The toolbar also offers desktop/portrait presets and
custom dimensions. Only the human owner resizes; read-only viewers do not change
the browser. These are real CSS viewport changes, while capture and recording
retain their launch canvas dimensions. Cropped viewer frames remove unused bars,
and larger viewports can be downsampled. Maximum physical dimensions are
3840 × 2160; higher device scale reduces the available CSS dimensions. The viewer
reports and enforces the applicable limits. See [viewer usage and limits](native-browser-viewer.md).

```ts
import { launchBrowser } from '@suitecut/browser-control'

const browser = await launchBrowser({
  executablePath: '/absolute/path/to/suitecut-browser',
  headless: true,
})
try {
  const page = await browser.newPage({ baseURL: 'https://example.com' })
  await page.goto('/')
  await page.getByRole('link', { name: 'More information' }).click()
  await page.close()
} finally {
  await browser.close()
}
```

`launchBrowser` returns a `NativeBrowserController` around the actual native
process transport. `contexts()`, `defaultContext()`, `version()`, and
`isConnected()` use existing received state without another native command.
`version()` is the CEF version from the native ready message.
`newPage()` uses the existing native page-owned context path; closing that page
also closes its owned context. Use `newContext()` followed by `context.newPage()`
for a context shared by multiple pages.

`browser.close()` is idempotent for concurrent callers and waits for actual
process closure. `browser.closed` observes that same process lifetime, including
unexpected shutdown. `browser.source` provides the native capture frames/audio
and low-level transport. A controller accepts only a process source, rejecting
individual page transports so its close cannot accidentally close just one tab.
Applications that need only the transport can keep using `launchNativeBrowser`.

`browser.capabilities()` returns immutable capability metadata from the actual
native ready packet, without another IPC request. It currently describes the
vendor-dependent directory/reset, WebSocket-routing, full-clock, old-document
coverage, offscreen-OOPIF capture, client-certificate import, concurrent credential
ceremonies and original-widget input receipts. A false flag remains false
when a proposed vendor patch is present on disk. An older helper that does not
report capabilities returns `undefined`, meaning unknown.

`page.webSocketRouting` provides explicit root-target native channel control
through an acknowledged custom-backend protocol. Stock CEF rejects activation.
See [WebSocket routing](features/native-websocket-routing.md) for message decisions,
transport uncertainty and the remaining context/frame/worker inheritance limits.

File mutations retain actual native command acknowledgments and serialize on the
page. `setInputFiles([])` now requests genuine native empty selection and observes
the original input's trusted completion. The pinned stock backend cannot clear a
populated input this way and returns an explicit unsupported error; the earlier
authored `DataTransfer`/event workaround is removed. Directory upload still fails
before enumeration on stock CEF. Its completed native entry remains an isolated
adapter pending a verified custom library. See [file-selection limits](../native/browser/cef-patches/chromium156-empty-file-selection.md).

Ordinary MIDI overrides, navigation-retained coverage, full clocks and stronger
input receipts remain unsupported on the stock library. The isolated adapters
and vendor patch candidates do not activate those capabilities. Coverage source
and interval merging is compiled, but final-document and cross-target receipt
barriers remain prerequisites for enabling navigation retention.

Frame context lookup and relative frame navigation use the owning page's actual
context and base URL. This requires no frame enumeration or context lookup IPC.

The original `suitecut/browser` entry reduced the static local runtime import
graph from 125 modules to 91 at its recorded baseline. This is an import-graph
comparison, not a startup latency benchmark. Shared transport lifecycle and callback-admission
helpers remain because browser operations depend on them; a few retain historical
runner/assertion-oriented internal names. No browser policies move into the SDK.

This entry describes implemented APIs, not complete cross-browser parity. Current
backend limitations remain in [the active backlog](internals/native-browser-active-backlog.md).

The [native performance review](internals/native-performance-review.md) records measured
capture, tracing, HAR replay, transport, lifecycle and viewer improvements, with
the remaining raw-frame bandwidth and multi-page memory limits.
