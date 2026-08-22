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
  reporter: [
    ['line'],
    ['suitecut/reporter', { outputFile: '.suitecut/latest-run.json' }],
  ],
  use: {
    trace: 'on',
    video: 'off',
  },
})
```

SuiteCut uses the public Playwright screencast API and streams frames to its own per-page encoder. This lets it finalize main pages, secondary pages, and popups even when a recorded page closes during the test.

## Write a recorded test

```ts
import { expect, test } from 'suitecut'

test('creates a report', async ({ page, suitecut }) => {
  await page.goto('/reports')
  suitecut.narrate('The reports workspace is open.', {
    provider: 'kokoro',
    voice: 'af_heart',
  })

  const createButton = page.getByRole('button', { name: 'Create report' })
  await suitecut.highlight(createButton, { borderColor: '#2DD4BF' })
  await createButton.hover()
  await createButton.click()

  suitecut.hold(650)
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
- `highlight(locator, options?)` for rendered callouts
- `zoom(locator, options?)` for rendered camera crops
- `hold(durationMs)` for presentation time without slowing the test

Pointer hover and click anchors are captured automatically in the main frame. Popups and secondary pages receive their own page ID, clock mapping, and WebM source.

Narrations are scheduled sequentially. Each one inserts a presentation-only hold on its active page for the measured audio duration plus the configured tail, so speech and captions never overlap. `narrate()` does not insert sleeps into the test body. Fixture teardown waits for queued synthesis, but SuiteCut stops page recording first so model latency never becomes idle video.

## Local Kokoro narration

Kokoro runs inside SuiteCut's Node worker through `onnxruntime-web/wasm`. Inference stays local. The first use downloads the quantized Kokoro 82M model, tokenizer, and selected voice profile from the model repository, then caches them for later tests. The current `af_heart` cache uses about 89 MB on disk.

```ts
suitecut.narrate('The generated report is ready.', {
  provider: 'kokoro',
  voice: 'af_heart',
  speed: 1,
  caption: 'The generated report is ready.',
})
```

SuiteCut supports the 28 English Kokoro voices used by Spek. Common choices include `af_heart`, `af_bella`, `af_sky`, `am_michael`, `bf_emma`, and `bm_george`. American voice IDs start with `a`; British voice IDs start with `b`.

The default cache is `~/Library/Caches/SuiteCut/kokoro-82m-v1.0` on macOS and the standard XDG cache directory on Linux. Set `SUITECUT_MODEL_CACHE` to use another directory. Choose the native provider explicitly when needed:

```ts
suitecut.narrate('Use the installed system voice.', {
  provider: 'macos-say',
  voice: 'Samantha',
  speed: 1,
})
```

`macos-say` remains the default for existing tests. Set `provider: 'kokoro'` to use the WASM model.

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

Render options include `--width`, `--height`, `--fps`, `--format`, `--captions`, and `--no-narration`. Captions are enabled by default. SuiteCut renders caption cards to transparent PNGs with local Chromium, so it does not depend on FFmpeg font filters or machine-specific subtitle fonts.

Programmatic rendering is also exported:

```ts
import { renderSuiteCut } from 'suitecut/render'

await renderSuiteCut({
  manifestPath: '.suitecut/latest-run.json',
  outputPath: '.suitecut/videos/report.mp4',
  config: {
    output: { width: 1280, height: 720, framesPerSecond: 30 },
    narrationTailMs: 350,
  },
})
```

Each render writes a report beside the video as `<video-path>.suitecut.json`. It records the selected attempt, presentation edits, diagnostics, FFmpeg arguments, process status, and output duration.

## Included examples

- `examples/basic.spec.ts` records a minimal narrated page.
- `examples/checkpoint.spec.ts` checks viewport and full-page PNG capture.
- `examples/product-tour.spec.ts` exercises narration, captions, pointer capture, click effects, holds, highlights, zoom, and checkpoints.
- `examples/popup-tour.spec.ts` records main and popup pages, closes the popup, and switches the rendered source back to its opener.

Run all four examples with:

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

- Kokoro inference is local, but the first use needs network access to populate the model cache.
- The quantized Kokoro model uses one WASM thread in the current Node worker. Multi-worker inference needs memory and throughput measurements before it becomes a default.
- Pointer geometry covers the main frame. Cross-origin iframe coordinate translation still needs browser-specific work.
- Chromium has full end-to-end evidence. Firefox and WebKit screencast timing remain release-hardening work.
- V1 mixes narration only. It does not capture or mix application audio.
