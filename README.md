# SuiteCut

Record and stream narrated browser flows using native embedded Chromium capture.
SuiteCut saves source video and an action timeline, then renders a finished video.

Browser control is also available through a standalone [native C++ SDK and C ABI](native/client/README.md), which controls the SuiteCut CEF engine or installed Chrome/Chromium without Node.js. A native MCP/JSON tool server exposes agent controls. Browser runtimes remain separate distributions.

## Native browser and media tools

Requires Node.js 22+, FFmpeg/FFprobe, and the separately built CEF executable.
See [native build instructions](native/browser/README.md#build). Linux x64 has
scoped live verification against the pinned CEF 156 beta. macOS ARM64 has
historical verification and Windows packaging is not implemented. The native
audit records remaining feature gaps and sanitizer startup failures.

```sh
pnpm install --frozen-lockfile
pnpm build
export SUITECUT_NATIVE_EXECUTABLE="/absolute/path/to/suitecut-browser"
node dist/cli.js doctor
```

For installed packages, `suitecut install` downloads checksum-verified media tools.
System FFmpeg/FFprobe or explicit `SUITECUT_FFMPEG_PATH` and
`SUITECUT_FFPROBE_PATH` can also be used.

## Record and render

```ts
import { record, renderSuiteCut } from 'suitecut'

const recording = await record('Product tour', async ({ page, suitecut }) => {
  await page.goto('https://example.com')
  await suitecut.highlight(page.locator('h1'))
  await suitecut.narrate('Welcome to the product tour.')
  await suitecut.checkpoint('Opening screen')
}, {
  native: { width: 1920, height: 1080, framesPerSecond: 120 },
  capture: { framesPerSecond: 60, audio: true },
})

await renderSuiteCut({
  manifestPath: recording.manifestPath,
  outputPath: '.suitecut/product-tour.mp4',
  config: { output: { framesPerSecond: 60 } },
})
```

Capture and output FPS accept integers from 1 through 120. Timestamp-based
resampling holds or drops frames; it does not generate new motion. Actual browser
capture throughput depends on page activity and host capacity. Page audio is off
by default. Native controls currently support main-document strict CSS locators,
JSON evaluation and basic Chromium input.

## Stream

`launchBrowser` and `createBroadcast` publish native pixels and optional page audio
to RTMP/RTMPS. Broadcast source selection preserves the publisher connection.
See [the native guide](native/browser/README.md) for source ownership, recording,
streaming, IPC and failure behavior.

## Verification and documentation

- [Setup and API guide](docs/guide.md)
- [Native validation status](native/browser/AUDIT.md)
- [Playwright behavior reference and feature-matching checklist](docs/playwright-feature-parity.md)
- [Audio plugin packages](plugins/)
- [Native example](examples/native.ts)
- [Contributing](CONTRIBUTING.md)

`pnpm test:native:cadence` decodes actual FFmpeg video/audio from timestamped test
inputs, including 120 FPS output. `pnpm test:native` additionally requires the CEF
binary and verifies real browser, recording and streaming behavior.

SuiteCut owns its native capture and media pipeline. Automation orchestration and
recording/publishing currently live in TypeScript; expanding C++ ownership is
ongoing. GPU texture sharing and native hardware encoding are not implemented.
