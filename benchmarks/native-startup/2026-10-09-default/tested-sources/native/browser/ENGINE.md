# Native C++ media engine and Playwright adapter

SuiteCut's native path now keeps CEF rendering, PCM capture, encoding, muxing,
recording, RTMP publishing, checkpoint decoding and rendering in C++. It links
FFmpeg libraries directly. It does not run an FFmpeg subprocess for those tasks.
TypeScript sends lifecycle commands, supplies Playwright's transport, and builds
SuiteCut manifest/timeline plans. The explicit `suitecut/playwright` legacy backend
remains separate; its older capture pipeline is not this native engine.

```ts
import { launchEngine } from 'suitecut/engine'

const engine = await launchEngine({
  executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE!,
  width: 1920, height: 1080, framesPerSecond: 30,
})
try {
  const context = await engine.browser.newContext()
  const page = await context.newPage()
  await page.goto('https://example.com')
  const recording = await engine.record(page, { destination: '/tmp/flow.mp4' })
  try {
    await page.getByRole('link').first().click()
    await page.mouse.wheel(0, 500)
  } finally {
    await recording.stop()
  }
  await context.close()
} finally {
  await engine.close()
}
```

`engine.browser`, contexts, pages and locators are actual Playwright objects.
Playwright's injected utility code, locator engine and auto-waiting stay in
Playwright. The C++ bridge supplies CEF targets, request contexts and private CDP
message routing. It opens no debugging TCP listener. The adapter uses the public
custom transport in Playwright 1.62.1; the package now requires Playwright 1.62.1 or later. Only 1.62.1 is verified here.

`engine.stream(page, {destination: 'rtmp://host/app/key'})` starts an independent
native output. `engine.select(output, otherPage)` switches its browser page inside
the same engine without replacing the encoder. `output.pause(true/false)` excludes
preparation time. `output.status()` exposes measured encoder and queue counters;
`output.failure` reports runtime failures. `output.stop()` finalizes the muxer,
joins the native worker and releases queued media, and is idempotent in the SDK.

Realtime recording and streaming always wait in C++ for video and native source
sound before starting their shared timeline. There is no selectable startup policy
and no immediate-start bypass for audio-enabled output. CEF initialization and an
AudioContext reporting `running` do not establish native audio readiness.

```ts
await page.getByRole('button', { name: 'Start audio' }).click()
const recording = await engine.record(page, {
  destination: '/tmp/flow.mp4',
  audioStartupTimeoutMs: 5000,
})
```

Both tracks start at the first available PCM sample exceeding 32 in signed 16-bit
magnitude in either channel (approximately −60 dBFS), no earlier than the earliest
buffered video. Preceding startup silence is excluded. Activate sound before or
concurrently with a pending `record`/`stream` call; waiting for that call to return
before activating its sound will time out. Subsequent intentional silence and
source gaps retain their original timestamps.

`audioStartupTimeoutMs` is an integer from 1 to 60000, defaults to 5000, and starts
after output/encoder initialization. No source sound produces an explicit error;
stopping interrupts the wait. Intentionally silent flows must declare
`audio: false`, which produces video-only output. Offline file rendering does not
wait for live CEF audio and retains its source silence. Waiting stays in C++ even
when the SDK event loop stalls. The helper must confirm mandatory readiness;
the SDK stops outputs from older helpers that do not confirm it. The removed
`audioStartup` field is rejected by both the SDK and native protocol.

The default distribution is CEF **154.0.34 / Chromium 154.0.8037.98**. Linux x64
is validated; the macOS ARM64 archive is checksum-pinned but this upgrade has not
been built or run on macOS.

MP4 recording uses keyframe fragments and flushes completed segments, so an abrupt
helper stop preserves earlier fragments. The unfinished final fragment can be lost.
Explicitly stop outputs before closing their page/context. The legacy health
adapter uses queue depth for capture lag and historical maximum encoding time
for output lag; use native counters and decoded media for quality conclusions.

## Codec selection

`await engine.encoders()` returns the native library's curated encoder list,
including codec names, compatible containers, hardware/lossless flags and whether
each encoder is linked. `linked: true` means the FFmpeg library contains that
encoder; hardware still needs a suitable device and driver. Starting an unavailable
encoder reports its error. There is no automatic codec fallback.

```ts
const encoders = await engine.encoders()
const av1 = await engine.record(page, {
  destination: '/tmp/flow-av1.mp4',
  videoCodec: 'libaom-av1', audioCodec: 'aac', container: 'mp4',
  crf: 32, audioBitrateKbps: 160,
})
await av1.stop()

const lossless = await engine.record(page, {
  destination: '/tmp/flow-lossless.mkv',
  videoCodec: 'ffv1', audioCodec: 'flac',
})
await lossless.stop()
```

