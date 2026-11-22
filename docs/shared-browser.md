# Shared browser: SDK and MCP

`@suitecut/shared-browser` (`packages/shared-browser`) is a preview. It runs one native CEF window with
suitecut's own tab strip and toolbar instead of Chrome's. Every tab is a full
Chromium page in Alloy style. Nothing is streamed: the pages draw in the
window, and a person can use the same tabs an agent is driving.

The browser is `suitecut-browser` started with `--suitecut-prototype=tabs`. See
`native/browser/process/prototype_tabs.h` for its flags. With
`--suitecut-prototype-control` it reads one JSON command per line on stdin. It
writes replies (`{id, ok, result}`) and events (`{event, tab, ...}`) on stdout.
CDP methods run in-process through `CefBrowserHost::ExecuteDevToolsMethod`.
There is no remote debugging port.

## TypeScript SDK

```ts
import { launchSharedBrowser } from '@suitecut/shared-browser'

const browser = await launchSharedBrowser({ executablePath, url: 'https://example.com/' })
const tab = await browser.activeTab()
console.log(await tab.snapshot()) // headings, text, and interactive elements with refs
await tab.fillRef('e3', 'hello')
await tab.clickRef('e4')
const png = await tab.screenshot()
await browser.close()
```

- **Tabs:**
  - `browser.tabs()`, `browser.newTab(url)`, `browser.tab(id)`, `browser.activeTab()`.
  - Popups and `target=_blank` links open as tabs, and are reported as `popup` events.
- **Navigation:**
  - `tab.goto(url)`, `back()`, `forward()` and `reload()` wait until loading ends.
  - Load errors are returned, not thrown.
- **Reading the page:**
  - `evaluate(expression)` returns JSON values; promises are awaited.
  - `title()`, `url()`, `content()`, `screenshot()`.
  - `snapshot()` gives an agent-oriented outline. Refs such as `e12` are positions among visible interactive elements. A ref is found again when it is used, so the page's DOM and globals are never touched.
- **Input:**
  - `mouseMove(x, y, steps)`, `click(x, y)`, `wheel(dx, dy)`, `type(text)`, `press(key)`, `clickRef(ref)`, `fillRef(ref, text)`.
  - Events go through CDP `Input.*`, so pages see them as trusted.
- **Dialogs:**
  - By default (`dialogs: 'control'`) a dialog waits for `tab.answerDialog(accept, text)`.
  - Input that opens a dialog returns at once.
  - Reads of a paused page fail with a clear message, and `snapshot()` reports the open dialog.
- **Permissions:**
  - `permissions: 'ask'` shows Allow and Block in our toolbar; `browser.answerPermission(allow)` answers it.
  - `'allow'` and `'deny'` answer every prompt without asking.
- **Background tabs:**
  - A hidden page draws no frames, whether its tab is in the background or its window is covered.
  - The first time the SDK sends input or takes a screenshot on a tab, it turns on `Emulation.setFocusEmulationEnabled`, as Playwright does. That tab then keeps rendering and reports itself visible and focused.

## Recording

```ts
const recording = await browser.startRecording({ path: 'demo.mp4', fps: 60 })
// ... drive the page, or let a person use it ...
const result = await recording.stop() // outputFrames, heldFrames, durationMs, ...
```

Recording uses the frames Chromium's compositor draws for the window, not
screen recording or a DevTools screencast, and encodes them with the same media
pipeline as `record()`.

- **Where frames come from:** the shared browser starts Chromium with
  `--disable-features=RemoteCoreAnimationAPI`. Each finished frame then reaches
  the browser process as an IOSurface on the window's layer, instead of being
  drawn into the window from the GPU process. The browser hooks that one layer
  and takes each frame when the layer receives it. Pages cannot tell; Nike's
  bot protection still let a test add a shoe to the bag while recording.
  `--suitecut-window-frames=off` restores Chromium's default, which disables
  recording.
