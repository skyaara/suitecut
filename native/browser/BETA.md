# SuiteCut 2.1: native by default

SuiteCut 2.1 keeps the native CEF backend as the default and moves media processing
into C++. It requires the separately built native executable described in
[ENGINE.md](ENGINE.md). Linux x86-64 is the verified target for this engine;
macOS integration remains unverified. A missing executable is an error; SuiteCut never silently substitutes
Playwright and reports it as native.

## Record with the embedded browser

Import from `suitecut`. Existing `suitecut/beta` imports remain compatible:

```ts
import { record, renderSuiteCut } from 'suitecut'

const result = await record('Native tour', async ({ page, suitecut }) => {
  await page.goto('https://example.com')
  await suitecut.highlight(page.locator('h1'))
  await suitecut.checkpoint('Opening screen')
})

await renderSuiteCut({
  manifestPath: result.manifestPath,
  outputPath: '.suitecut/native-tour.mp4',
})
```

Set `SUITECUT_NATIVE_EXECUTABLE` to the absolute CEF executable path first, or pass
`native: { executablePath: '/absolute/path/to/suitecut-browser' }` in the recording
options. Defaults are **native, 1920x1080, 60 FPS, I420**. The recorder launches and
closes its primary browser, including on callback failure. Secondary sources added
with `addSource` remain caller-owned. Recording audio is disabled by default; set
`capture: { audio: true }` to include page audio. Audio-enabled recording and
streaming wait for native encoders, muxer, and the first captured video frame.
Silent pages can start with audio enabled; the engine emits zero PCM until page
audio arrives. This readiness does not guarantee that stock CEF has started its
audibility-triggered capture or preserved the beginning of the first sound. See
[ENGINE.md](ENGINE.md) for mandatory readiness and timeout behavior.
Results include `backend`.

`defineSuiteCut({ native, capture, output, audioPlugins, signal })` creates a
native-default recorder with reusable settings. Its returned function accepts
per-recording overrides. Native dimensions/FPS belong in `native`; recording
`capture` accepts output `size`, `narrationTailMs`, and the boolean `audio` flag.

Choose the old implementation explicitly for comparison or compatibility:

```ts
const baseline = await record('Playwright baseline', async ({ page, suitecut }) => {
  await page.goto('https://example.com')
  await suitecut.checkpoint('Opening screen')
}, { backend: 'playwright', capture: { framesPerSecond: 60 } })
```

Native callbacks receive `NativePage`, not the complete Playwright `Page` API.
Strict main-document CSS locators and the existing authored presentation actions
are supported. Full Playwright selectors, fixtures, popup handling, setup
extensions, and popup capture are not native equivalents.
Use the real Playwright adapter in `suitecut/engine` for CEF page/locator APIs,
or `suitecut/playwright` for the legacy capture backend.
The root API changed in 2.0; `suitecut/playwright` and `suitecut/test` keep their existing backends.

## Native-default streaming

```ts
import { launchBrowser, createBroadcast } from 'suitecut'

const source = await launchBrowser()
let broadcast
try {
  await source.navigate('https://example.com')
  broadcast = await createBroadcast({
    source,
    stream: { url: process.env.RTMP_URL!, audio: true },
  })
  await broadcast.ready()
  await Promise.race([source.failure, broadcast.failure, applicationStopSignal])
} finally {
  await broadcast?.stop()
  await source.close()
}
```

`applicationStopSignal` is an application-owned promise. The broadcast does not
own the browser. `createBroadcast` uses the native linked FFmpeg engine; its
compatibility `ffmpegPath` option is not executed. The lower-level APIs remain available
from `suitecut/native`. Stream audio is disabled by default and only subscribes to
CEF page audio when `stream.audio` is `true`.

## Build the 2.1 release packages

```sh
pnpm pack:release
```

This produces `.suitecut/release/suitecut-2.1.0.tgz`, eight matching audio packages,
a complete package inventory and `SHA256SUMS`. Its root import resolves to the
native-default API; `suitecut/beta` remains its compatibility alias and
`suitecut/playwright` / `suitecut/stable` retain the legacy backend.

```sh
npm install /absolute/path/to/suitecut-2.1.0.tgz
```

This only creates local artifacts. It does not publish to npm or bundle a compiled
CEF browser. See [the release guide](../../RELEASE.md) for verification and known
quality limits. The separate `pnpm pack:beta` helper derives the next minor beta
from the checkout version; from 2.1.0 it creates 2.2.0-beta.0.

## Compare recording performance

```sh
export SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser.app/Contents/MacOS/suitecut-browser"
pnpm benchmark:beta --duration-ms 10000 --trials 3 --width 1920 --height 1080 --fps 60
```

The harness runs the actual beta API and explicit Playwright fallback on the same
local animated page. It excludes one warmup run per backend, alternates the order
of paired trials, and saves source clips, manifests, individual results, host and
browser versions, JSON, and a Markdown summary under `.suitecut/beta-benchmark/`.
Use `--output /absolute/directory` for a chosen destination. A failed run fails the
command; it is never relabeled as the other backend.

The primary measure is **distinct frames decoded from the video**, using a binary
frame counter drawn by the page. A file labeled 60 FPS can still contain repeated
frames. The report also shows repeated-frame percentage, longest held frame,
startup/finalization time, file size, and sampled process-tree RSS/CPU.

Interpret these as comparisons of the current complete recording pipelines:
CEF/I420 and Playwright/MJPEG use their respective bundled Chromium builds; the
VP9 encoders both use CRF 18 but currently have different thread budgets. Process
sampling covers worker startup through output verification; summed RSS can count
shared pages more than once, short-lived processes can escape sampling, and GPU
memory is not measured. Startup timing begins after module imports. Close unrelated
heavy workloads and retain the exact report when comparing changes.

This is a short synthetic **recording** benchmark. It does not prove streaming
latency, audio sync, visual fidelity, site compatibility, or 24-hour stability.
`pnpm test:native:stream` separately exercises native RTMP output, audio sync,
source replacement, and receiver reconnection. See [AUDIT.md](AUDIT.md) for the
remaining platform and release gates.
