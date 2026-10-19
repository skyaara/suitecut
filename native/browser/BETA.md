# SuiteCut 2.0: native by default

SuiteCut 2.0 promotes the beta to the default native CEF backend. It requires the separately built
native executable described in [README.md](README.md). macOS ARM64 is the verified
platform. A missing executable is an error.

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
`capture: { audio: true }` to include page audio. Results include `backend`.

`defineSuiteCut({ native, capture, output, audioPlugins, signal })` creates a
native-default recorder with reusable settings. Its returned function accepts
per-recording overrides. Native dimensions and capture FPS belong in `native`;
recording `capture` accepts output `size`, output `framesPerSecond`,
`narrationTailMs`, and the boolean `audio` flag. Capture and output FPS accept
integers from 1 through 120. See
[frame rates and resampling](README.md#frame-rates-and-resampling).

Native callbacks receive `NativePage`. Strict main-document CSS locators and
authored presentation actions are supported. Role/text selectors, iframe
traversal, popup handling and popup capture are not implemented.

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
own the browser. `createBroadcast` resolves FFmpeg through the usual SuiteCut
configuration; `ffmpegPath` can override it. The lower-level APIs remain available
from `suitecut/native`. Stream audio is disabled by default and only subscribes to
CEF page audio when `stream.audio` is `true`.

## Build an installable prerelease

```sh
pnpm pack:beta
```

This produces `.suitecut/beta-packages/suitecut-2.1.0-beta.0.tgz`. Its **root import
`from 'suitecut'` resolves to the native-default API**. It also retains
`suitecut/beta`, `suitecut/native`, and `suitecut/render`. Install the tarball in a test project:

```sh
npm install /absolute/path/to/suitecut-2.1.0-beta.0.tgz
```

Packaging stages a prerelease manifest without changing the checkout's release
version or exports. The beta package's publish tag is `beta`. This command only
builds a local tarball; **it does not publish to npm**, install Chromium, or switch
an existing production stream. CEF SDKs and compiled native binaries are excluded.

## Verify native behavior

`pnpm test:native:cadence` verifies decoded resampling and audio through FFmpeg
without a browser. `pnpm test:native` additionally needs the separately built
CEF executable and runs real browser, recording and streaming checks.
