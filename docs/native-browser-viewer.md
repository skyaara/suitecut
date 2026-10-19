# Live browser viewer and human handover

`@suitecut/browser-control` can show an existing SuiteCut native browser session
in a local web page. A person can take control, click, type, paste text, scroll,
drag and navigate, then return
control to an AI using the same page, cookies and storage.

The standalone SDK requires Node.js 22 or newer. Build it from this repository
with `pnpm --filter @suitecut/browser-control build`, then install the local
package or its packed `.tgz` in your application. Building and packing do not
publish it to npm; this package has not been published as part of this change.
The JavaScript package and matching native runtime are installed separately.
Keep the native executable together with its adjacent CEF/runtime assets.

```sh
browser-control view https://example.com \
  --executable /absolute/path/to/suitecut-browser \
  --resolution auto
```

From the repository, the existing `node dist/cli.js view` command remains
available. `SUITECUT_NATIVE_EXECUTABLE` can supply the native executable path
instead of `--executable`.

Open the printed private link in a regular browser. **Take control** waits for
admitted agent work to finish before enabling input. **Resume AI** releases held
keys and buttons, waits for native automation to settle and returns control.
Ctrl+C stops the standalone viewer and its native helper. `view --help` lists
resolution, viewport, port, JPEG quality and frame-rate options.
The CLI honors `HTTPS_PROXY` / `HTTP_PROXY` and `NO_PROXY`; `--proxy` overrides
the proxy server. SDK applications configure their own browser context proxy.

The server binds to `127.0.0.1`. When running on another machine, use an SSH tunnel
with the same local port and open the original link through that tunnel. There is
no public listener or automatic hosting. The URL fragment holds a bearer token;
the viewer removes it from the address bar after saving it for that browser tab.
Keep the original link private. Requests require the token and validate Host and
Origin. Assets use no external scripts or CDNs.

## Add a viewer to an AI application

```ts
import { launchBrowser, createBrowserViewer } from '@suitecut/browser-control'

const browser = await launchBrowser({
  executablePath: '/absolute/path/to/suitecut-browser',
  headless: true,
  pixelFormat: 'bgra',
})
const page = await browser.newPage()
await page.goto('https://example.com')
const viewer = await createBrowserViewer({ browser, page, resolutionMode: 'auto' })
console.log(viewer.url)

try {
  // Submit one fully awaited agent step at a time. Each step waits during human control.
  await viewer.runAgent(() => page.getByRole('link', { name: 'More information' }).click())
} finally {
  await viewer.close()
  await browser.close()
}
```

`viewer.state()` reports ownership, the selected page, current CSS viewport,
resolution mode, supported maximum dimensions and frame progress.
`viewer.closed` observes shutdown. Only one viewer server can attach to a native
helper. Up to eight clients can watch, with one client owning input across that
helper's contexts and pages. Tab selection is shared among those clients.

Takeover closes new authored command admission at the shared SDK transport,
including page objects created before the viewer attached. Direct SDK calls made
while human control is active fail explicitly. `runAgent()` lets an application
wait for control instead of retrying those calls; submit bounded steps and await
all work they start. Existing callbacks and narrowly scoped retirement commands
remain able to finish. This SDK gate coordinates a trusted application; it is not
a security boundary against an application deliberately bypassing it.

The native `viewerIdle` probe waits for actual automation/backend work, including
work that can outlive a caller timeout. Published AI element references remain
resident. Held agent keys, mouse buttons or touch contacts refuse transfer; the
agent must release them first. If transfer cannot finish within the configured
timeout, control stays with its previous owner. Active recording, publishing,
debugger or other long-lived native work can prevent a transfer. Page JavaScript,
network traffic and timers continue; taking control does not freeze the website.

Human input carries its ownership revision. Stale input, another client's input
and overlapping server actions are rejected. The frontend serializes actions,
coalesces unsent movement and wheel events and bounds its queue. Blur resets held
input. If heartbeats stop for 20 seconds, the server releases held input and keeps
the AI paused until someone reconnects and explicitly returns control. Release
failures keep human ownership for retry. Closing the viewer normally preserves
the browser; if input release cannot be verified during close, the helper closes
instead of reopening AI admission with uncertain input state.

## Resolution and automatic fitting

