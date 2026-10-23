# SuiteCut

Record and stream narrated browser flows using native embedded Chromium capture.
SuiteCut saves source video and an action timeline, then renders a finished video.

Browser control is also available through a standalone [native C++ SDK and C ABI](native/client/README.md), which controls the SuiteCut CEF engine or installed Chrome/Chromium without Node.js. Its `suitecut-native-control` CLI also runs as an MCP server for agents. Browser runtimes remain separate distributions.

## Native browser and media tools

Requires Node.js 22+, pnpm 11, FFmpeg/FFprobe, and the separately built CEF
executable. See [build requirements and component setup](docs/building.md).
The native SDK builds independently of CEF; the embedded browser uses the pinned
CEF 156 build (Chromium 156 beta channel). Linux x64 and macOS ARM64 have separate qualification scopes.
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

## Packages

| Package                                                                         | What it is                                                                                                 |
| ------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| [`suitecut`](https://www.npmjs.com/package/suitecut)                            | Record, narrate, render and stream browser flows (this directory)                                          |
| [`@suitecut/browser-control`](packages/browser-control/README.md)               | Browser control for the native CEF engine, Chromium (CDP) and Firefox (WebDriver BiDi), with a live viewer |
| [`SuiteCut::Client`](native/client/README.md)                                   | C/C++ SDK with a stable C ABI and the `suitecut-native-control` CLI and MCP server                         |
| [Audio plugins](https://github.com/skyaara/suitecut/tree/audio-plugins/plugins) | Optional Sherpa text-to-speech voices, developed on the `audio-plugins` branch                             |

## Repository layout

| Path                        | Contents                                                                                            |
| --------------------------- | --------------------------------------------------------------------------------------------------- |
| `src/`                      | TypeScript SDK, CLI and test runner                                                                 |
| `native/`                   | CEF runtime, C/C++ client SDK, native test runner and shared helpers ([overview](native/README.md)) |
| `packages/browser-control/` | Build and package metadata for `@suitecut/browser-control`                                          |
| `site/`                     | Documentation website                                                                               |
| `docs/`                     | Guides, feature reference and engineering notes ([index](docs/README.md))                           |
| `examples/`                 | Runnable examples ([index](examples/README.md))                                                     |
| `tests/`                    | Unit tests and the real-CEF regression harness (`tests/cef`)                                        |
| `scripts/`                  | Build, packaging and verification scripts ([index](scripts/README.md))                              |
| `assets/`                   | Bundled Kokoro voice (Git LFS) and cursor images                                                    |

## Documentation

- [Documentation index](docs/README.md) and [setup and API guide](docs/guide.md)
- [Build requirements](docs/building.md) and [benchmarks](docs/benchmarks.md)
- [CEF runtime architecture](native/browser/ARCHITECTURE.md) and [validation status](native/browser/AUDIT.md)
- [Playwright behavior reference and feature checklist](docs/playwright-feature-parity.md)
- [Contributing](CONTRIBUTING.md), [security policy](SECURITY.md) and [changelog](CHANGELOG.md)

## Verification

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

## License

SuiteCut is [MIT licensed](LICENSE). Bundled Kokoro assets are Apache-2.0
([notice](assets/kokoro/NOTICE.md)); other third-party terms are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
