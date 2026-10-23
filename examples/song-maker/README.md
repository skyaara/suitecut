# Real application: Song Maker at 1080p60 with page audio

This is a complete browser script against the live
[Chrome Music Lab Song Maker](https://musiclab.chromeexperiments.com/Song-Maker/)
application. It composes a melody and percussion with real pointer input,
changes the tempo, plays the song, switches instruments, undoes an edit and
plays a second arrangement. It records the real application and its Web Audio
output, then renders and verifies a 1920×1080, 60 FPS MP4 with audio.

Browser actions, rendering and page-audio capture use the SuiteCut CEF engine.
No installed Chrome, Playwright or browser driver is used. FFmpeg encodes the
recording and renders the finished video; FFprobe and FFmpeg verify the result.
The script does not inject an app, synthesize replacement audio, modify app
state through evaluation, or save/publish the song to the website.

## Packages used

| Package                     | Role                                                                         |
| --------------------------- | ---------------------------------------------------------------------------- |
| `@suitecut/browser-control` | Launch and own the actual CEF browser                                        |
| `suitecut/native`           | Record that browser through `recordNative()` and its page/locator/input APIs |
| `suitecut`                  | Render the manifest into the finished MP4                                    |
| `@tonaljs/note`             | Convert the authored musical notes into grid positions                       |
| `dotenv`                    | Read optional local environment settings                                     |
| `zod`                       | Validate configuration, geometry and media inspection output                 |

The example's third-party dependency versions are pinned in `package.json`.
The helper installs both SuiteCut packages from freshly built tarballs into a
new consumer outside the repository, installs the external packages from npm,
and retains the resulting `package-lock.json`. It uses package exports, not
workspace links or imports from `src`/`dist`. This also exercises browser-control
and recording-package interoperability through `browser.source`.

## Build the engine

Requirements: Node.js 22+, the repository's pinned pnpm, Python 3.11+, CMake,
a C++20 toolchain, the exact platform CEF SDK and pinned libyuv source, and
FFmpeg/FFprobe with VP9, Opus, H.264 and AAC support. Runtime dependencies and
supported platforms are listed in [build requirements](../../docs/building.md).
Install the package manager dependencies first:

```sh
pnpm install --frozen-lockfile
```

Follow the [CEF build recipe](../../native/browser/README.md#build) to fetch
the pinned CEF/libyuv sources and install their dependencies. On macOS ARM64,
use the [macOS recipe](../../docs/native-full-runtime-macos.md). For example,
with matching sources already present and Homebrew dependencies installed:

```sh
export CEF_ROOT=/absolute/path/to/the/pinned/macosarm64/cef-sdk
export LIBYUV_ROOT=/absolute/path/to/the/pinned/libyuv/source
cmake -S native/browser -B build/song-maker-engine -G Ninja \
  -DCEF_ROOT="$CEF_ROOT" -DLIBYUV_ROOT="$LIBYUV_ROOT" \
  -DPROJECT_ARCH=arm64 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DCURL_INCLUDE_DIR=/opt/homebrew/opt/curl/include \
  -DCURL_LIBRARY_RELEASE=/opt/homebrew/opt/curl/lib/libcurl.dylib \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@4 \
  -DCMAKE_CXX_FLAGS=-D_LIBCPP_ENABLE_EXPERIMENTAL
cmake --build build/song-maker-engine --target suitecut-browser --parallel 1
export SUITECUT_NATIVE_EXECUTABLE="$PWD/build/song-maker-engine/suitecut-browser.app/Contents/MacOS/suitecut-browser"
export SUITECUT_FFMPEG_PATH="$(command -v ffmpeg)"
export SUITECUT_FFPROBE_PATH="$(command -v ffprobe)"
```

Use the installed OpenSSL prefix appropriate to your machine; curl must use
the same OpenSSL major for the full API extra-CA feature. Keep the entire `.app`
together. The existing fixed signing policy is reused. Local signing does not
notarize the app or qualify Homebrew dylibs for older macOS releases.

On Linux use the matching Linux CEF SDK, `PROJECT_ARCH=x86_64` and the Linux
dependency recipe. The executable is `build/song-maker-engine/suitecut-browser`.
The script defaults to headless CEF; set `SUITECUT_EXAMPLE_HEADLESS=0` to use a
display. This example requires network access to the real app and its assets.

## Build the SDKs and run the isolated example

From the repository root, after setting the executable and media-tool paths:

```sh
pnpm example:song-maker
```

This command builds both JavaScript SDK packages, packs them, creates a fresh
temporary consumer, installs those exact tarballs plus the external packages,
and executes `run.mjs`. It prints the consumer and evidence directories. A new
output directory is used on every run; existing evidence is never overwritten.
To choose the evidence directory explicitly:

```sh
python3 scripts/run-song-maker-example.py --output "$PWD/.suitecut/my-song-maker-run"
```

Only one browser and one recording run at a time. The script has a six-minute
cancellation budget and closes its owned browser in `finally`. The two ten-second
holds are intentional recording time while the real song plays; UI readiness
uses locators and assertions. Allow disk space for the SDK tarballs, consumer
installation, source recording and rendered MP4. The external consumer and its
lockfile remain available for inspection; the command prints their exact path.

## Run it in your own project

The helper's output directory contains `suitecut-2.0.0.tgz` and
`suitecut-browser-control-2.0.0.tgz`. These packages need not be published to npm.
Copy this example into a new directory, then install the archives:

```sh
mkdir my-song-maker
cp examples/song-maker/package.json examples/song-maker/run.mjs \
  examples/song-maker/verify-media.mjs examples/song-maker/.env.example my-song-maker/
cd my-song-maker
npm install --save-exact /absolute/path/to/suitecut-2.0.0.tgz \
  /absolute/path/to/suitecut-browser-control-2.0.0.tgz
cp .env.example .env
# Edit .env to point to your built engine and media tools.
npm start
```

`run.mjs` is ordinary JavaScript with `async`/`await`; no custom script format
or transpiler is needed. Change the melody, selectors, actions and assertions
to build another flow. The SDKs are separate from the browser runtime.

## Outputs and verification

The run produces:

- `song-maker-1080p60.mp4`: the final video with real page audio.
- `manifest.json` and `recording/`: source video, action timeline and three checkpoints.
- `composition.png` and `playing.png`: screenshots from the real app.
- `imports.json`: where the installed SDKs and external packages resolved.
- `report.json`: actions, dependency versions, native frame delivery during both playback windows, decoded media checks and any failure.
- `native-stderr.log`: bounded native diagnostics.
- `setup.json`, SDK tarballs and the temporary consumer's `package-lock.json`: installation provenance.

The script fails on a browser action/assertion, recording, shutdown, render or
verification error. It decodes all video frames to check frame count/cadence,
requires 1080p at `60/1`, and decodes audio to check RMS, peak and at least eight
seconds of audible content. An audio track containing silence does not pass.
Encoded 60 FPS does not establish 60 distinct source paints every second; this
is an application/recording example, not a sustained-load capture benchmark.
The report separately measures native frame delivery during the two performances
and records the native drop counter. Shutdown requires actual process exit code
zero with no signal; an acknowledged close or forced termination cannot pass.

Song Maker is an external service: its markup, availability and assets can
change. Failures are retained rather than replaced with a local imitation or
retried into a passing result. The canvas geometry assumes its default 32-column,
14-note-row composition and a fresh temporary profile. No CI job is added.
