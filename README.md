# SuiteCut

SuiteCut records Playwright browser flows, then renders narrated MP4 or WebM videos. Use it from a
normal Node script, or add the optional Playwright Test integration when the recording is also a
test. Capture, timing, manifests, overlays, captions, and rendering live in this package. It does not
use `playwright-recast`, hosted model APIs, or cloud speech services.

## Requirements

- Node.js 22 or newer
- `playwright` or `@playwright/test` 1.59 or newer
- At least one supported Playwright browser: Chromium, Firefox, or WebKit
- FFmpeg and FFprobe installed with `npx suitecut install`, available on `PATH`, or configured with `SUITECUT_FFMPEG_PATH` and `SUITECUT_FFPROBE_PATH`
- macOS only when using the optional `/usr/bin/say` provider

## Install

Install SuiteCut and Playwright in the project that owns the browser flow:

```sh
npm install suitecut playwright
npx playwright install chromium
npx suitecut install
npx suitecut doctor
```

If the flow is already a Playwright Test, install the test package too:

```sh
npm install --save-dev suitecut playwright @playwright/test
npx playwright install chromium
npx suitecut install
npx suitecut doctor
```

The npm package includes the local Kokoro model and default voice, so the installed package uses
about 89 MB before its JavaScript dependencies.

### Managed media tools

`suitecut install` downloads pinned FFmpeg 9.0.1 and FFprobe builds for macOS
x64/arm64, Linux x64/arm64, and Windows x64. SuiteCut verifies each archive against
its pinned SHA-256 checksum and checks the executables before publishing the cache.
Downloads use existing public build hosting from Martin Riedl, BtbN, and Gyan. No download
runs during recording or streaming, and npm install itself does not fetch media tools.

Explicit paths take priority, then SuiteCut's versioned cache, then system tools on
`PATH`. Playwright's limited FFmpeg build is not used. `suitecut doctor` checks
H.264/AAC and HTTP/RTMP/RTMPS capabilities without downloading anything.

Set `SUITECUT_MEDIA_CACHE` to choose the cache root. By default it is under the
user's OS cache directory. Run setup during deployment or Docker image construction
as the runtime user, or use a shared cache path accessible to that user. Browser
installation and Linux browser libraries are still managed by Playwright. Linux
media builds require a compatible glibc system and `tar` with xz support; Alpine is not a verified target.
Unsupported platforms can use explicit media-tool paths or a system installation.

If an upstream archive becomes unavailable or fails its checksum, installation
fails and leaves existing tools intact. Updating a pin requires changing both its
URL and checksum and testing that build. See [third-party notices](THIRD_PARTY_NOTICES.md).

### Linux container

Build the included Debian-based image from this checkout:

```sh
docker build -t suitecut:linux-test .
docker run --rm --init --shm-size=1g suitecut:linux-test
```

The image installs Chromium, its Linux libraries, and SuiteCut's pinned media tools
during the build. It runs as the unprivileged `node` user; the default command checks
FFmpeg and FFprobe. Docker builds for the host architecture unless you specify
`--platform`. Use headless Chromium for browser flows in this image.

To run a local JavaScript flow that imports `suitecut`, mount the file into `/app`:

```sh
docker run --rm --init --shm-size=1g \
  -v "$PWD/flow.mjs:/app/flow.mjs:ro" \
  suitecut:linux-test node flow.mjs
```

The image also includes the local streaming verifiers. This test runs moving video
with synchronized flashes and beeps, audio interruptions, page switching, capture
pause, and an encoder restart:

```sh
docker run --rm --init --shm-size=1g \
  -e SUITECUT_AUDIO_STRESS=1 -e SUITECUT_AUDIO_SOAK_SECONDS=120 \
  suitecut:linux-test node scripts/verify-tab-audio.mjs
```

Verification artifacts are written under `/app/.suitecut`. Omit `--rm` and use
`docker cp` before removing the container if you want to retain the recordings and
memory samples. These tests use a local receiver and do not publish to Twitch.

## Record a browser flow

Import `record` or `defineSuiteCut` from `suitecut`. SuiteCut launches and closes the browser, so the
script does not need a test runner, reporter, fixture, or Playwright configuration file.

```ts
import { defineSuiteCut } from 'suitecut'

const record = defineSuiteCut({
  browserName: 'chromium',
  launch: { headless: true },
  context: { baseURL: 'http://localhost:3000' },
  capture: {
    viewport: { width: 1600, height: 900 },
    size: { width: 1600, height: 900 },
    framesPerSecond: 30,
    quality: 100,
  },
  output: {
    manifestPath: '.suitecut/latest-run.json',
    pathKind: 'manifest-relative',
  },
})

await record('creates a report', async ({ page, suitecut }) => {
  await page.goto('/reports')
  const create = page.getByRole('button', { name: 'Create report' })
  await suitecut.highlight(create)
  await suitecut.click(create)
  await suitecut.checkpoint('Report created')
})
```

`record(name, callback, options?)` accepts per-record overrides. `defineSuiteCut()` sets shared
defaults and can add typed setup values with `setup({ browser, context, page, suitecut, onCleanup })`.
Cleanup callbacks run in reverse order after recording stops and before SuiteCut closes the context.
When a recording has a cancellation signal, the same signal is available to setup and recording
callbacks.

Recording options:

- `browserName`: `chromium` by default, `firefox`, or `webkit`.
- `launch`: Playwright `LaunchOptions` passed to the selected browser.
- `context`: Playwright `BrowserContextOptions` used for the owned context.
- `capture`: viewport, source size, frame rate, quality, and narration tail options.
- `audioPlugins`: external narration providers loaded in SuiteCut's worker. Each entry sets a
  provider ID, package or local module, and optional JSON configuration.
