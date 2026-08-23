# SuiteCut

SuiteCut records Playwright tests and renders narrated MP4 or WebM videos. Capture, timing, manifests, overlays, captions, and rendering live in this package. It does not use `playwright-recast`, hosted model APIs, or cloud speech services.

## Requirements

- Node.js 22 or newer
- `@playwright/test` 1.59 or newer
- Chromium installed through Playwright
- FFmpeg and FFprobe available on `PATH`, or `SUITECUT_FFMPEG_PATH` and `SUITECUT_FFPROBE_PATH` set explicitly
- macOS only when using the optional `/usr/bin/say` provider

On macOS, install the media tools with:

```sh
brew install ffmpeg
```

## Configure Playwright

Import SuiteCut's fixture, disable Playwright's separate video recorder, and add the SuiteCut reporter.

```ts
import { defineConfig } from '@playwright/test'

export default defineConfig({
  reporter: [['line'], ['suitecut/reporter', { outputFile: '.suitecut/latest-run.json' }]],
  use: {
    trace: 'off',
    video: 'off',
  },
})
```

Keep Playwright trace and video recording off for SuiteCut tests. Both use the page screencast before
the SuiteCut fixture can request the full source frame size. SuiteCut keeps its own manifest, source video,
screenshots, and normalized Playwright steps.

Set a capture profile when a test needs a fixed source size or cadence:

```ts
test.use({
  suitecutCapture: {
    viewport: { width: 1600, height: 900 },
    size: { width: 1600, height: 900 },
    framesPerSecond: 60,
    quality: 100,
  },
})
```

SuiteCut uses a 1600x900 viewport and source recording by default. Capture runs at 60 frames per
second and JPEG screencast quality defaults to 100. `viewport` and `size` are separate, strictly
validated settings. The source size cannot exceed the viewport because Playwright's public
screencast does not create pixels beyond the rendered page. Capture dimensions must be even because
the encoder writes YUV 4:2:0 video.

The default render is already 4K at 60 frames per second. An explicit command looks like this:

```sh
suitecut render \
  --manifest .suitecut/latest-run.json \
  --output .suitecut/videos/tour-4k.mp4 \
  --width 3840 \
  --height 2160 \
  --fps 60 \
  --quality high
```

SuiteCut uses the public Playwright screencast API and streams frames to its own per-page encoder. This lets it finalize main pages, secondary pages, and popups even when a recorded page closes during the test.

## Write a recorded test

