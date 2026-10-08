# SuiteCut

Record and stream narrated browser flows for product demos, tutorials, and automated walkthroughs.
SuiteCut saves browser video and a timeline of your actions, then renders them into a finished video.

[Documentation](https://suitecut.aakashreddy.com/docs/) · [Releases](https://github.com/skyaara/suitecut/releases) · [Changelog](CHANGELOG.md)

## What it does

- **Recording:** embedded Chromium (CEF) capture, with an explicit Playwright backend for existing scripts and tests.
- **Presentation:** narration, captions, highlights, zoom, pointer effects, and checkpoint clips.
- **Audio:** local Kokoro narration, optional model plugins, and opt-in native page audio.
- **Delivery:** offline rendering from saved manifests and live RTMP/RTMPS streaming.

## Installation

Requires Node.js 22 or newer. Install the [2.0 release package](https://github.com/skyaara/suitecut/releases/tag/v2.0.0)
and set up FFmpeg and FFprobe:

```sh
npm install https://github.com/skyaara/suitecut/releases/download/v2.0.0/suitecut-2.0.0.tgz
npx suitecut install
npx suitecut doctor
```

`suitecut install` downloads checksum-verified media tools. You can also use system tools
or set `SUITECUT_FFMPEG_PATH` and `SUITECUT_FFPROBE_PATH` explicitly.

### Native browser

SuiteCut 2.0 uses CEF by default. The package includes its source and build pins;
the browser executable must be built separately. Follow the [native build instructions](native/browser/README.md#build),
then set its path:

```sh
export SUITECUT_NATIVE_EXECUTABLE="/absolute/path/to/suitecut-browser.app/Contents/MacOS/suitecut-browser"
```

macOS Apple Silicon is the verified native platform. Linux native builds are unverified;
Windows native packaging is not implemented. Native capture defaults to 1920 × 1080 at 60 FPS.

## Record and render

Save this as `tour.mjs` and run `node tour.mjs`:

```js
import { record, renderSuiteCut } from 'suitecut'

const recording = await record(
  'Product tour',
  async ({ page, suitecut }) => {
    await page.goto('https://example.com')
    await suitecut.highlight(page.locator('h1'))
    await suitecut.narrate('Welcome to the product tour.')
    await suitecut.checkpoint('Opening screen')
  },
  { capture: { audio: true } },
)

await renderSuiteCut({
  manifestPath: recording.manifestPath,
  outputPath: '.suitecut/tour.mp4',
})
```

The recorder launches and closes its primary browser. Recording artifacts and the manifest
are saved under `.suitecut/`; the renderer uses those files to produce the final video.
Page audio is off by default and enabled in this example. Narration is mixed during rendering.

## Use with Playwright

Choose `suitecut/playwright` for full Playwright locators, iframe traversal, popup recording,
or existing browser automation. Chromium, Firefox, and WebKit are supported.

```sh
npm install playwright
npx playwright install chromium
```

The recording entry points are available from the explicit backend:

```js
import { record, defineSuiteCut } from 'suitecut/playwright'
```

For Playwright Test, install `@playwright/test`, import tests from `suitecut/test`, and
configure `suitecut/reporter` to save the manifest. See the [setup guide](docs/guide.md#add-suitecut-to-playwright-test)
and [standalone examples](examples/playwright/).

## Migrating to 2.0

The root `suitecut` import now selects the native backend. Existing Playwright flows can
keep their behavior by changing their recording imports:

| Existing usage                                         | In 2.0                                             |
| ------------------------------------------------------ | -------------------------------------------------- |
| Playwright `record` / `defineSuiteCut` from `suitecut` | Import from `suitecut/playwright`                  |
| Native imports from `suitecut/beta`                    | Import from `suitecut`; the beta alias still works |
| `suitecut/test` and `suitecut/reporter`                | Keep the existing imports                          |

- **Backend selection:** root `record` also accepts `backend: 'playwright'`; `suitecut/stable` remains a Playwright alias.
- **Capture settings:** native dimensions, device scale, and FPS belong in `native`. Native `capture` accepts output `size`, `narrationTailMs`, and boolean `audio`.
- **Locators:** native recording supports strict main-document CSS selectors. Keep role/text selectors, iframe and popup flows, and custom Playwright setup on `suitecut/playwright`.
- **Audio:** replace `stream.audio: 'tab'` with `true` and `'silent'` with `false`. Native recording audio uses `capture.audio: true`.
- **Types and output:** import Playwright recording types from `suitecut/playwright`. Manifests, rendering, and audio plugin APIs retain their behavior.

See the [native migration guide](native/browser/BETA.md) for backend configuration and streaming examples.

## Documentation and benchmarks

- [Full guide](docs/guide.md): capture settings, presentation actions, streaming, narration, plugins, and rendering.
- [Native browser](native/browser/README.md): CEF builds, platform requirements, and native APIs.
- [Examples](examples/): standalone flows and Playwright tests.
- [Benchmark methodology](benchmarks/README.md): FLV framing, FFprobe batching, and native versus Playwright capture.

Run the built-in benchmarks from a source checkout:

```sh
pnpm benchmark:flv
pnpm benchmark:media
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for setup and local checks, and follow the
[code of conduct](CODE_OF_CONDUCT.md). Report bugs in [GitHub Issues](https://github.com/skyaara/suitecut/issues)
and vulnerabilities privately using [SECURITY.md](SECURITY.md).

## License

[MIT](LICENSE). Bundled Kokoro assets use Apache-2.0; see the
[third-party notices](THIRD_PARTY_NOTICES.md).