- `output.directory`: artifact directory. The default is a unique directory under
  `.suitecut/recordings`.
- `output.manifestPath`: manifest path; default `.suitecut/latest-run.json`.
- `output.pathKind`: `absolute` by default or `manifest-relative`.
- `signal`: an `AbortSignal` that cancels setup, capture, narration, and owned browser resources.

The manifest contains one recording with one attempt and an empty `steps` list because there is
no test runner. Browser actions, popup pages, checkpoints, narration, pointer events, and source
videos use the same recorder as the Playwright Test integration.

### Script examples

The repository includes five scripts that run with Node and `playwright`. None use Playwright Test.

```sh
npm run example:playwright
npm run example:playwright:checkpoints
npm run example:playwright:popup
npm run example:playwright:setup
npm run example:playwright:product
```

- [`examples/playwright.ts`](examples/playwright.ts) is the shortest complete recording.
- [`examples/playwright/checkpoints.ts`](examples/playwright/checkpoints.ts) uses per-record options
  and captures short WebM checkpoint clips.
- [`examples/playwright/popup-tour.ts`](examples/playwright/popup-tour.ts) records a popup, switches
  the active page, then returns to the opener.
- [`examples/playwright/custom-setup.ts`](examples/playwright/custom-setup.ts) starts a local HTTP
  server in typed `setup()` and closes it through `onCleanup()`.
- [`examples/playwright/product-tour.ts`](examples/playwright/product-tour.ts) combines local
  narration, pointer actions, scrolling, highlights, zoom, and a checkpoint.

Run `npm run examples:playwright` to execute all five after one build. Each script writes to its own
artifact directory and manifest path under `.suitecut`.

## Add SuiteCut to Playwright Test

The optional test integration lives at `suitecut/test`. Disable Playwright's separate video recorder
and add the SuiteCut reporter.

```ts
import { defineConfig } from '@playwright/test'

export default defineConfig({
  projects: [
    { name: 'chromium', use: { browserName: 'chromium' } },
    { name: 'firefox', use: { browserName: 'firefox' } },
    { name: 'webkit', use: { browserName: 'webkit' } },
  ],
  reporter: [['line'], ['suitecut/reporter', { outputFile: '.suitecut/latest-run.json' }]],
  use: {
    trace: 'off',
    video: 'off',
  },
})
```

Reporter options:

- `outputFile`: manifest path; default `.suitecut/latest-run.json`. The
  `SUITECUT_MANIFEST_PATH` environment variable takes precedence.
- `pathKind`: `absolute` by default or `manifest-relative` artifact paths.
- `includeStepCategories`: optional allowlist of `hook`, `fixture`, `pw:api`, `expect`, `test.step`,
  `test.attach`, or `unknown` Playwright step categories.

Keep Playwright trace and video recording off for SuiteCut tests. Both use the page screencast before
the SuiteCut fixture can request the full source frame size. SuiteCut keeps its own manifest, source
video, screenshots, and normalized Playwright steps.

## Write a recorded test

```ts
import { expect, test } from 'suitecut/test'

test('creates a report', async ({ page, suitecut }) => {
  await page.goto('/reports')
  await suitecut.narrate('The reports workspace is open.', {
    voice: 'af_heart',
  })

  const createButton = page.getByRole('button', { name: 'Create report' })
  await suitecut.highlight(createButton, { borderColor: '#2DD4BF' })
  await suitecut.hover(createButton)
  await suitecut.click(createButton)

  await suitecut.hold(650)
  const report = page.getByTestId('generated-report')
  await suitecut.zoom(report, { scale: 1.15, holdMs: 1_200 })
  await suitecut.checkpoint('Generated report')
  await expect(report).toBeVisible()
})
```

## Fixture API

All fixture option objects are strict. Unknown keys and values outside the ranges below fail at the
call site.

### `selectPage(page): void`

Selects the main page, popup, or secondary page used by later narration, checkpoints, holds, and
`scrollTop()`. Locator methods select the locator's page automatically. Closing the selected popup
returns selection to its opener when possible.

### `narrate(text, options?): Promise<void>`

Synthesizes speech locally, records the narration event and audio artifact, shows a browser caption,
and waits for the clip plus `narrationTailMs`. Synthesis time is excluded from the recording clock.
`text` must not be empty.

- `provider`: `kokoro` by default, `macos-say`, or an ID registered through `audioPlugins` or
  `suitecutAudioPlugins`.
- `voice`: `af_heart` by default for Kokoro. The system default is used by `macos-say` when omitted.
- `speed`: `0.5` through `2`; default `1`.
- `caption`: caption text that can differ from the spoken text. Omit it to show `text`; use `''` to
  hide the caption without removing the audio.

Built-in Kokoro narration highlights the current spoken word when the displayed caption matches the
synthesized text. A different custom caption remains static because its words do not have a reliable
one-to-one mapping to the narration. Kokoro also writes a `captions` JSON artifact for every
narration event. Its word times are relative to the beginning of that narration clip:

```json
{
  "schemaVersion": 1,
  "type": "word-timings",
  "sourceEventId": "event-id",
  "text": "Create a report.",
  "durationMs": 1050,
  "words": [
    { "text": "Create", "startOffset": 0, "endOffset": 6, "startMs": 150, "endMs": 520 },
    { "text": "a", "startOffset": 7, "endOffset": 8, "startMs": 550, "endMs": 630 },
    { "text": "report", "startOffset": 9, "endOffset": 15, "startMs": 660, "endMs": 980 }
  ]
}
```

The manifest links this artifact to the narration through `sourceEventId`. Other narration providers
continue to show static captions unless they gain timing support.

