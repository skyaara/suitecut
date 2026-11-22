# Shared-browser prototype: window mode and control protocol

`suitecut-browser --suitecut-prototype=tabs` runs one native CEF window with
suitecut's own tab strip and toolbar instead of Chrome's. Every tab is a full
Chromium page in Alloy style. Nothing is streamed: the pages draw in the window,
and a person can use the same tabs a program is driving.

This is a prototype, not a supported product mode (see the comment at the top of
`native/browser/process/prototype_tabs.h`). The TypeScript SDK and the `npx` MCP
server that used to drive it were removed with the Node product; what remains is
the engine side described here. For apps that embed a browser and share it with
an agent, use the [embed kit](embedding/embed-kit-macos.md) and `suitecut/embed.h`
instead.

## Launch flags

`prototype_tabs.h` lists the flags:

```
--suitecut-prototype=tabs --suitecut-prototype-root=<empty temp dir>
[--suitecut-prototype-pages=chrome|alloy] [--suitecut-prototype-switch=swap|hide]
[--suitecut-prototype-selftest] [--suitecut-prototype-control] [--suitecut-prototype-autodialog]
[--suitecut-prototype-url=<first page>] [--suitecut-prototype-permissions=ask|allow|deny]
[--suitecut-prototype-dialogs=control] [--suitecut-prototype-size=<width>x<height>]
```

## Control protocol

With `--suitecut-prototype-control` the browser reads one JSON command per line
on stdin. It writes replies (`{id, ok, result}`) and events (`{event, tab, ...}`)
on stdout. CDP methods run in-process through `CefBrowserHost::ExecuteDevToolsMethod`.
There is no remote debugging port.

| Command | Fields | Result |
| --- | --- | --- |
| `open` | `url` | the new tab's id |
| `tabs` | | `[{tab, title, url, active}]` |
| `select`, `close` | `tab` | |
| `navigate` | `tab`, `url` | |
| `back`, `forward`, `reload`, `stop` | `tab` | |
| `cdp` | `tab`, `method`, optional `params` | the DevTools result |
| `dialog` | `tab`, `accept`, `text` | answers a held JavaScript dialog |
| `permission` | `allow` | answers the pending permission prompt |
| `ui` | `js` | runs script in suitecut's toolbar |
| `quit` | | closes the window |
| `record-start`, `record-stop`, `frame-probe` | see below | macOS only |

```json
{"id":1,"cmd":"open","url":"https://example.com/"}
{"id":2,"cmd":"cdp","tab":1,"method":"Runtime.evaluate","params":{"expression":"document.title","returnByValue":true}}
```

Events are `ready`, `page`, `loading` (`loading`, `url`, `back`, `forward`),
`load-error` (`code`, `error`, `url`), `popup` (`opened`, `url`, `gesture`),
`dialog`, `permission-prompt`, `media-permission`, `download-started`,
`download-complete`, `download-canceled`, `fullscreen` and `closed`. Popups and
`target=_blank` links open as tabs.

`--suitecut-prototype-dialogs=control` holds each JavaScript dialog until a
`dialog` command answers it. `--suitecut-prototype-permissions=ask` shows Allow
and Block in the toolbar; `allow` and `deny` answer every prompt without asking.

## Recording

Recording uses the frames Chromium's compositor draws for the window, not
screen recording or a DevTools screencast, and encodes them with the engine's
media pipeline.

- **Where frames come from:** the shared browser starts Chromium with
  `--disable-features=RemoteCoreAnimationAPI`. Each finished frame then reaches
  the browser process as an IOSurface on the window's layer, instead of being
  drawn into the window from the GPU process. The browser hooks that one layer
  and takes each frame when the layer receives it.
  `--suitecut-window-frames=off` restores Chromium's default, which disables
  recording.
- **Rate:** `fps` (at least 1, default 60) sets the video's rate. Only frames due
  at that rate are copied. When nothing repaints, the pipeline holds the last
  frame, so a static page costs nothing.
- **Area:** `area` is `page` (the tab in front; switching tabs switches what is
  recorded) or `window`.
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

```json
{"id":3,"cmd":"record-start","path":"/abs/out.mp4","fps":60,"area":"page","width":0,"height":0,"audio":false,"ffmpegPath":"ffmpeg"}
{"id":4,"cmd":"record-stop","recording":"recording-1","cancel":false}
```

`record-start` replies once frames are being encoded, with the recording's id.
`record-stop` replies with the finished file's details. An existing file is
refused. `frame-probe` (`ms`, `path`) reports window frame counts and timing
for diagnostics.

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

## Bot detection

`.suitecut/bot-detection-custom-ui-2026-11-01/` holds the 2026-11-01 benchmark.
It was driven through the removed TypeScript SDK and MCP server, with the same
five detectors, snapshot script and input as the 2026-10-20 runs.