| Video encoder | Codec | Tested pairing | Recording default |
| --- | --- | --- | --- |
| `libx264` | H.264 | AAC / MP4 | veryfast, zero latency, CRF 18 |
| `libx265` | H.265 / HEVC | AAC / MP4 | ultrafast, zero latency, CRF 23 |
| `libvpx` | VP8 | Opus / WebM | realtime, speed 8, quality 18, target 4500 kbps |
| `libvpx-vp9` | VP9 | Opus / WebM | realtime, speed 5, CRF 18 |
| `libsvtav1` | AV1 | AAC / MP4 | preset 12 (SVT 2.3 maps to 11), normal prediction/encoder lookahead, CRF 32 |
| `libaom-av1` | AV1 | AAC / MP4 | realtime, speed 8, zero lookahead, CRF 32 |
| `ffv1` | FFV1 | FLAC or PCM / MKV | level 3, slice checksums |
| `h264_nvenc`, `hevc_nvenc`, `av1_nvenc` | NVIDIA H.264 / HEVC / AV1 | Device verification pending | preset p4, ultra low latency, target bitrate |

`audioCodec` accepts `aac`, `libopus`, `flac` and `pcm_s16le`. `container`
accepts `mp4`, `webm`, `mkv` and `mov`, or is inferred from the destination's
extension. The native engine checks codec/container compatibility before creating
the file. Use `engine.encoders()` for the compatibility matrix of the actual build.
MP4 and MOV HEVC outputs use the `hvc1` sample entry.

`crf` is an encoder-specific software quality setting: 0–51 for H.264/HEVC and
0–63 for VP8/VP9/AV1. Lower values generally preserve more detail and use more
space. Equal values across codecs do not mean equal visual quality. SVT uses its
normal prediction mode: its low-delay mode crashed in the sanitizer test with
SVT 2.3 at 320×180, and is not enabled. Use AOM's realtime mode when low AV1
encoder latency matters. Omit `crf` for FFV1,
hardware encoders and streaming. VP8 uses constrained quality plus `bitrateKbps`;
the other software recording paths use quality control. `audioBitrateKbps` is
16–512 and affects AAC/Opus; FLAC/PCM preserve samples rather than target a bitrate.

FFV1 is lossless relative to the native **8-bit BT.709 limited-range I420** input;
browser RGB has already undergone chroma subsampling and color conversion. FLAC
and PCM preserve the engine's **48 kHz stereo signed 16-bit PCM**. This API does
not add HDR, 10-bit capture or RGB-lossless capture.

RTMP remains conventional **H.264 + AAC in FLV**. HEVC/AV1 enhanced RTMP is not
implemented. A recording can use a different codec while an independent H.264
stream runs from the same CEF page. The media core uses two FFmpeg worker threads
and constrains the separate x265/SVT pools; total browser and encoder thread counts
are larger and measured in the benchmark evidence.

`engine.render()` accepts these codec/container names through `config.output`.
Its existing `standard`, `high` and `master` quality profiles are intended for
offline presentation export; their settings differ from realtime recording.
The October 10 codec report and reproducible collectors are in
[`benchmarks/native-codecs`](../../benchmarks/native-codecs/README.md). Software
paths were tested on Linux. This host exposes no GPU, so NVIDIA encoder startup
failure and recovery were checked, but GPU encoding performance remains unverified.

Existing `recordNative` and native `createBroadcast` use this engine. Their old
`ffmpegPath` setting is retained for source compatibility and is not executed.
They reject helpers without the C++ media capability. Raw `onFrame` / `onAudio`
subscriptions are opt-in diagnostic APIs and are not used by native recording or
publishing. The existing `NativePage` facade is retained for SuiteCut presentation
recipes; use `launchEngine` for Playwright's APIs.

`engine.render({manifestPath, outputPath, config})` executes the existing SuiteCut
presentation plan with native libavfilter, codecs and muxing. The root renderer and `suitecut render` CLI
launch a helper using `SUITECUT_NATIVE_EXECUTABLE`, or accepts `nativeSource`.
The low-level `suitecut/render` module accepts that same explicit native source;
without it, the module retains its legacy FFmpeg command-line renderer. Scene and
manifest planning remains TypeScript; pixel and PCM processing does not. Render
reports retain the existing `ffmpeg` field: for native execution its executable
is `suitecut-native-libav` and its args describe the equivalent presentation plan for review. The helper
executes that plan through linked libraries.

For Playwright Test, import `{test, expect}` from `suitecut/engine/test`.
Set the worker option `suitecutNativeOptions` (or `SUITECUT_NATIVE_EXECUTABLE`).
The adapter replaces the runner's browser fixture with the CEF connection.