Because the method waits for audio playback, long narration needs enough Playwright test timeout.

### `checkpoint(label, options?): Promise<void>`

Marks the current page state, keeps the test actions still for a short recording window, and attaches
that window as a WebM clip cut from the page's source screencast. `label` must not be empty.

- `durationMs`: checkpoint clip duration; default `500`, maximum `10000`.

### `hold(durationMs): Promise<void>`

Keeps the active browser page recording its current state for the requested number of milliseconds.
The duration must be greater than zero.

### `highlight(locator, options?): Promise<void>`

Measures a visible locator and displays the highlight in the browser, so its complete animation is part
of the source recording. It rejects hidden or geometry-less locators.

- `durationMs`: total enter, visible, and exit time; default `1200`.
- `mode`: `outline` (default), `fill`, or `spotlight`.
- `geometry`: `element` (default) uses the locator box; `content` tightly wraps rendered text and controls inside a block.
- `paddingPx`: space outside the element; default `8`.
- `borderWidthPx`: default `4`.
- `borderStyle`: `solid` (default) or `dashed`.
- `borderColor`: any browser-supported CSS color; default `#7C3AED`.
- `borderRadiusPx`: default `10`.
- `fillColor`: default `#7C3AED`.
- `fillOpacity`: `0` through `1`; default `0.08`.
- `backdropColor`: spotlight backdrop color; default `#000000`.
- `backdropOpacity`: `0` through `1`; default `0.45`.
- `label`: optional text displayed above the target.
- `enter`: animation object; default `{ type: 'fade', durationMs: 180, easing: 'ease-out' }`.
- `exit`: animation object; default `{ type: 'fade', durationMs: 140, easing: 'ease-in' }`.

Animation `type` accepts `none`, `fade`, `scale`, or `fade-scale`; `easing` accepts `linear`,
`ease-in`, `ease-out`, or `ease-in-out`. `none` makes that phase instantaneous. If enter and exit
durations exceed `durationMs`, SuiteCut still completes both phases and uses no stable interval.
Scale emphasis never transforms the highlight box, so its border stays aligned throughout the phase.

`highlight()`, `hover()`, and `click()` accept `zoom: true` or a zoom options object:

```ts
await suitecut.highlight(details, { zoom: true })
await suitecut.click(publishButton, { zoom: { scale: 1.25, paddingPx: 32 } })
```

Action zoom is off by default. It scrolls the target into view, zooms in before the
action, stays close until the action completes, then zooms out. `holdMs` is the
minimum time spent close, including the action. A longer action extends the hold.
The zoom applies to rendered video and live streams without changing page layout.
Action zooms share the camera queue with `zoom()` calls.

### `zoom(locator, options?): Promise<void>`

Scrolls the locator into view, measures its visible geometry, and records a render-time camera move.
The output renderer evaluates the move continuously, centers edge targets, and pads beyond the captured
browser frame when centering needs it. SuiteCut limits the scale when necessary to retain available
target padding. Concurrent zoom calls run in call order instead of competing for the camera.

- `scale`: `1` through `1.25`; default `1.15`.
- `geometry`: `element` (default) uses the locator box; `content` centers rendered text and controls inside a block.
- `paddingPx`: minimum space retained around the target; default `24`.
- `holdMs`: time at the closest scale; default `900`.
- `enter`: default `{ type: 'scale', durationMs: 260, easing: 'ease-out' }`.
- `exit`: default `{ type: 'scale', durationMs: 220, easing: 'ease-in-out' }`.

Zoom uses the same animation values as highlights. `none` disables that phase. `fade`, `scale`, and
`fade-scale` all use eased camera interpolation because a camera crop has no independent opacity.

### `hover(locator, options?): Promise<void>`

Scrolls a locator into view, moves the recorded cursor to its center, performs a real Playwright
hover, optionally waits for resulting CSS or Web Animations, and then settles.

- `zoom`: optional `true`, `false`, or zoom options object; default `false`.
- `moveDurationMs`: cursor travel time; default `450`.
- `settleMs`: wait after the action; default `120`.
- `waitForAnimations`: wait for application animations; default `true`.
- `animationTimeoutMs`: maximum animation wait; default `2000`.

### `click(locator, options?): Promise<void>`

Uses the same options and cursor movement as `hover()`, performs a real Playwright click, and records
a 280 ms click ripple. The ripple and optional application-animation wait run together before
`settleMs` begins. Navigation caused by the click is supported.

### `type(locator, text, options?): Promise<void>`

Scrolls the field into view and enters `text` one character at a time so the typewriter effect is
visible in the source recording. The text must contain at least one character.

- `delayMs`: time between characters; default `70`.
- `settleMs`: wait after the last character; default `250`.
- `clearExisting`: replace the current value before typing; default `true`. Set it to `false` to type
  at the current caret.

```ts
const projectName = page.getByLabel('Project name')
await suitecut.click(projectName)
await suitecut.type(projectName, 'SuiteCut', { delayMs: 80 })
```

### `scrollTo(locator, options?): Promise<void>`

Uses `scrollIntoView()` and waits for the locator position to remain stable for three animation
frames.

- `behavior`: `smooth` (default) or `auto`.
- `block`: `start`, `center` (default), `end`, or `nearest`.
- `inline`: `start`, `center`, `end`, or `nearest` (default).
- `timeoutMs`: scroll-settle limit; default `2000`.
- `settleMs`: additional wait after settling; default `120`.

### `scrollTop(options?): Promise<void>`

