# SuiteCut native backend 2.1.0

This is the complete native backend for thin SDKs in any language: C++ owns
CEF, frame/audio capture, the shared A/V clock, buffering, FFmpeg encoders,
muxing, recorded files and RTMP/RTMPS publishing. SDKs own process lifetime
and send commands; raw media does not pass through a language adapter for encoding.
No Node runtime or Python SDK is required by the native process.

The package exposes a versioned private stdio API, not an in-process ABI.
`PROTOCOL.md` describes automation, output readiness, status, pause, target
selection and stop. `media.stop` finalizes a playable file containing both
tracks. Streaming sends the muxed output directly to the chosen RTMP endpoint.
It does not require an SDK to implement synchronization, an encoder or a muxer.

## Linux runtime

Extract the Linux x86-64 runtime and spawn `runtime/suitecut-browser` with the
launch arguments in `PROTOCOL.md`. Keep the runtime directory together.
It includes CEF/Chromium 152, browser resources, the compiled media engine,
FFmpeg shared libraries and their additional media dependencies, dependency
inventories and license notices. Library lookup uses `$ORIGIN:$ORIGIN/lib`.

This Debian 13 build still requires compatible system/Chromium display and
audio libraries, a display server and audio service. It is not an operating
system image. `runtime/runtime-dependencies.json` lists bundled libraries and
remaining system dependencies. CEF's sandbox stays enabled. Linux x86-64 is
verified; macOS packaging and Windows delivery are not verified.

Use `media.encoders` to discover available codecs and containers. Availability
does not prove that a hardware encoder can initialize on the deployment host.
Recording defaults to H.264/AAC with a container inferred from the destination;
WebM requires appropriate VP8/VP9 and Opus codec selections.

## Build from the source package

Use CMake 3.21+, Ninja, C++20, pinned libyuv and CEF, and FFmpeg 7.1+ development
libraries. On Debian 13, install `libavcodec-dev libavformat-dev libavutil-dev
libavfilter-dev libswscale-dev libswresample-dev` plus CEF's Linux build/runtime
dependencies. From the extracted source package:

```sh
python3 scripts/install-cef-codecs.py --destination "$PWD/deps/cef"
git init deps/libyuv
git -C deps/libyuv fetch --depth=1 \
  https://chromium.googlesource.com/libyuv/libyuv \
  7c85a3a0820fab29abb502c207d8b9a394e352cc
git -C deps/libyuv checkout --detach FETCH_HEAD
cmake -S . -B build -G Ninja \
  -DCEF_ROOT="$PWD/deps/cef" -DLIBYUV_ROOT="$PWD/deps/libyuv" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The executable is `build/native/browser/suitecut-browser`. Source builds use
installed FFmpeg libraries; preparing the complete runtime also bundles those
libraries, records their origins and retains their notices. A private FFmpeg
development sysroot can be selected with `-DSUITECUT_FFMPEG_ROOT`.
In a repository checkout, configure `-S native/engine`.

To prepare runtime packages from a verified repository build:

```sh
python3 scripts/stage-native-runtime.py \
  --binary-dir /absolute/path/to/build/native/browser \
  --output /absolute/path/to/new/runtime
python3 scripts/pack-native.py --output /absolute/path/to/new/packages \
  --runtime-dir /absolute/path/to/new/runtime \
  --verification /absolute/path/to/verification
```

These tools record checksums and package origins and refuse to overwrite prior
artifacts. Staging currently targets Debian 13 Linux x86-64 and retains each
bundled media library's Debian copyright notice and common license texts.

## Package boundary

`suitecut-native` is the SDK foundation that returns ready encoded A/V outputs.
The smaller `suitecut-cef` package is an optional browser/capture component for
applications that supply their own media pipeline. Neither archive contains a
Python SDK. Their manifests distinguish `nativeMedia:true` from `false`.

The native API can be wrapped in TypeScript, Python, Rust, Go or another
language. SDKs must drain stdout/stderr, validate packets, set request deadlines,
wait for engine output readiness, inspect failures and close the owned process.
The native backend's native-clock buffering handles normal A/V timing skew;
it does not guarantee absent source silence or zero dropped frames under overload.
Earlier audio quietness and whole-Chromium memory-audit limits remain documented
in SuiteCut's saved release evidence.

SuiteCut source is MIT licensed. Bundled CEF, Chromium, FFmpeg and codec
libraries retain their own licenses; this runtime uses Debian's GPL-enabled
FFmpeg build. Keep `runtime/licenses` and its package/source inventory with any
redistribution. These archives are prepared locally, not published.