```ts
import { expect, test } from 'suitecut'

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

The fixture provides:

- `selectPage(page)` to choose the active page explicitly
- `narrate(text, options?)` for local Kokoro WASM or macOS speech and captions
- `checkpoint(label, options?)` for attached PNG evidence
- `highlight(locator, options?)` for callouts recorded in the browser
- `zoom(locator, options?)` for rendered camera crops
- `hover(locator, options?)` for an animated cursor move followed by a real Playwright hover
- `click(locator, options?)` for an animated cursor move, real click, ripple, and animation wait
- `scrollTo(locator, options?)` for native browser scrolling to an element
- `scrollTop(options?)` for native browser scrolling to the active page's top edge
- `hold(durationMs)` for presentation time

Pointer hover and click anchors are captured automatically in the main frame. Popups and secondary pages receive their own page ID, clock mapping, and WebM source.

## Capture timing

Every SuiteCut recording runs at presentation speed. There is no capture mode setting.

```ts
test.use({
  suitecutCapture: {
    framesPerSecond: 60,
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

`narrate()` synthesizes speech before it records the event. SuiteCut excludes synthesis time from the capture clock, then records the caption for the measured audio duration. Highlights, cursor travel, click ripples, native scrolling, holds, and application animations all happen inside Chromium while the screencast is running. `click()` waits for CSS and Web Animations created by the action, up to `animationTimeoutMs`.

The renderer trims the recorded page sources, joins page switches, applies requested camera crops, and mixes narration at each recorded event time. It does not add presentation holds or rebuild browser overlays.

`scrollTo()` and `scrollTop()` use Chromium's native scrolling and wait until movement settles. They default to smooth behavior. `scrollTo()` centers the target vertically and uses nearest horizontal alignment unless you override `behavior`, `block`, or `inline`.

Narration synthesis runs while frame ingestion is paused. Model startup time is omitted from the source video and the attempt clock. The page itself remains live during preparation, so call `narrate()` before the action whose animation must be recorded. Camera zoom remains a render-time crop because transforming the application document would change fixed and sticky layout.

## Local Kokoro narration

Kokoro runs inside SuiteCut's Node worker through `onnxruntime-web/wasm`. Inference stays local. The npm package includes the quantized Kokoro 82M model, tokenizer, and `af_heart` voice profile, so the default Kokoro voice works without a download or network request. These bundled files add about 89 MB to the installed package.

```ts
await suitecut.narrate('The generated report is ready.', {
  voice: 'af_heart',
  speed: 1,
  caption: 'The generated report is ready.',
})
```

SuiteCut supports the 28 English Kokoro voices used by Spek. `af_heart` ships with SuiteCut. Other voices download on first use and remain in the local cache. Common choices include `af_bella`, `af_sky`, `am_michael`, `bf_emma`, and `bm_george`. American voice IDs start with `a`; British voice IDs start with `b`.

Optional voices use `~/Library/Caches/SuiteCut/kokoro-82m-v1.0` on macOS and the standard XDG cache directory on Linux. Set `SUITECUT_MODEL_CACHE` to use another directory. Choose the native provider explicitly when needed:

```ts
await suitecut.narrate('Use the installed system voice.', {
  provider: 'macos-say',
  voice: 'Samantha',
  speed: 1,
})
```

Kokoro with `af_heart` is the default. Set `provider: 'macos-say'` only when a test should use an installed macOS system voice.

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
  --format webm
```

Render options include `--width`, `--height`, `--fps`, `--format`, `--quality`, and `--no-narration`. The default output is 3840x2160 at 60 frames per second with the `high` quality profile. Captions are already part of the browser recording.

The renderer accepts `standard`, `high`, and `master` quality profiles. They map to codec-specific
CRF, encoder speed, and audio bitrate settings. `high` uses H.264 CRF 18 with the slow preset.
`master` uses CRF 14 with the slower preset and produces much larger files. SuiteCut applies Lanczos
scaling with accurate rounding when a source does need resizing.

A 1600x900 source rendered at 3840x2160 is a 4K delivery file, but upscaling cannot recover detail
that Chromium did not record. For native 4K source detail, the Playwright viewport itself must be
3840x2160.

Programmatic rendering is also exported:

```ts
import { renderSuiteCut } from 'suitecut/render'

await renderSuiteCut({
  manifestPath: '.suitecut/latest-run.json',
  outputPath: '.suitecut/videos/report.mp4',
  config: {
    output: {
      width: 3840,
      height: 2160,
      framesPerSecond: 60,
      quality: 'high',
    },
  },
})
```

Each render writes a report beside the video as `<video-path>.suitecut.json`. It records the selected attempt, presentation edits, diagnostics, FFmpeg arguments, process status, and output duration.

## Included examples

- `examples/basic.spec.ts` records a minimal narrated page.
- `examples/checkpoint.spec.ts` checks viewport and full-page PNG capture.
- `examples/product-tour.spec.ts` exercises narration, captions, pointer capture, click effects, holds, highlights, zoom, and checkpoints.
- `examples/popup-tour.spec.ts` records main and popup pages, closes the popup, and switches the rendered source back to its opener.
- `examples/browser-presentation.spec.ts` verifies browser-rendered captions, overlays, and application transitions.
- `examples/flickks-launch.spec.ts` records a 1600x900 source and renders a 4K launch film at 60 frames per second.

Run the examples with:

```sh
npm run examples
```

Run the complete local quality gate with:

```sh
npm run check
```

The gate runs type-aware ESLint, strict TypeScript, unit tests, and the package build.

The local example scripts also provide `npm run example:record` and `npm run example:render`.

## Output model

The reporter writes a strict manifest containing:

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
- Chromium has full end-to-end evidence. Firefox and WebKit screencast timing remain release-hardening work.
- V1 mixes narration only. It does not capture or mix application audio.