The default resolution mode is `fixed`: opening a viewer preserves the page's
current viewport. Start at an explicit fixed resolution with
`--resolution 1920x1080` (or `--width` and `--height`), or use
`--resolution auto`. SDK applications select the same behavior through
the `resolutionMode` option on `createBrowserViewer`, set to `'fixed'` or `'auto'`.

The toolbar offers **Native / current**, **Auto fit viewer**, desktop presets
1280 × 720, 1920 × 1080, 2560 × 1440 and 3840 × 2160, and portrait presets
390 × 844 and 768 × 1024. Custom width and height fields accept whole-number
CSS dimensions from 2 up to the maximum supported at the page's device scale.
Unavailable presets are disabled. **Apply** chooses a fixed resolution.

Auto fitting follows the available viewer area after toolbar and padding space,
with a 200 ms debounce. Only the human client currently owning control may
resize the browser. Spectators never affect its resolution; automatic fitting
waits until that client takes control. Pending dimensions coalesce while input
finishes, preserving key and button ordering. Input is disabled during the resize
and until a frame from the acknowledged page revision arrives. Selecting another
page uses that page's actual viewport. Resizing changes the website's real CSS
viewport, so responsive layouts update; it does not merely scale the viewer image.

The native physical viewport is limited to 3840 × 2160 pixels. Device pixel ratio
reduces the maximum CSS dimensions: at scale 2, the maximum is 1920 × 1080 CSS
pixels. The viewer exposes these limits and clamps automatic sizes to them;
explicit sizes outside the limit are rejected.

Capture and recording keep the canvas dimensions selected at launch. A viewport
resize does not change that canvas or the capture/recording frame rate. Native
frames identify their actual content rectangle and CSS viewport; the viewer crops
unused canvas bars before displaying the image. A larger viewport can be
downsampled to fit the fixed capture canvas, so changing to a large resolution
does not guarantee matching image detail in the stream. Launch with suitable
capture dimensions when full-resolution output is needed. Resizing requires a
matching native helper that reports this geometry; older helpers fail explicitly
rather than forwarding input against uncertain coordinates.

## Rendering and current boundaries

The viewer subscribes to existing native frames. It does not capture a new
screenshot for every refresh. The existing open-source Sharp/libvips dependency
encodes a JPEG once and shares it with every viewer. There is at most one encoder
job, one replaceable pending raw frame and one published JPEG. Encoding requires
current client demand and stops when that demand is served. Hidden viewers stop
requesting images. Authenticated frame requests may use `wait=1` to wait for a
current-revision image for up to one second, with at most 32 pending requests;
disconnect, revision changes and shutdown release these waits. Immediate polling
remains supported. The default encoding limit and frontend
polling limit are 30 FPS; `framesPerSecond` can set the encoder's maximum from 1
through 120, while the current frontend displays at most 30 FPS. Actual delivery
depends on capture, encoding and the connection. This viewer does not change the
native capture or recording rate.

Pointer coordinates use the CSS viewport, including a resized page and a device
scale of two. The current viewer requires BGRA capture and an explicit page
viewport (`noViewport` pages are unsupported); that viewport can be resized during
human control. It has no audio stream, desktop/OS controls, native file-picker UI
or file-transfer
panel. Browser-reserved shortcuts may stay in the local browser. Paste forwards
text supplied by the local paste event; it does not read the remote clipboard.
The page display is a canvas and does not mirror the remote accessibility tree
into the viewer's own accessibility tree.

noVNC and Apache Guacamole provide established open-source desktop viewers over
VNC/RDP and related protocols. They need a corresponding desktop server. This
viewer uses SuiteCut's offscreen CEF capture/input directly and does not include
or depend on either project. It does not change website bot-detection behavior.

## Verification

The focused gate tests are `tests/native-viewer-gate.test.ts`. The real browser
check uses the native SDK both for the target browser and for a separate browser
driving the viewer UI:

```sh
SUITECUT_NATIVE_EXECUTABLE=/absolute/path/to/suitecut-browser \
SUITECUT_VIEWER_SDK=/absolute/path/to/dist/browser.js \
SUITECUT_VIEWER_PROOF=/absolute/path/to/output \
  /absolute/path/to/suitecut-proof-supervisor \
    --report /absolute/path/to/output-supervisor.json \
    --timeout-ms 150000 --cleanup-grace-ms 1000 --physical-ms 6000 -- \
    node --max-old-space-size=512 scripts/verify-native-browser-viewer.mjs
```
