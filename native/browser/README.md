# SuiteCut 2.0 embedded browser

The default Chromium Embedded Framework (CEF) capture backend for SuiteCut 2.0. Each source is a
separate sandboxed CEF browser process. Video comes from `CefRenderHandler::OnPaint`;
audio comes from `CefAudioHandler::OnAudioStreamPacket`. No Playwright screenshot
loop, browser extension, or exposed remote-debugging port participates in capture.

The root `suitecut` API uses this backend. `suitecut/playwright` retains the explicit Playwright backend.
The implementation transfers CPU BGRA or I420 buffers. It does **not** implement GPU
texture sharing, hardware encoding, a desktop UI, or the complete Playwright API.

For the root API, migration from the beta, and a repeatable comparison with Playwright,
see [the native-default guide](BETA.md).

## Build

Use CEF **152.0.8+g1ce985c+chromium-152.0.7977.134**, CMake 3.21+, Ninja, and a C++20
compiler. Obtain the matching minimal binary SDK from
[CEF's build distribution](https://cef-builds.spotifycdn.com/index.html).
Keep SDKs and outputs outside source control (for example under `.suitecut/`).
The tested ARM64 archive URL, byte length, and SHA-256 checksum are pinned in
[`cef-build.json`](cef-build.json); verify the archive against that checksum before
extracting it. Builds do not silently fetch or update Chromium.
Fetch the exact [libyuv Git commit](libyuv-build.json) into `.suitecut/libyuv`.
Gitiles-generated source archives vary in metadata between requests, so their
compressed SHA-256 is not a stable pin. Git verifies the fetched objects; check
that `rev-parse HEAD` matches the pinned commit. libyuv supplies SIMD color
conversion for I420 and is linked statically.

```sh
git init .suitecut/libyuv
git -C .suitecut/libyuv fetch --depth=1 https://chromium.googlesource.com/libyuv/libyuv 7c85a3a0820fab29abb502c207d8b9a394e352cc
git -C .suitecut/libyuv checkout --detach FETCH_HEAD
git -C .suitecut/libyuv rev-parse HEAD
```

On macOS ARM64:

```sh
cmake -S native/browser -B .suitecut/native-build -G Ninja \
  -DCEF_ROOT="$PWD/.suitecut/cef-sdk" \
  -DLIBYUV_ROOT="$PWD/.suitecut/libyuv" \
  -DPROJECT_ARCH=arm64 -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/native-build --parallel 4
ctest --test-dir .suitecut/native-build --output-on-failure
export SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser.app/Contents/MacOS/suitecut-browser"
pnpm test:native
```

The CMake bundle includes the CEF framework, sandbox helper apps, CEF license, and
Chromium credits. Keep the complete `.app` together. Distribution builds need
platform signing/notarization; local compilation is not a signed release.

The Linux CMake branch needs a matching CEF SDK and Chromium's runtime/display
dependencies. It is not yet validated by this project's native integration test.
Windows configuration deliberately fails until its sandbox and packaging are
implemented. Do not work around platform problems by disabling the sandbox.

## Node API

```ts
import { launchNativeBrowser, createNativeBroadcast } from 'suitecut/native'

const source = await launchNativeBrowser({
  executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE!,
  width: 1920,
  height: 1080,
  framesPerSecond: 60,
  pixelFormat: 'i420',
})

let broadcast
try {
  await source.navigate('https://example.com')
  broadcast = await createNativeBroadcast({
    source,
    ffmpegPath: '/absolute/path/to/ffmpeg',
    stream: { url: process.env.RTMP_URL!, audio: true },
  })
  await broadcast.ready()
  // Keep running until your application's stop signal or an observed failure.
  await Promise.race([source.failure, broadcast.failure, applicationStopSignal])
} finally {
  await broadcast?.stop()
  await source.close()
}
```

`applicationStopSignal` above is an application-owned promise. RTMP credentials
belong in environment variables or a secret store, never in examples or logs.
CEF and FFmpeg are explicitly selected executables; importing the module does not
download software, launch a browser, or start a broadcast.

Native broadcast audio is disabled by default. Set `stream.audio: true` to send
the selected CEF source's page audio; `false` sends the publisher's silent audio
track and does not subscribe to CEF audio packets.

- `navigate(url)` accepts HTTP(S), resolves after the main-frame load, and rejects
  HTTP errors, loading errors, concurrent navigation, closure, or a 30-second
  command timeout. Page subresources can continue loading afterward.
  Modal JavaScript dialogs and context menus are suppressed, downloads are denied,
  and new popup browser windows are rejected. In-page select popups are composited.
- `sendDevToolsCommand(method, params)` invokes CEF's native DevTools API over the
  private command pipe and returns the untrusted JSON result. Validate returned
  values. It supports input, DOM inspection, evaluation, and other Chromium
  DevTools commands; it is a trusted operator interface, not a page-facing API.
- `onFrame(callback)` delivers owned pixel buffers, their `pixelFormat`, physical width/height,
  a capture timestamp, and a cumulative native video-drop counter. The latest
  frame is delivered immediately to a new subscriber when available. Callbacks
  must be synchronous and return promptly. Do not mutate buffers shared with
  other subscribers; copy before modifying. Unsubscribe using the returned function.
- `pixelFormat: 'bgra'` (default) preserves full-resolution BGRA8 pixels. Use
  `pixelFormat: 'i420'` for streaming: native libyuv converts to contiguous Y, U,
  and V planes with BT.709 limited-range coefficients. I420 needs even CSS
  dimensions, discards alpha, and subsamples chroma; it reduces pipe bandwidth by
  62.5%. The publisher declares the matching matrix/range in the encoded video.
- `onAudio(callback)` delivers owned 48 kHz stereo PCM, signed 16-bit little-endian,
  plus frame count and first-sample timestamp. There is no extension or system
  microphone capture. Silence is supplied by the publisher when no audio arrives.
- Timestamps are mapped into Node's `performance.now()` clock at the startup
  handshake. Video timestamps describe capture callbacks. Audio PTS is anchored
  at its first callback and subsequent PTS deltas are preserved; discontinuities
  over 250 ms re-anchor it. CEF builds have used different audio clock origins.
  This is not a hardware clock or a
  guarantee of zero latency. Clock and A/V drift need measurement in long runs.
- `failure` rejects for unexpected exit, renderer termination, audio error,
  malformed IPC, or a frame/audio subscriber exception. Observe it for each source.
  `closed` resolves once the child and stdio close; `close()` is idempotent and
  forces termination after five seconds if graceful shutdown stalls.
  Each source receives a private temporary Chromium root profile, removed after
  process closure. The user’s normal browser profile is never reused.
  `state` reports `running`, `closing`, `closed`, or `failed`; closed/failed sources
  reject new subscriptions and cannot be selected for a broadcast.
- A broadcast owns its publisher, **not** its sources. `selectSource(next)` requires
  matching physical size, pixel format, and FPS and preserves the publisher connection. It clears
  previous audio. If a source fails, the publisher holds its last frame and emits
  silence; observe `source.failure` and switch to a prewarmed fallback or stop.
  Automatic browser restart and game scheduling remain application responsibilities.
- `deviceScaleFactor` (1 or 2) preserves CSS dimensions while increasing physical capture
  dimensions, up to 3840×2160. Dimensions are immutable for each source. Broadcasts
  require even physical dimensions. Use a replacement source to change layouts.

The existing `suitecut/playwright` and `suitecut/test` APIs keep their existing
backend. This API does not claim Playwright `Page` compatibility with CEF.

## Recording, narration, and presentations

The same `NativeBrowser` can now feed a recording, a broadcast, or both. Recording
uses native raw frames directly and produces ordinary SuiteCut WebM source clips,
checkpoints, narration artifacts, and a manifest consumable by `renderSuiteCut`.
It shares the existing narration worker, presentation actions, manifest writer,
and renderer with Playwright.

```ts
import { launchNativeBrowser, recordNative } from 'suitecut/native'
import { renderSuiteCut } from 'suitecut/render'

const source = await launchNativeBrowser({
  executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE!,
  width: 1920,
  height: 1080,
  framesPerSecond: 60,
  pixelFormat: 'i420',
})
try {
  const recording = await recordNative('Native product tour', async ({ page, suitecut }) => {
    await page.goto('https://example.com')
    await suitecut.highlight(page.locator('h1'))
    await suitecut.narrate('This tour uses the embedded browser.')
    await suitecut.checkpoint('Opening screen')
  }, {
    source,
    capture: { audio: true },
    output: { directory: '.suitecut/native-tour', manifestPath: '.suitecut/native-tour.json' },
  })
  await renderSuiteCut({
    manifestPath: recording.manifestPath,
    outputPath: '.suitecut/native-tour.mp4',
    config: { output: { width: 1920, height: 1080, framesPerSecond: 60 } },
  })
} finally {
  await source.close()
}
```

`recordNative(name, callback, options)` requires an already launched, caller-owned
source. It never closes that source or a broadcast using it. `ffmpegPath` is
optional and uses the existing SuiteCut media-tool resolver. Output directory,
manifest path, relative/absolute artifact paths, and audio plugin references use
the same conventions as `suitecut/playwright`.

The callback receives `{ source, page, suitecut, signal, addSource }`:

- `suitecut` supports narration/captions, checkpoint clips, holds, highlights,
  rendered zooms, cursor movement, clicks, typing, and scrolling. Narration model
  preparation pauses the recording clock and frame capture; narration playback
  duration remains in the video. This pause does not pause a concurrent broadcast.
- `await addSource(otherSource)` registers another caller-owned browser and returns
  its `NativePage`. Call `suitecut.selectPage(nextPage)` to switch the authored
  timeline. Sources may use different dimensions/formats/FPS; each records its
  own clip and the renderer composes the selected intervals. Selection is explicit;
  this does not enable popup windows in CEF.
- `capture.size` optionally scales recorded video while leaving CSS layout alone.
  `capture.narrationTailMs` controls the pause after speech. Set CSS dimensions,
  device scale, and capture FPS when launching the source. Unsupported capture
  options are rejected instead of silently ignored.
- `capture.audio` is a boolean and defaults to `false`. Set it to `true` to put
  48 kHz stereo page audio into each native source WebM. The final renderer follows
  authored page selection, inserts silence for holds or sources without audio, and
  mixes page audio with narration. `sourceAudioEnabled`, `sourceAudioVolume`, and
  `narrationVolume` provide final-render controls.
- The native `page` supports `goto`, `evaluate`, `locator`, waits, and private
  DevTools commands. `createNativePage(source)` exposes these controls without
  starting a recording. Evaluation functions must be self-contained and arguments
  and returned values JSON-serializable; there are no remote JS handles.
- Native locators use **strict CSS selectors in the main document**. They check
  uniqueness and dispatch input through Chromium. They do not provide Playwright's
  role/text selectors, iframe/shadow-root traversal, automatic retries, or full
  actionability checks. Await application readiness explicitly. Existing
  Playwright tests and `Page` objects retain their own backend and API.
- Recording observes source/encoder failures and cancellation, finalizes media
  when possible, and writes a failed manifest for callback errors. Sources remain
  caller-owned. Finalization permits up to 60 seconds for the encoder; it does not
  use streaming's five-second shutdown budget.

With `capture.audio: true`, recorded source clips contain Opus page audio alongside
VP9 video. Audio uses the same pause-adjusted recording clock as video, is trimmed
with the selected source intervals, and is mixed with synthesized narration during
rendering. Authored zoom is applied by the final renderer, not by changing the
native page's viewport. Concurrent broadcasts receive browser-rendered effects
and captions, but not the recording renderer's zoom transform or narration mix.

Run `pnpm test:native:recording` with `SUITECUT_NATIVE_EXECUTABLE` set. The verifier
records native input and presentation actions, excludes deliberately slow audio
preparation, switches sources, renders the manifest, decodes output audio, and
checks source-specific page tones, narration mixing, the default audio-off path,
and callback-failure/cancellation cleanup. Its narration fixture is a
repeatable tone, not a speech-model quality benchmark.

## Memory and backpressure

CEF callbacks never wait for pipe writes. The native writer retains one pending
video frame, up to 100 audio packets, and up to 128 control packets. A stalled
consumer causes video replacement and old audio eviction. Node retains only the
latest source frame unless the caller retains more. Commands are capped at 64 KiB
and 64 outstanding requests; results at 1 MiB; packet headers at 16 KiB.

The publisher retains enough raw video for its 150 ms audio delay plus three
frames, with a minimum cap of 64 MiB. At 1080p60 the raw queue cap is about 95 MiB;
at 4K60 it is about 380 MiB, excluding the CEF surfaces, in-flight pipe buffers,
encoder, and consumer-retained buffers. BGRA transfer is approximately 475 MiB/s
at 1080p60 and 1.85 GiB/s at 4K60. With I420, transfer is approximately 178 MiB/s
and 712 MiB/s respectively; publisher queue caps are 64 MiB and about 142 MiB.
GPU texture sharing is the next optimization to
evaluate, not a capability of this implementation.

## IPC protocol v1

The executable reads newline-delimited UTF-8 JSON on stdin (64 KiB per command).
Every command has `version: 1`, a positive integer `id`, and `command`:

```json
{"version":1,"id":1,"command":"navigate","url":"https://example.com"}
{"version":1,"id":2,"command":"devtools","method":"Runtime.evaluate","params":{"expression":"document.title","returnByValue":true}}
```

EOF requests graceful browser shutdown. Stdout is exclusively binary IPC:
four-byte little-endian JSON-header length, header bytes, then exactly `bytes`
payload bytes. All headers include `version`, `type`, and `bytes`.

| Type | Header fields | Payload |
| --- | --- | --- |
| `ready` | `timestampMs`, `cefVersion` | none |
| `frame` | `timestampMs`, `width`, `height`, `pixelFormat`, `dropped` | tightly packed BGRA8 or contiguous I420 planes |
| `audio` | `timestampMs`, `frames` | interleaved stereo PCM s16le |
| `response` | `id`, `ok`, optional `error` | optional JSON DevTools result |
| `error` | `error` | none; source is no longer reliable |

Native timestamps are milliseconds from process start. Audio presentation time
is anchored against callback receipt time. Unknown versions and malformed
lengths are fatal; consumers must not attempt to resynchronize a corrupt stream.
Stderr is drained without forwarding page URLs or browser diagnostics into reports.

## Open-source distribution

SuiteCut's source is MIT licensed. CEF and Chromium retain their own licenses and
third-party notices. SDK binaries are not committed or included in the npm package.
libyuv retains its BSD-3-Clause license. Keep `CEF-LICENSE.txt`, `CEF-CREDITS.html`,
and `libyuv-LICENSE.txt` in any native binary distribution.
See [AUDIT.md](AUDIT.md) for validation evidence and release limitations.