Scrolls the selected page to `{ top: 0, left: 0 }` and uses `behavior`, `timeoutMs`, and `settleMs`
with the same defaults as `scrollTo()`. It accepts `block` and `inline` for a consistent scroll
options type, but they do not affect a window-to-origin scroll.

Pointer hover and click anchors are captured automatically in the main frame. Popups and secondary
pages receive their own page ID, clock mapping, and WebM source. Cross-origin iframe geometry is not
translated in the current release.

## Capture options

Set `suitecutCapture` with `test.use()` or in Playwright configuration:

- `viewport`: browser viewport, default `{ width: 1600, height: 900 }`; maximum `7680x4320`.
- `deviceScaleFactor`: physical pixels per CSS pixel from `1` through `4`. For example, a
  `1920x1080` viewport at `2` renders `3840x2160` pixels without changing page layout.
- `size`: encoded source size, defaulting to the viewport. Larger sizes must preserve the viewport
  aspect ratio. Width and height must be even and no larger than `7680x4320`.
- `framesPerSecond`: `30` or `60`; default `30`.
- `quality`: capture JPEG quality from `1` through `100`; default `100`
  when recording, or `90` in streaming-only mode.
- `narrationTailMs`: non-negative pause after every narration clip; default `350`.

Use `deviceScaleFactor` when you want sharper rasterization without moving responsive breakpoints:

```ts
capture: {
  viewport: { width: 1920, height: 1080 },
  deviceScaleFactor: 2,
  quality: 100,
  framesPerSecond: 30,
  stream: {
    url,
    size: { width: 1920, height: 1080 },
    bitrateKbps: 6000,
    audio: 'tab',
  },
}
```

This keeps `innerWidth` at `1920`, `innerHeight` at `1080`, and `devicePixelRatio` at `2`.
SuiteCut captures `3840x2160` device pixels, then uses Lanczos scaling for the `1920x1080`
recording or stream. It does not set CSS zoom or transforms. Every page in the browser context,
including popups, uses the same scale.

SuiteCut must set the scale when Playwright creates the browser context. The standalone
`record()` API does this automatically. The `suitecut/test` fixture also derives Playwright's
context option from `suitecutCapture.deviceScaleFactor`. If you pass an already-created page to
the lower-level recording API with a different DPR, SuiteCut reports how to configure the context
instead of falling back to CSS scaling.

When `deviceScaleFactor` is omitted, the existing `viewport` plus `size` behavior is unchanged.
In that compatibility mode, a larger proportional `size` still creates a larger browser surface
and uses SuiteCut's layout scaling. New recordings should use `deviceScaleFactor` for supersampling.

## Livestream a website

Set `capture.stream` to publish the selected browser page to an RTMP or RTMPS
ingest endpoint. Streaming never saves source videos or checkpoint clips. There is
no recording toggle; specifying a stream destination always selects streaming-only operation.
Use the complete publish URL, including the stream key, from your livestream service.

```ts
import { record } from 'suitecut'

const url = process.env.SUITECUT_STREAM_URL
if (!url) throw new Error('Set SUITECUT_STREAM_URL')

await record(
  'live website tour',
  async ({ page, suitecut }) => {
    await page.goto('https://example.com')
    // Use normal Playwright or SuiteCut actions. Viewers see changes as they happen.
    await suitecut.hold(30_000)
  },
  {
    capture: {
      viewport: { width: 1280, height: 720 },
      framesPerSecond: 30,
      stream: { url, bitrateKbps: 4500 },
    },
  },
)
```

`stream.url` is required. `stream.bitrateKbps` defaults to 4500 and
`stream.size` defaults to the capture size. The encoder keeps a fixed output size,
letterboxing pages as needed. It sends H.264 video and stereo AAC at 48 kHz.
Audio defaults to silence. Set `stream.audio: 'tab'` to capture the selected tab
through the `suitecut/playwright` recorder:

```ts
capture: {
  stream: { url, audio: 'tab', bitrateKbps: 4500 },
}
```

Tab audio launches an isolated persistent profile with a bundled capture extension.
It requires current Playwright Chromium with `Extensions.triggerAction` support
and works in headed and headless mode. Run `pnpm exec playwright install chromium`
after updating Playwright. Firefox, WebKit, custom browser executables, blocked
service workers, and the Playwright Test fixture do not support this option and
fail explicitly. Popups opened as tabs work; separate popup windows are rejected
because the current Chromium action command crashes on those windows. Silent streaming remains available with the existing browser setup.
The audio mode enables autoplay inside its isolated browser.

Install a full FFmpeg build with `libx264`, AAC, and RTMP support, plus TLS for RTMPS.

Streaming-only sessions keep the latest frame per open page and discard closed-page
history and completed events. They write no source or checkpoint video files. The
returned manifest is an end-of-session summary with no replay timeline. Narration
synthesis uses temporary audio/timing files, which are removed after synthesis in
this mode.

The live output follows `suitecut.selectPage()` and the existing page-selection
behavior for popups and page closure. Idle pages repeat their latest frame. A capture
pause freezes the live image and sends silence while the broadcast clock continues.
Tab audio follows page selection and survives navigation. Switching pages drops
queued sound from the old page. Capture resumes with current audio after a pause.
Microphone input and SuiteCut narration are not mixed into the live output.
Audio mode buffers video and timestamped PCM by 150 ms, uses a shared encoder
sample timeline, fills missing chunks with silence, and discards expired samples.
Encoder reconnections start a fresh audio input and discard the old queue.
The extension sends binary PCM over one persistent loopback WebSocket. Worklet
credits and receiver acknowledgments bound pending audio to four chunks at each
stage. Stalls drop audio instead of building a backlog; disconnected sockets retry
after 250 ms. FFmpeg reads a continuous local HTTP response.
`zoom()` crops and resizes the live JPEG frames before encoding, including its enter,
hold, exit, and easing options. Highlights, cursor movement, click pulses, typing,
scrolling, and captions also appear live. The live camera preserves website layout
and mouse coordinates.
`checkpoint()` holds the current view in streaming-only mode without saving a clip.
Final-render editing, result cards, and audio mixing remain offline operations.

