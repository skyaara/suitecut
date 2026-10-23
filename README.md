# SuiteCut

Record and stream narrated browser flows using native embedded Chromium capture.
SuiteCut saves source video and an action timeline, then renders a finished video.

Browser control is also available through a standalone [native C++ SDK and C ABI](native/client/README.md), which controls the SuiteCut CEF engine or installed Chrome/Chromium without Node.js. A native MCP/JSON tool server exposes agent controls. Browser runtimes remain separate distributions.

## Native browser and media tools

Requires Node.js 22+, pnpm 11, FFmpeg/FFprobe, and the separately built CEF
executable. See [build requirements and component setup](docs/building.md).
The native SDK builds independently of CEF; the embedded browser uses the pinned
CEF 156 beta. Linux x64 and macOS ARM64 have separate qualification scopes.
Windows runtime packaging is not implemented.

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

const recording = await record(
  'Product tour',
  async ({ page, suitecut }) => {
    await page.goto('https://example.com')
    await suitecut.highlight(page.locator('h1'))
    await suitecut.narrate('Welcome to the product tour.')
    await suitecut.checkpoint('Opening screen')
  },
  {
    native: { width: 1920, height: 1080, framesPerSecond: 120 },
    capture: { framesPerSecond: 60, audio: true },
  },
)

await renderSuiteCut({
  manifestPath: recording.manifestPath,
  outputPath: '.suitecut/product-tour.mp4',
  config: { output: { framesPerSecond: 60 } },
})
```

Capture and output FPS accept integers from 1 through 120. Timestamp-based
resampling holds or drops frames; it does not generate new motion. Actual browser
capture throughput depends on page activity and host capacity. Page audio is off
by default. Browser control includes locators, frames, input, network interception,
artifacts and lifecycle operations; see the
[feature checklist](docs/playwright-feature-parity.md) for backend limits.

## Stream

`launchBrowser` and `createBroadcast` publish native pixels and optional page audio
to RTMP/RTMPS. Broadcast source selection preserves the publisher connection.
See [the native guide](native/browser/README.md) for source ownership, recording,
streaming, IPC and failure behavior.

## Verification and documentation

- [Setup and API guide](docs/guide.md)
- [Native validation status](native/browser/AUDIT.md)
- [Native component and SDK layout](native/README.md)
- [CEF runtime architecture](native/browser/ARCHITECTURE.md)
- [Real-browser regression suite](docs/cef-regressions.md)
- [macOS runtime packaging and fixed signing](docs/native-full-runtime-macos.md)
- [Playwright behavior reference and feature-matching checklist](docs/playwright-feature-parity.md)
- [Audio plugin packages](plugins/)
- [Native example](examples/native.ts)
- [Full real-app example: Song Maker with 1080p60 video and page audio](examples/song-maker/README.md)
- [Contributing](CONTRIBUTING.md)

`pnpm test:native:cadence` decodes actual FFmpeg video/audio from timestamped test
inputs, including 120 FPS output. `pnpm test:native` additionally requires the CEF
binary and verifies real browser, recording and streaming behavior.

The October 21 macOS ARM64 qualification passed all 90 full CEF regression cases,
with one regression worker, and all 17 standalone native SDK checks. The runtime
split also passed 23 translation-unit checks without forced precompiled headers.
These are local results on macOS 27.0.1, not qualification for older macOS or
Intel hardware. The CEF suite is available locally and has not been added to CI.

SuiteCut owns its native capture and media pipeline. The macOS runtime includes
GPU pixel conversion and VideoToolbox H.264 encoding; the C++ SDK also exposes
native recording tools. TypeScript provides narration and finished-video
composition. See the platform recipes for supported paths and measured limits.
