# Contributing to SuiteCut

SuiteCut owns its Playwright capture, timing, manifest, and rendering code. Keep changes inside those
boundaries. Do not add hosted speech services or `playwright-recast`.

## Set up the repository

You need Node.js 22, pnpm 11, FFmpeg, FFprobe, and the Playwright browsers used by the tests.

```sh
git clone https://github.com/skyaara/suitecut.git
cd suitecut
pnpm install --frozen-lockfile
pnpm exec playwright install chromium firefox webkit
pnpm check
```

On macOS, `brew install ffmpeg` provides FFmpeg and FFprobe. On Linux, use your distribution's
FFmpeg package.

## Make a change

- Keep TypeScript strict. Validate JSON-shaped runtime input with Zod.
- Use NodeNext ESM imports with emitted `.js` relative specifiers.
- Add or update a focused test for behavior changes.
- Keep fixture methods small and Playwright-native. Recording lifecycle and FFmpeg process cleanup
  stay inside SuiteCut.
- Do not commit `.suitecut`, `dist`, Playwright results, or generated video files.
- Do not replace the bundled Kokoro files without updating `assets/kokoro/NOTICE.md`, including the
  pinned upstream revision and SHA-256 hashes.

The durable public entries are `suitecut`, `suitecut/test`, `suitecut/audio-plugin`, `suitecut/playwright`,
`suitecut/reporter`, `suitecut/render`, and `suitecut/types`. Treat changes to their exports, option
schemas, manifest fields, or CLI flags as public API changes.

Keep optional model runtimes in `plugins/`. Audio plugins run in the narration worker and must write
mono PCM WAV to the requested output path. Do not add their model runtime dependencies to the core
`suitecut` package.

## Run the checks

For most changes:

```sh
pnpm check
pnpm site:check
pnpm site:build
```

For recording, page lifecycle, timing, or reporter work:

```sh
pnpm test:stream
pnpm test:browsers
pnpm test:retry-parallel
```

For Kokoro inference changes:

```sh
pnpm test:kokoro
```

For the external audio plugin contract or Sherpa family adapters:

```sh
pnpm test:audio-plugin
pnpm test:sherpa
```

Set `SUITECUT_SHERPA_VITS_MODEL_DIR` to run the Sherpa-ONNX integration test against a local Piper
or VITS model. The plugin never downloads model files.

For package metadata, exports, bundled assets, or release work:

```sh
pnpm test:package
```

`test:package` builds the project once, packs with lifecycle scripts disabled, inspects every npm
tarball, installs the core and two representative audio packages in a temporary consumer project,
imports every public entry, and runs the installed CLI help command.

## Open a pull request

Explain the observed problem, the chosen fix, and the commands you ran. Include a small reproduction
for bugs. Keep unrelated cleanup out of the same pull request. A pull request must pass CI before it
is ready to merge.

By submitting a contribution, you agree that the project may distribute it under the MIT license.
