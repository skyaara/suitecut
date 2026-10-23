# Contributing to SuiteCut

SuiteCut owns its CEF capture, private transport, timing, narration, manifests,
recording and publishing. Preserve source ownership, failure propagation and
sandboxing when changing these boundaries.

## Setup and checks

See [component build requirements](docs/building.md) for the JavaScript SDK,
standalone C++ SDK, macOS packaging and local CEF regression suite.
Install [Git LFS](https://git-lfs.com) before cloning: the bundled Kokoro model
(`assets/kokoro/model_quantized.onnx`) is stored in LFS, and `pnpm pack` refuses a
pointer file. In an existing clone run `git lfs install` and `git lfs pull`.
Use Node.js 22 or newer, pnpm 11, FFmpeg and FFprobe. Real browser checks require the pinned
CEF executable described in [native/browser/README.md](native/browser/README.md).

```sh
pnpm install --frozen-lockfile
pnpm build
pnpm typecheck
pnpm exec vitest run --maxWorkers=2
pnpm test:native:cadence
pnpm site:check
pnpm site:build
```

Run `pnpm test:native` for browser/capture/lifecycle changes on a supported CEF
host. Report missing runtime evidence as blocked, not passing. Use native CTest
checks for frame-rate parsing, audio-clock and libyuv pixel changes.
Run `pnpm test:cef:unit` for harness changes and the
[full CEF suite](docs/cef-regressions.md) for runtime qualification. Keep compiler
jobs and browser workers bounded; default to one browser worker on a small Mac.
The CEF suite is not enabled in CI.

## Code and package boundaries

- Keep TypeScript strict and validate runtime JSON input with Zod.
- Use NodeNext ESM imports with emitted `.js` relative specifiers.
- Keep renderer DOM logic independent of host orchestration.
- Preserve bounded capture queues, capture timestamps and observable failures.
- Keep model runtimes in `plugins/` and retain upstream license notices.
- Do not commit `.suitecut`, `dist`, browser profiles or generated media.
- Do not replace Kokoro assets without updating their pin and notices.

Public entries are `suitecut`, `suitecut/native`, `suitecut/beta`,
`suitecut/audio-plugin`, `suitecut/render` and `suitecut/types`. Treat export,
option, manifest and CLI changes as public API changes. Audio plugins run in the
narration worker and write mono PCM WAV to the requested path.

Run `pnpm test:package` for package changes, `pnpm test:audio-plugin` for audio
contracts and `pnpm test:sherpa` with an explicit local model when exercising
Sherpa integrations. These checks do not publish anything.

## Review

Describe the concrete problem, resulting behavior, validation and remaining
limitations. Preserve unrelated work. Publishing, pushing, deployment and release
require explicit authorization. Contributions retain the repository MIT license.