- **Rate:** `fps` (at least 1, default 60) sets the video's rate. Only frames due
  at that rate are copied. When nothing repaints, the pipeline holds the last
  frame, so a static page costs nothing.
- **Area:** the video is cropped to the tab in front, so switching tabs switches
  what is recorded. The tab strip and toolbar are drawn on a separate surface and
  aren't recorded yet.
- **Size:** the captured area in device pixels by default (2560x1504 for a
  1280x752 page on a 2x display), with no maximum. When it matches the
  video size, the crop and NV12 conversion stay on the GPU and go straight to
  hardware H.264. `width` and `height` change the size; the media worker then
  scales each frame.
- **Audio:** `audio: true` adds the front tab's sound as 48 kHz stereo AAC.
  CEF's audio capture mutes the page it captures, so capture is on only while
  such a recording runs, and the browser plays the captured sound to the
  speakers meanwhile; a person in the window still hears the page. A sound that
  started before the recording is picked up. In a test where a page flashed and
  beeped together, the beep landed 14 to 19 ms after the flash in the file.
  Capture can stay on after the recording until the page next goes quiet; it
  stays audible through the same playback.
- **Minimized window:** Chromium draws nothing, and the video holds the last frame
  until the window is back.
- **Measured (M4, 1280x860 window, 2x):** an animation recorded at 60 fps gave 241
  distinct frames in 4.0 s, none held or dropped. At 30 fps the browser skipped
  every other frame before copying it. In the same test, a DevTools screencast
  from Google Chrome delivered 54 to 58 fps and used about 1.8 more CPU cores
  for JPEG encoding.
- **Protocol:** `{"cmd":"record-start","path":"/abs/out.mp4","fps":60,"area":"page","width":0,"height":0,"audio":false,"ffmpegPath":"ffmpeg"}`
  replies once frames are being encoded. `{"cmd":"record-stop","recording":"recording-1","cancel":false}`
  replies with the finished file's details. An existing file is refused.

## Window size after navigation

In Alloy Views on macOS, a new document widget can report its window
geometry as 0 until shortly after `load`. The affected values are
`outerWidth`, `outerHeight` and `screenX`. A new widget comes from
RenderDocument, or from the process swap that BackForwardCache makes.

- **How often:** 7 of 40 navigations without the fix.
- **Why it matters:** Chrome never reports 0 for a visible window, and BotD
  classified such a page as headless Chrome.
- **Fix:** the shared browser starts Chromium with
  `--disable-features=RenderDocument,BackForwardCache`. With that, 0 of 40
  navigations showed it, and about 1 in 80 in a second sample.
- **Cost:** Back and Forward reload pages instead of restoring them from the
  cache.

## MCP server

```bash
npx -y @suitecut/shared-browser --executable <path to suitecut-browser>
```

It speaks MCP over stdio. `--executable` falls back to `SUITECUT_SHARED_BROWSER`.
The browser starts on the first tool call, and again if its window was closed.
Example client configuration:

```json
{
  "mcpServers": {
    "suitecut-browser": {
      "command": "npx",
      "args": ["-y", "@suitecut/shared-browser", "--executable", "<suitecut-browser>"]
    }
  }
}
```

Tools: `browser_navigate`, `browser_snapshot`, `browser_click`, `browser_hover`,
`browser_type`, `browser_press`, `browser_scroll`, `browser_evaluate`,
`browser_screenshot`, `browser_wait_for`, `browser_back`, `browser_reload`,
`browser_tabs`, `browser_new_tab`, `browser_select_tab`, `browser_close_tab`,
`browser_dialog`, `browser_permission`, `browser_events`, `browser_record_start`,
`browser_record_stop`.

## Bot detection

`.suitecut/bot-detection-custom-ui-2026-11-01/` holds the 2026-11-01 benchmark,
driven through both the SDK and MCP. It uses the same five detectors, snapshot
script and input as the 2026-10-20 runs.