Publishing starts before the recording callback and ends when it returns or the
recording is aborted. Disconnected or stalled encoders reconnect automatically,
starting with a one-second delay and backing off to 30 seconds. The browser and its
script keep running during reconnection; missed frames are discarded. Before the
first successful publish, the recording callback waits. Use `signal` to cancel
that wait or stop an unattended session.

By default, live streaming uses a persistent H.264/AAC publisher, a persistent AAC
encoder, and a separate H.264 video encoder. Bitrate changes prepare a replacement
video encoder, wait for its AVC configuration and a keyframe, and switch video on
the shared capture timeline. The publisher and audio encoder stay running, so a
bitrate change does not reconnect RTMP or reset audio timestamps. The previous
video encoder keeps supplying frames during preparation. A failed replacement
leaves the current encoder running. Handoffs briefly require two video encoders.

Set `stream.adaptiveBitrate: false` to use the simpler fixed-bitrate FFmpeg path.
`reconnect: false` disables recovery from a failed publishing connection; it does
not disable video-only bitrate handoffs on an otherwise healthy connection.

A bounded loopback transport measures the upstream network socket independently
of frame processing. RTMPS remains encrypted and certificate-verified on the
connection to the streaming service. Local transport uses plain RTMP.

Five seconds of sustained socket pressure triggers a reduction; short drain gaps
do not reset the observation window. Reductions are at least 30 seconds apart.
Each lowers video bitrate by 25–50%, using observed socket throughput with room
for AAC and framing. The floor is 25% of the configured bitrate. After five healthy
minutes, bitrate rises by 10% of the configured maximum, without exceeding it.
Resolution and frame rate stay fixed. Frame-processing lag alone never lowers
bitrate. This follows the measured-output approach used by
[OBS](https://github.com/obsproject/obs-studio/blob/master/plugins/obs-outputs/rtmp-stream.c),
while using prepared video encoders rather than a native encoder reconfiguration API.

Temporary capture lag is allowed to catch up. Retained JPEGs are capped at 64 MiB
and 610 frames per video encoder; PCM retention is capped at 510 chunks, about
1.9 MiB. Ten seconds of lag, or earlier frame eviction, skips stale video capture
time while preserving timestamps on the shared audio/video timeline. Repeated
congestion uses a shorter recovery threshold. Encoded tags are incrementally
parsed, size-limited, and backpressured rather than collected in an unbounded queue.
These bounds exclude Chromium, FFmpeg, and operating-system buffers. An actual
publishing failure or 20 seconds without output frame progress still invokes
reconnection. Keeping a socket open cannot prevent buffering when bandwidth is
insufficient or the network stops transmitting.

Use `stream.onDiagnostic` for capture lag, output backlog, progress age, pending
write durations, and bitrate changes. `bitrate-changing` announces preparation;
`bitrate-adjusted` confirms the keyframe handoff; `bitrate-change-failed` reports a
failed preparation. Recovery also considers the publisher's progress and monitored
network queue. These measurements are not Twitch player latency or TCP acknowledgements,
and cannot identify the ISP, route, or ingest server responsible for congestion.

Run `pnpm test:stream:congestion` to inject processing stalls and verify the hang
watchdog. Run `pnpm test:stream:bitrate` to verify video handoffs in both directions,
a failed replacement, and adaptation to a throttled local RTMP receiver. It checks
that the receiver sees one connection, timestamps stay monotonic, decoding succeeds,
and continuous test audio has no handoff gaps. Set `SUITECUT_BITRATE_1080P60=1`
to run that verification at 1080p60. These tests do not broadcast
externally or change system network settings.

Configure `stream.reconnect` with `initialDelayMs`, `maxDelayMs`, and `maxAttempts`.
`maxAttempts: 0`, the default, retries indefinitely. A positive value limits
consecutive retries; a connection lasting at least 30 seconds resets the budget.
Set `reconnect: false` to fail immediately. `record()` rejects when the retry budget
is exhausted. Playwright Test reports terminal stream failures at fixture teardown. Use one publisher per stream key, without parallel tests or retries to
the same destination. Stream URLs and FFmpeg diagnostics are not included in SuiteCut
manifests, but publish URLs are visible in local process arguments.

Run `pnpm test:stream:audio` to verify local RTMP reception, flash/beep synchronization,
navigation, silent-page selection, capture pause, and reconnection. Set
`SUITECUT_AUDIO_HEADED=1` for a visible browser or `SUITECUT_AUDIO_SOAK_SECONDS=3600`
for a longer run. Set `SUITECUT_AUDIO_STRESS=1` to run at 720p, repeatedly block
the audio thread and disconnect its WebSocket, and sample child-process RSS on
macOS/Linux. For example:

```sh
pnpm build
SUITECUT_AUDIO_STRESS=1 SUITECUT_AUDIO_SOAK_SECONDS=600 node scripts/verify-tab-audio.mjs
```

The verifier writes received FLV files and timing results under
`.suitecut/tab-audio-verification`. Windows CI is configured to run the short verification; an
hour-long Windows drift and memory validation is still required before claiming
long-run Windows reliability.

For a visible browser you can operate manually, set `SUITECUT_STREAM_URL` and
`SUITECUT_WEBSITE_URL`, then run:

```sh
pnpm build
node --experimental-strip-types examples/playwright/livestream.ts
```

Set `SUITECUT_STREAM_AUDIO=tab` to include website sound. Press Ctrl+C to stop.
Streaming with silent audio also works under `suitecutCapture` in Playwright Test. RTMP is an ingest output, not a browser player URL; embedded web
playback requires a media server or livestream service.

### Verify live streaming locally

`pnpm build && node scripts/verify-live-stream.mjs` starts a local RTMP receiver,
checks live visual effects, kills and restarts the receiver, and verifies sustained
recovery and an empty source-artifact directory. It saves receiver footage and
screenshots under `.suitecut/live-verification/`. It does not publish externally.

## Capture timing

Every SuiteCut recording runs at presentation speed. There is no capture mode setting.

```ts
test.use({
  suitecutCapture: {
    framesPerSecond: 30,
    narrationTailMs: 350,
  },
})

test('records the real transition', async ({ page, suitecut }) => {
  await page.goto('/reports')
  await suitecut.narrate('Create a report from this workspace.', {
    voice: 'af_heart',
  })

  const create = page.getByRole('button', { name: 'Create report' })
  await suitecut.scrollTo(create)
  await suitecut.highlight(create, { durationMs: 900 })
  await suitecut.click(create)
  await suitecut.hold(500)
})
```

`narrate()` synthesizes speech before it records the event. SuiteCut excludes synthesis time from the capture clock, then records the caption for the measured audio duration. Highlights, cursor travel, click ripples, native scrolling, holds, and application animations all happen inside the selected browser while the screencast is running. `click()` waits for CSS and Web Animations created by the action, up to `animationTimeoutMs`.

The renderer trims the recorded page sources, joins page switches, applies requested camera crops, and mixes narration at each recorded event time. It does not add presentation holds or rebuild browser overlays.

`scrollTo()` and `scrollTop()` use the browser's native scrolling and wait until movement settles. They default to smooth behavior. `scrollTo()` centers the target vertically and uses nearest horizontal alignment unless you override `behavior`, `block`, or `inline`.

Narration synthesis runs while frame ingestion is paused. Model startup time is omitted from the source video and the attempt clock. The page itself remains live during preparation, so call `narrate()` before the action whose animation must be recorded. Camera zoom crops frames in the live compositor or final renderer; it never transforms the application document or changes fixed and sticky layout.

## Local Kokoro narration

Kokoro runs inside SuiteCut's Node worker through `onnxruntime-web/wasm`. Inference stays local. The npm package includes the quantized Kokoro 82M model, tokenizer, and `af_heart` voice profile, so the default Kokoro voice works without a download or network request. These bundled files add about 89 MB to the installed package.

```ts
await suitecut.narrate('The generated report is ready.', {
  voice: 'af_heart',
  speed: 1,
  caption: 'The generated report is ready.',
})
```

SuiteCut supports the 28 English Kokoro voices used by Spek. `af_heart` ships with SuiteCut. Other
voices download from the immutable revision recorded in `assets/kokoro/NOTICE.md`, with a 30-second
request timeout, then remain in the local cache. Common choices include `af_bella`, `af_sky`,
`am_michael`, `bf_emma`, and `bm_george`. American voice IDs start with `a`; British voice IDs start
with `b`.

Cold model startup depends on the machine. If it takes more than five seconds, SuiteCut prints a
notice while keeping the recording clock paused. Maintainers can run `npm run test:kokoro` to perform
a real local inference and validate the resulting mono 24 kHz PCM WAV file.

Optional voices use `~/Library/Caches/SuiteCut/kokoro-82m-v1.0` on macOS and the standard XDG cache directory on Linux. Set `SUITECUT_MODEL_CACHE` to use another directory. Choose the native provider explicitly when needed:

```ts
await suitecut.narrate('Use the installed system voice.', {
  provider: 'macos-say',
  voice: 'Samantha',
  speed: 1,
})
```

Kokoro with `af_heart` is the default. Set `provider: 'macos-say'` only when a test should use an installed macOS system voice.

## Optional audio model plugins

Audio model adapters install separately from `suitecut`. SuiteCut loads the selected adapter in its
narration worker. A plugin cannot replace the built-in `kokoro` or `macos-say` providers.

Sherpa adapters are split by model family. Install only the family used by the recording project:

- `@suitecut/audio-vits` for VITS and Piper
- `@suitecut/audio-matcha` for Matcha
- `@suitecut/audio-kokoro-sherpa` for Sherpa-compatible Kokoro models
- `@suitecut/audio-kitten` for KittenTTS
- `@suitecut/audio-zipvoice` for ZipVoice
- `@suitecut/audio-pocket` for Pocket TTS
- `@suitecut/audio-supertonic` for Supertonic

```sh
npm install --save-dev @suitecut/audio-vits
```

Configure the plugin once for Playwright Test, then use its provider ID in `narrate()`:

```ts
import { test } from 'suitecut/test'

test.use({
  suitecutAudioPlugins: [
    {
      provider: 'piper-lessac',
      module: '@suitecut/audio-vits',
      options: {
        model: './models/piper/en_US-lessac-medium.onnx',
        tokens: './models/piper/tokens.txt',
        dataDir: './models/piper/espeak-ng-data',
      },
    },
  ],
})

test('records Piper narration', async ({ page, suitecut }) => {
  await page.goto('https://example.com')
  await suitecut.narrate('The report is ready.', {
    provider: 'piper-lessac',
    voice: 'default',
    speed: 1.1,
  })
})
```

The primary recorder accepts the same entries through `record()` or `defineSuiteCut()`:

```ts
import { record } from 'suitecut'

await record(
  'Piper report',
  async ({ page, suitecut }) => {
    await page.goto('https://example.com')
    await suitecut.narrate('The report is ready.', {
      provider: 'piper-lessac',
      voice: 'default',
    })
  },
  {
    audioPlugins: [
      {
        provider: 'piper-lessac',
        module: '@suitecut/audio-vits',
        options: {
          model: './models/piper/en_US-lessac-medium.onnx',
          tokens: './models/piper/tokens.txt',
          dataDir: './models/piper/espeak-ng-data',
        },
      },
    ],
  },
)
```

Each family package accepts only that family's options. The packages share
`@suitecut/audio-sherpa-core`, so npm installs one Sherpa runtime when a project uses several
families. Sherpa's current Node WASM file contains the family backends, which means one adapter still
installs that shared runtime.

The adapters never download model files. Download a Sherpa-compatible model separately and keep its
paths in the recording project. Named voices map to numeric speaker IDs through `speakerIds`.
Numeric voice strings work without a map. Sherpa publishes compatible files in its [TTS model
releases](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models).

Single-speaker Piper models use `voice: 'default'`. For a multi-speaker model, pass a numeric voice
such as `voice: '2'`, or configure `speakerIds: { narrator: 2 }` in the plugin options and use
`voice: 'narrator'`.

### Write an audio plugin

An audio plugin is an ESM package with one default export. Its `synthesize()` method receives plain
JSON options and must write a mono PCM WAV file to `outputPath` before it resolves.

```ts
import { writeFile } from 'node:fs/promises'
import { defineSuiteCutAudioPlugin, encodePcm16Wav } from 'suitecut/audio-plugin'

export default defineSuiteCutAudioPlugin({
  async synthesize({ text, voice, speed, outputPath, options }) {
    const { samples, sampleRate } = await runModel({ text, voice, speed, options })
    await writeFile(outputPath, encodePcm16Wav(samples, sampleRate))
  },
  async dispose() {
    await releaseModelResources()
  },
})
```

Provider IDs use lowercase letters, numbers, dots, underscores, and hyphens. SuiteCut resolves
package names from the recording project, validates plugin references before starting the worker,
and caches one loaded plugin per provider for the recording. The optional `dispose()` hook runs once
when the narration worker closes normally so plugins can release models, workers, files, and child
processes.

## Record and render

Run Playwright through SuiteCut:

```sh
suitecut test --manifest .suitecut/latest-run.json -- tests/report.spec.ts
```

Render the first test and latest attempt:

```sh
suitecut render \
  --manifest .suitecut/latest-run.json \
  --output .suitecut/videos/report.mp4
```

Select a specific test or retry when a manifest contains several attempts:

```sh
suitecut render \
  --manifest .suitecut/latest-run.json \
  --test-id <playwright-test-id> \
  --retry 0 \
  --output .suitecut/videos/report.webm \
  --container webm
```

`suitecut test` accepts `--manifest <path>`, removes that option before invoking Playwright, and
forwards every argument after `--` to `playwright test`. It always installs the line reporter and
SuiteCut reporter for that run.

All `suitecut render` flags are listed below. `--manifest` and `--output` are required.

- `--manifest <path>`: saved SuiteCut run manifest.
- `--output <path>`: destination video. Its extension supplies the default container.
- `--test-id <id>`: test to render; default is the first test in declaration order.
- `--retry <number>`: zero-based attempt; default is the latest attempt.
- `--container <mp4|webm|mov|mkv>`: output container; default is inferred from `--output`.
- `--format <format>`: deprecated alias for `--container`. If programmatic config supplies both,
  they must match.
- `--video-codec <name>`: installed FFmpeg video encoder; default `libvpx-vp9` for WebM and
  `libx264` otherwise.
- `--audio-codec <name>`: installed FFmpeg audio encoder; default `libopus` for WebM and `aac`
  otherwise.
- `--pixel-format <name>`: FFmpeg pixel format; default `yuv422p10le` for ProRes and `yuv420p`
  otherwise.
- `--color-range <auto|full|limited>`: output sample range; default `auto`, which selects full for
  RGB formats and limited for YUV.
- `--width <pixels>` and `--height <pixels>`: output dimensions; default `1920x1080`. Supply both;
  each must be even and no larger than `7680x4320`.
- `--fps <30|60>`: output cadence; default `30`.
- `--quality <standard|high|master>`: codec-specific quality and speed profile; default `standard`.
- `--result-hold-ms <ms>`: non-negative final-frame freeze; default `0`.
- `--background-color <color>`: letterbox color; default `#0B1020`. Accepts three- or six-digit hex
  plus `black`, `white`, `red`, `green`, `blue`, and `yellow`.
- `--failure-mode <strict|best-effort>`: default `strict`. Best-effort may omit missing narration or
  narration whose audio encoder is unavailable, and records an `OPTIONAL_TRACK_OMITTED` warning.
  Missing source video, invalid configuration, a missing video encoder, and FFmpeg failure remain
  fatal.
- `--ffmpeg-path <path>`: executable FFmpeg path, overriding automatic resolution and
  `SUITECUT_FFMPEG_PATH`.
- `--no-narration`: render without narration audio. Browser-recorded captions remain visible.

SuiteCut infers MP4, WebM, MOV, or Matroska from the output extension. MP4, MOV, and Matroska
default to H.264 and AAC. WebM defaults to VP9 and Opus. You can select any encoder exposed by the
installed FFmpeg build. FFmpeg still decides whether the chosen container can store that codec and
whether the encoder accepts the selected pixel format.

For example, this command writes 10-bit ProRes and PCM audio to MOV:

```sh
suitecut render \
  --manifest .suitecut/latest-run.json \
  --output .suitecut/videos/report.mov \
  --container mov \
  --video-codec prores_ks \
  --audio-codec pcm_s24le \
  --pixel-format yuv422p10le
```

`--color-range auto` is the default. SuiteCut reads the source range from FFprobe, then writes full
range for RGB pixel formats and limited range for YUV pixel formats. Use `full` or `limited` only
when a delivery requirement calls for it. Color range controls how sample values map to black and
white. It does not select a video or audio codec.

The renderer accepts `standard`, `high`, and `master` quality profiles. They map to codec-specific
CRF, encoder speed, and audio bitrate settings for H.264, HEVC, VP8, VP9, AV1, ProRes, AAC, Opus,
Vorbis, MP3, and AC-3 encoders. Other installed encoders use their FFmpeg defaults. `high` uses
H.264 CRF 18 with the slow preset. `master` uses CRF 14 with the slower preset and produces much
larger files. SuiteCut applies Lanczos scaling with accurate rounding when a source does need
resizing.

The default 1920x1080 output preserves the source's aspect ratio without paying the cost of a 4K60
encode. A 1600x900 source rendered at 3840x2160 is a 4K delivery file, but upscaling cannot recover
detail that the browser did not record. For native 4K source detail, use a proportional 4K capture
size with `deviceScaleFactor: 2`, then request 4K output explicitly.

Programmatic rendering is also exported:

```ts
import { renderSuiteCut } from 'suitecut/render'

await renderSuiteCut({
  manifestPath: '.suitecut/latest-run.json',
  outputPath: '.suitecut/videos/report.mp4',
  selection: { testId: 'optional-playwright-test-id', retry: 0 },
  config: {
    output: {
      container: 'mp4',
      videoCodec: 'libx264',
      audioCodec: 'aac',
      pixelFormat: 'yuv420p',
      colorRange: 'auto',
      width: 3840,
      height: 2160,
      framesPerSecond: 60,
      quality: 'high',
    },
    narrationEnabled: true,
    resultHoldMs: 0,
    backgroundColor: '#0B1020',
    failureMode: 'strict',
    // ffmpegPath: '/path/to/ffmpeg',
  },
  // signal: abortController.signal,
})
```

Every programmatic field maps directly to the CLI behavior above. Omit `selection`, `config`,
`signal`, or individual fields to use their defaults. Passing an `AbortSignal` stops the owned
FFmpeg process and rejects with its abort reason. `output.format` is the deprecated programmatic
alias for `output.container`.

Each render writes a report beside the video as `<video-path>.suitecut.json`. It records the selected
attempt, presentation edits, diagnostics, redacted FFmpeg arguments, process status, and output
duration. Local project, home, temporary, input, and output paths are replaced with placeholders.

## Included examples

- `examples/basic.spec.ts` records a minimal narrated page.
- `examples/checkpoint.spec.ts` checks default and custom checkpoint video windows.
- `examples/product-tour.spec.ts` exercises narration, captions, pointer capture, click effects, holds, highlights, zoom, and checkpoints.
- `examples/popup-tour.spec.ts` records main and popup pages, closes the popup, and switches the rendered source back to its opener.
- `examples/browser-presentation.spec.ts` verifies browser-rendered captions, overlays, and application transitions.
- `examples/browser-support.spec.ts` records and validates the same focused flow in Chromium, Firefox, and WebKit.
- `examples/flickks-launch.spec.ts` records a 1600x900 source and renders a 4K launch film at 60 frames per second.

Run the examples with:

```sh
npm run examples
```

Run the complete local quality gate with:

```sh
npm run check
```

The gate runs type-aware ESLint, Prettier checks, strict TypeScript, unit tests, and a clean package
build.

Run the real browser recording matrix with:

```sh
npm run test:browsers
```

This command records Chromium, Firefox, and WebKit, then checks each manifest attempt, source-video
artifact, event set, dimensions, cadence, and duration.

Reporter or retry changes should also run:

```sh
npm run test:retry-parallel
```

Package metadata and public export changes should run:

```sh
npm run test:package
```

The local example scripts also provide `npm run example:record` and `npm run example:render`.

## Output model

The reporter writes a strict manifest containing:

- `schemaVersion: 1`; the decoder also accepts unversioned pre-v1 manifests as legacy v1 input
- test identity, declaration order, retries, status, errors, and normalized Playwright steps
- the attempt epoch and monotonic clock origins
- page identities, popup openers, close times, and active-page events
- narration, checkpoint, pointer, highlight, zoom, and hold events
- checkpoint, narration, trace, and source-video artifacts with explicit roles
- FFprobe media metadata and first-frame timing for every recorded page

The renderer reads only this saved manifest and its referenced files. It does not require a live page or a Playwright trace.

## Current boundaries

- Bundled `af_heart` inference is local and offline. Other Kokoro voices need network access on first use to populate the voice cache.
- The quantized Kokoro model uses one WASM thread in the current Node worker. Multi-worker inference needs memory and throughput measurements before it becomes a default.
- Pointer geometry covers the main frame. Cross-origin iframe coordinate translation still needs browser-specific work.
- Chromium, Firefox, and WebKit have focused end-to-end capture, interaction, checkpoint, manifest, and media evidence.
- Offline rendering mixes narration only. Chromium tab audio is available for live streaming.

## Contributing and security

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Report vulnerabilities
privately as described in [SECURITY.md](SECURITY.md). User-visible changes are recorded in
[CHANGELOG.md](CHANGELOG.md).

## License

SuiteCut is MIT licensed. The bundled Kokoro assets are Apache-2.0 licensed and carry separate
attribution, pinned source hashes, and license text in `assets/kokoro`.
