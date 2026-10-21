# Native full runtime

For macOS ARM64 builds and qualification, see
[the macOS runtime recipe](native-full-runtime-macos.md).

`SuiteCutRuntime-0.1.0-linux-x86_64.tar.gz` combines the native C++ SDK,
C ABI, agent CLI, native recorder, SuiteCut engine, pinned CEF 156 beta,
CEF resources/locales/notices, and pinned FFmpeg/ffprobe in one installation.
The smaller SDK archives remain available for applications using existing browsers.
No Node.js process or dependency download is required to use this bundle.

Extract the archive into a directory owned by the application user. All component
paths are relative to the extracted installation; moving that installation does
not require rebuilding it. Normal Linux system libraries are still required.
The manifest records ELF dependencies on the packaging host. Current qualification
is Linux x86_64 with glibc 2.38+ and GLIBCXX 3.4.32+; Windows/macOS are separate work.

## Use

```sh
./bin/suitecut --session demo open https://example.com
./bin/suitecut --session demo snapshot
./bin/suitecut --session demo close

./bin/suitecut-native-record --url https://example.com \
  --output /tmp/example.webm --duration-ms 5000 --fps 29.97 --audio
```

`bin/suitecut` selects the included engine/CEF by default. The original
`bin/suitecut-native-control` remains available for explicit backend configuration.
The recorder resolves bundled engine and media executables from its own location.
Recording output must be a new file in an existing directory. CEF records WebM
with optional page audio; the page must actually play audio for captured content.
Audio autoplay remains subject to browser policy. Fractional output FPS in 1–120
is resampled from the source capture cadence; it does not guarantee that many
distinct browser frames. Default capture dimensions are 1280×720 and must be even.

External Chromium recording uses the same native executable, with a caller-supplied
browser, and currently produces silent MP4/WebM:

```sh
./bin/suitecut-native-record --backend chromium --browser /usr/bin/chromium \
  --url https://example.com --output /tmp/chrome.mp4 --duration-ms 5000
```

The C++ Chromium recording helper accepts `ffmpegPath` and `ffprobePath` in its
options object. When omitted it retains `/usr/bin/ffmpeg` and `/usr/bin/ffprobe`.
CEF uses the existing `Page::command("recording", ...)` protocol, with explicit
`ffmpegPath`, `outputPath`, dimensions, cadence, audio flag and returned recording
ID. The native recorder is a complete example of both paths. Firefox recording
is not supplied. The full Node.js SuiteCut narration/editing workflow is not part
of this native browser runtime.

## Build the archive

Build/install `native/client` normally, then run the packager with explicit
inputs. It never downloads dependencies or overwrites a previous artifact:
The packaging host needs Python 3.11+, GNU strip/readelf, and patchelf.

```sh
python3 scripts/package-native-runtime.py \
  --sdk /absolute/native-sdk-install \
  --runtime /absolute/cef-runtime \
  --engine /absolute/current/suitecut-browser \
  --cef-credits /absolute/cef-sdk/CREDITS.html \
  --media-archive /absolute/pinned-ffmpeg.tar.xz \
  --output /absolute/new-artifact-directory
```

The media archive is checked against `native/client/tools/full-runtime-media.json`
before copying. Archive ownership/timestamps are normalized, file hashes and ELF
dependencies are recorded, and `SHA256SUMS` identifies the resulting archive.
The SDK, engine, CEF license/credits, libyuv license, and upstream FFmpeg license,
documentation and media provenance are retained. No model files are included.

This is a local release candidate. Its GPL-enabled media executables require
complete corresponding source and applicable third-party notices before public
redistribution; provider links alone are not a substitute. The manifest records
this publication hold. Stock CEF playback capabilities are unchanged by bundling
FFmpeg: FFmpeg's H.264/AAC support does not enable those codecs inside CEF.
The separately pending custom CEF patches/codec-enabled build are not included.
