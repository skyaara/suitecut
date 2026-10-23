# SuiteCut native guide

SuiteCut controls an offscreen CEF browser and captures its pixels and page audio
through native callbacks. The Node API currently handles source lifecycle,
recording, presentation, publishing and FFmpeg composition.

## Setup

Use Node.js 22+, pnpm 11, FFmpeg and FFprobe. Build the pinned CEF executable using
[the native build instructions](../native/browser/README.md#build), then set
`SUITECUT_NATIVE_EXECUTABLE` to its absolute path. A missing binary is an error.

```sh
pnpm install --frozen-lockfile
pnpm build
node dist/cli.js doctor
```

`node dist/cli.js install` downloads checksum-verified media tools. Explicit
`SUITECUT_FFMPEG_PATH` and `SUITECUT_FFPROBE_PATH` select local executables.

## Recording

```ts
import { defineSuiteCut, renderSuiteCut } from 'suitecut'

const record = defineSuiteCut({
  native: { width: 1920, height: 1080, framesPerSecond: 120 },
  capture: { framesPerSecond: 60, audio: true, narrationTailMs: 350 },
  output: { directory: '.suitecut/tour', manifestPath: '.suitecut/tour.json' },
})

const result = await record('Tour', async ({ page, suitecut }) => {
  await page.goto('https://example.com')
  await suitecut.highlight(page.locator('h1'))
  await suitecut.narrate('The opening page is ready.')
  await suitecut.checkpoint('Opening page')
  await suitecut.hold(1000)
})

await renderSuiteCut({
  manifestPath: result.manifestPath,
  outputPath: '.suitecut/tour.mp4',
  config: { output: { width: 1920, height: 1080, framesPerSecond: 60 } },
})
```

The root recorder owns and closes its primary source. `recordNative` in
`suitecut/native` accepts a caller-owned source and leaves it open. `addSource`
registers additional caller-owned sources; `suitecut.selectPage` selects their
intervals for the authored timeline. Callback failures and cancellation propagate
and produce failed manifests when finalization is possible.

## Browser controls

`NativePage` supports `goto`, `evaluate`, `locator`, `waitForTimeout`,
`waitForLoadState('domcontentloaded')`, `isClosed` and private DevTools commands.
Functions and values used by evaluation must be self-contained and JSON
serializable. There are no remote object handles.

Locators require exactly one CSS match in the main document. They support
bounding boxes, element evaluation, scroll-into-view, hover, click, fill and
sequential text insertion. Click/hover use basic geometry, hit-test and enabled
checks; complete automatic actionability/retry behavior is not implemented.
Role/text selectors, shadow-root traversal, frame APIs, popup windows and downloads
are not supported.

## Capture settings and resampling

Native launch controls CSS `width`/`height`, `deviceScaleFactor` (1 or 2),
`framesPerSecond` (integer 1–120), and `pixelFormat` (`bgra` or `i420`). Root launch
defaults are 1920×1080, scale 1, 60 FPS and I420. Physical size is bounded to
3840×2160, and I420 requires even dimensions.

Recording `capture` accepts output `size`, output `framesPerSecond`,
`narrationTailMs` and boolean `audio`. Output FPS defaults to each source's
requested capture rate. The recorder samples the newest captured frame at or
before each output tick, holding or dropping images without interpolation.

Native broadcast `stream.framesPerSecond` selects a fixed output cadence;
omission uses the first source's rate. Source changes may use different capture
rates if dimensions and pixel format match. Audio remains on its 48 kHz sample
clock, with whole-sample video boundaries that avoid fractional-FPS rounding drift.
See [the detailed cadence contract](../native/browser/README.md#frame-rates-and-resampling).

A 120 FPS encoded file can contain repeated frames. Decoded distinct-frame checks
are separate from actual CEF capture throughput; the latter is not yet verified
at 120 FPS in this workspace.

## Presentation and audio

The recording fixture supports `selectPage`, `narrate`, `checkpoint`, `hold`,
`highlight`, `zoom`, `hover`, `click`, `type`, `scrollTo` and `scrollTop`.
Narration preparation pauses the recording clock, while playback duration stays
in the video. Captions and highlights are rendered in the browser; authored zoom
is applied by the final renderer. Checkpoints are video clips.

Page audio defaults to disabled. `capture.audio: true` records 48 kHz stereo page
audio as Opus with VP9 source video. Final rendering follows source selection and
mixes it with narration. `sourceAudioEnabled`, `sourceAudioVolume` and
`narrationVolume` control the final mix.

Local Kokoro narration and optional model packages share the worker contract.
Choose a provider with `audioPlugins` and `suitecut.narrate` options. Audio plugins
must write mono PCM WAV to the requested path and must not download model files
implicitly. See [audio packages](https://github.com/skyaara/suitecut/tree/audio-plugins/plugins) (on the `audio-plugins` branch) and [the plugin API](../src/audio-plugin.ts).

## Streaming

```ts
import { launchBrowser, createBroadcast } from 'suitecut'

const source = await launchBrowser({ framesPerSecond: 120 })
const broadcast = await createBroadcast({
  source,
  stream: { url: process.env.RTMP_URL!, framesPerSecond: 60, audio: true },
})
try {
  await source.navigate('https://example.com')
  await broadcast.ready()
  await Promise.race([source.failure, broadcast.failure, applicationStopSignal])
} finally {
  await broadcast.stop()
  await source.close()
}
```

`applicationStopSignal` is application-owned. Native streams accept RTMP/RTMPS URLs,
output size, boolean audio, a fixed bitrate and fractional output rates through 120.
`await health()` and `await status()` report actual native counters.
`await selectSource(pageSource)` preserves the connection and clears old-source
audio, requiring a real page in the same native helper. Graceful page/context
closure finalizes the publisher; failures remain observable. Reconnect, adaptive
bitrate and diagnostic callbacks are explicitly unsupported by the native wrapper;
see [native publishing](native-publishing.md).

Recording and broadcasting can share a source. Recording-only pauses and
selection do not affect the broadcast; final-render zoom and narration mixing
are not applied to a concurrent broadcast.

## Renderer and CLI

The renderer consumes the same native manifests and supports MP4, WebM, MOV and
MKV, dimensions, FPS 1–120, supported FFmpeg codecs/pixel formats, quality profiles,
source audio and narration controls. Inputs cannot be overwritten by the output.
It writes a report with timing decisions and diagnostics.

```sh
node dist/cli.js render --manifest .suitecut/tour.json --output .suitecut/tour.mp4 --fps 60
node dist/cli.js --help
```

The CLI provides `install`, `doctor`, `render` and help.

## Validation

```sh
pnpm build
pnpm typecheck
pnpm test
pnpm test:native:cadence
pnpm test:native
```

The last command needs the built CEF executable. Native C++ tests cover frame-rate
parsing, audio timestamps and pixel conversion. Native verifiers retain reports
and decoded media under `.suitecut/`; the cadence verifier needs FFmpeg but no
browser. See [native validation status](../native/browser/AUDIT.md) for remaining
platform, throughput and release gates.