## Build

Install FFmpeg **7.1+** development headers/libraries for libavcodec, libavformat,
libavutil, libavfilter, libswscale and libswresample, plus the required encoder
implementations. Debian 13 packages are `libavcodec-dev libavformat-dev
libavutil-dev libavfilter-dev libswscale-dev libswresample-dev`. Recording defaults
to libx264/AAC in the engine API, or VP9/Opus for legacy WebM manifests.

Use the pinned CEF and libyuv versions in this directory. Build with C++20:

```sh
cmake -S native/browser -B .suitecut/native-build -G Ninja \
  -DCEF_ROOT="$PWD/.suitecut/cef-sdk" \
  -DLIBYUV_ROOT="$PWD/.suitecut/libyuv" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/native-build --parallel 4
ctest --test-dir .suitecut/native-build --output-on-failure
```

For a private development sysroot, add `-DSUITECUT_FFMPEG_ROOT=/absolute/sysroot`.
Dynamic FFmpeg libraries and their codec dependencies must exist on the runtime
host. The saved host build uses Debian FFmpeg 7.1.5. New media code is tested on
Linux x86-64; macOS linking/packaging remains unverified, and Windows is rejected.
CEF's sandbox remains enabled. Hardware encoding is unverified on this host;
GPU texture sharing is not implemented.

## Ownership and limits

CEF callbacks enqueue shared immutable buffers; no encoder or network write runs
on a CEF callback. Each output owns one native worker and separate bounded queues:
up to 20 video frames / 64 MiB and 500 audio packets / 1.92 MiB. Slow outputs replace
old video frames and explicitly count discarded audio samples. Offline checkpoint
input applies backpressure instead of dropping frames. Native file jobs release
FFmpeg contexts, packets, frames and filter graphs through RAII.

Audio status distinguishes `droppedAudioSamples` (queue overflow),
`missingAudioSamples` (uncovered input replaced with zero PCM), and
`lateAudioSamples` (input submitted after its interval was encoded). These are
48 kHz frames per channel; divide by 48 to obtain milliseconds. Source-provided
silent PCM is covered input and does not increment the missing counter. `ready`
means the encoder and first video frame are ready; it does not guarantee CEF's
audio stream has started in the default `immediate` mode. In a wait mode, `ready`
also means the requested native audio condition was met. It does not guarantee
future source continuity.

Set `SUITECUT_AUDIO_TRACE_DIRECTORY` to an existing directory for buffered native
source PCM/timestamp and output PCM/coverage traces. Tracing is opt-in, includes
page audio, and writes local files rather than SDK events. See
`benchmarks/native-audio/2026-10-10/report.md` for source/encoder gap attribution,
startup behavior, and CPU scheduling controls. The current output jitter buffer
is 150 ms; increasing it cannot reconstruct absent timestamped source intervals.

The control protocol bounds commands at 4 MiB, responses/events at 16 MiB,
queued UI commands at 64 / 16 MiB, DevTools requests at 256 per page, and control
output at 4096 messages / 16 MiB. The larger command limit is needed for
Playwright's utility script. Output count is eight per CEF target, and concurrent
native file jobs are bounded at eight. Local file rendering supports cancellation.

This is Chromium/CEF compatibility, not Firefox or WebKit support. The curated
runner checks are in `benchmarks/native-engine/engine.e2e.spec.ts`; passing those
does not establish every Playwright feature. Permission grants/reset are handled
by CEF request contexts and permission callbacks. The native permission adapter
accepts geolocation, notifications, microphone/camera, camera pan/tilt/zoom, MIDI
SysEx, clipboard, sensors, idle detection, window management and local fonts;
other permission names fail explicitly. Geolocation grant/reset is tested.
Partitioned cookies are explicitly unsupported. Extra CDP sessions share CEF
agent domain state; independent domain enable/disable and event fidelity are
unverified. Tracing, service workers, cross-origin process frames, proxy/auth
combinations and the complete upstream Playwright suite remain unverified. Native RTMP reconnect/adaptive bitrate,
hardware encoding, signed binary distribution and a full upstream Playwright
compatibility suite remain unimplemented or unverified. Switching between
separate helper processes is rejected; use pages in one engine.

Memory sanitizers exercise the media core, including failure paths and offline
jobs. They do not instrument the prebuilt CEF/Chromium libraries or prove the
absence of every possible leak. See the saved benchmark report for measured
long-run memory and continuity, and exact tested builds.

FFmpeg and codec licensing depends on the linked build. The tested Debian build
includes GPL encoders. SuiteCut's MIT source license does not replace upstream
binary redistribution obligations. CEF/Chromium and libyuv notices also apply.
