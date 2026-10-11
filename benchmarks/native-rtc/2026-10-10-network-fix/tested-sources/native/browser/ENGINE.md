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
  width: 1920,
  height: 1080,
  framesPerSecond: 30,
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
joins the native workers and releases queued media, and is idempotent in the SDK.

Realtime recording and streaming wait in C++ for both initialized encoders, the
muxer/header, and the first captured video frame. There is one startup policy.
Silent pages are ready even before CEF creates an audio stream: the native audio
worker emits timestamped zero PCM until source audio arrives. Neither audibility
nor an AudioContext reporting `running` is required. Both tracks use the same
origin, video frame count, and 48 kHz audio sample count; preceding silence stays
on that timeline, so later sound does not shift the video start.

```ts
const recording = await engine.record(page, { destination: '/tmp/flow.mp4' })
// Sound can start after the output is ready, including on initially silent pages.
await page.getByRole('button', { name: 'Start audio' }).click()
```

The existing `audioStartupTimeoutMs` field is retained for compatibility. It is an
integer from 1 to 60000 (default 5000) bounding the first-video wait after encoder
and muxer initialization. No captured video produces an explicit timeout; stop
interrupts the wait. `audio: false` explicitly creates video-only output. The
helper must confirm `audioReadiness: 'engine'`; the SDK stops outputs from older
helpers that only confirm the previous source-sound gate. The removed
`audioStartup` field is rejected by both the SDK and native protocol.

Stock CEF starts its loopback capture only after a page becomes audible and
stops it after two seconds of quiet. Output readiness cannot guarantee that this
source is initialized or preserve sound emitted before loopback starts. Cold
source startup can therefore still leave initial silence. The candidate
[persistent-capture CEF patch](cef-patches/README.md) targets the previous stock
CEF 154 source; it has not been built, deployed or rebased onto the codec-enabled
152 distribution. Monotonic capture PTS are mapped
against the matching host clock, avoiding first-callback latency in A/V alignment;
real timestamp discontinuities remain visible.

The [detailed synchronization evidence](../../benchmarks/native-sync/2026-10-10/report.md)
saves decoded capture results, input/output PCM attribution, real browser checks,
sanitizer repetitions, resource samples, failed attempts and exact tested builds.

Linux x64 now defaults to codec-enabled **CEF 152.0.6 / Chromium 152.0.7977.83**.
The older stock 154 build cannot decode H.264/AAC, even though SuiteCut's output
encoders support them. The new archive, extracted library and public build recipe
are pinned in `cef-build-linux-codecs.json`; this third-party binary was tested,
not independently rebuilt. macOS ARM64 remains on stock CEF 154, with its upgrade
and proprietary-codec playback unverified. See the [playback evidence](../../benchmarks/native-playback/2026-10-10/report.md).

MP4 recording uses keyframe fragments and flushes completed segments, so an abrupt
helper stop preserves earlier fragments. The unfinished final fragment can be lost.
Explicitly stop outputs before closing their page/context. The legacy health
adapter uses queue depth for capture lag and historical maximum encoding time
for output lag; use native counters and decoded media for quality conclusions.

## Codec selection

The encoders below control recorded/exported media. They do not enable browser
playback decoders. The Linux codec-enabled CEF profile separately enables H.264
and AAC playback (`proprietary_codecs=true`, `ffmpeg_branding=Chrome`). Real
progressive MP4 and MSE playback, captured AAC samples, and native GOFNS recording
are verified. Other proprietary playback codecs, DRM, hardware decoding and
macOS playback are not established by these checks.

`await engine.encoders()` returns the native library's curated encoder list,
including codec names, compatible containers, hardware/lossless flags and whether
each encoder is linked. `linked: true` means the FFmpeg library contains that
encoder; hardware still needs a suitable device and driver. Starting an unavailable
encoder reports its error. There is no automatic codec fallback.

```ts
const encoders = await engine.encoders()
const av1 = await engine.record(page, {
  destination: '/tmp/flow-av1.mp4',
  videoCodec: 'libaom-av1',
  audioCodec: 'aac',
  container: 'mp4',
  crf: 32,
  audioBitrateKbps: 160,
})
await av1.stop()

const lossless = await engine.record(page, {
  destination: '/tmp/flow-lossless.mkv',
  videoCodec: 'ffv1',
  audioCodec: 'flac',
})
await lossless.stop()
```

| Video encoder                           | Codec                     | Tested pairing              | Recording default                                                           |
| --------------------------------------- | ------------------------- | --------------------------- | --------------------------------------------------------------------------- |
| `libx264`                               | H.264                     | AAC / MP4                   | veryfast, zero latency, CRF 18                                              |
| `libx265`                               | H.265 / HEVC              | AAC / MP4                   | ultrafast, zero latency, CRF 23                                             |
| `libvpx`                                | VP8                       | Opus / WebM                 | realtime, speed 8, quality 18, target 4500 kbps                             |
| `libvpx-vp9`                            | VP9                       | Opus / WebM                 | realtime, speed 5, CRF 18                                                   |
| `libsvtav1`                             | AV1                       | AAC / MP4                   | preset 12 (SVT 2.3 maps to 11), normal prediction/encoder lookahead, CRF 32 |
| `libaom-av1`                            | AV1                       | AAC / MP4                   | realtime, speed 8, zero lookahead, CRF 32                                   |
| `ffv1`                                  | FFV1                      | FLAC or PCM / MKV           | level 3, slice checksums                                                    |
| `h264_nvenc`, `hevc_nvenc`, `av1_nvenc` | NVIDIA H.264 / HEVC / AV1 | Device verification pending | preset p4, ultra low latency, target bitrate                                |

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

Use the pinned CEF and libyuv versions in this directory. On Linux x64, install
the verified codec-enabled SDK with Python 3.11+ and build with C++20:

```sh
pnpm native:install:cef
cmake -S native/browser -B .suitecut/native-build -G Ninja \
  -DCEF_ROOT="$PWD/.suitecut/cef-codecs-sdk" \
  -DLIBYUV_ROOT="$PWD/.suitecut/libyuv" \
  -DSUITECUT_CEF_PIN_FILE=cef-build-linux-codecs.json \
  -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/native-build --parallel 4
ctest --test-dir .suitecut/native-build --output-on-failure
SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser" pnpm test:native:playback
```

For macOS, use the stock ARM64 SDK and build instructions in [README.md](README.md).

For a private development sysroot, add `-DSUITECUT_FFMPEG_ROOT=/absolute/sysroot`.
Dynamic FFmpeg libraries and their codec dependencies must exist on the runtime
host. The saved host build uses Debian FFmpeg 7.1.5. New media code is tested on
Linux x86-64; macOS linking/packaging remains unverified, and Windows is rejected.
CEF's sandbox remains enabled. Hardware encoding is unverified on this host;
GPU texture sharing is not implemented.

## Ownership and limits

CEF callbacks enqueue shared immutable buffers; no encoder or network write runs
on a CEF callback. Each output owns independent audio and video encoding workers and bounded input queues:
up to 64 video frames / 768 MiB and 500 audio packets / 1.92 MiB. Slow outputs replace
old video frames and explicitly count discarded audio samples. Offline checkpoint
input applies backpressure instead of dropping frames. Native file jobs release
FFmpeg contexts, packets, frames and filter graphs through RAII. Each encoder
owns its context/frame/packet; realtime video uses one encoder thread per output
to reserve CPU for browser rendering and audio. A mutex serializes muxer writes. Neither track may
lead the other by more than 500 ms before the next encode, bounding the muxer
interleaver backlog. Network writes can still delay either track, with a bounded
I/O deadline. Audio packets use their delivered sample clock for timestamp
deviations under 70 ms, as OBS does. The cumulative correction stays below
70 ms; larger discontinuities retain the source timestamp. This removes
avoidable silence from small PTS deviations, but cannot reconstruct lost PCM.

Audio status distinguishes `droppedAudioSamples` (queue overflow),
`missingAudioSamples` (uncovered input replaced with zero PCM), and
`lateAudioSamples` (input submitted after its interval was encoded). These are
48 kHz frames per channel; divide by 48 to obtain milliseconds. Source-provided
silent PCM is covered input and does not increment the missing counter. `ready`
means both encoders, muxer, and first video frame are ready. It does not guarantee
that the page has created an audio stream or future source continuity. A silent
page without callbacks also increments the uncovered-input counter; that counter
alone does not distinguish intentional silence from lost source audio.

Set `SUITECUT_AUDIO_TRACE_DIRECTORY` to an existing directory for buffered native
source PCM/timestamp and output PCM/coverage traces. Tracing is opt-in, includes
page audio, and writes local files rather than SDK events. See
`benchmarks/native-audio/2026-10-10/report.md` for source/encoder gap attribution,
startup behavior, and CPU scheduling controls. The current output jitter buffer
starts at 150 ms and adapts up to 1000 ms when active-source input is delayed.
A later packet past a missing block confirms a timestamp hole beyond the
70 ms timing tolerance; that interval is preserved. A timestamp hole alone
does not prove missing audible samples. Source CSV traces retain `pts_ms` and
`raw_mapped_ms` alongside the effective `mapped_ms` for auditing corrections.
`bufferedDelayMs` reports this
latency tradeoff. Stop keeps accepting delayed PCM up to its original cutoff,
with a drain deadline of the current buffer delay plus 200 ms, then releases
queued ownership. Pause drains the pre-pause interval before clearing buffers;
fixed-frame audio codecs align pause to their next complete block; variable
PCM supports an exact sample boundary. Resume excludes preparation time and
refreshes the current captured view,
including pages that became static during preparation. More buffering cannot reconstruct absent source intervals.

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
agent domain state; page events are delivered to attached aliases and nested
worker session IDs retain their alias. Console events, page isolation, detach,
and tracing completion are regression-tested. Independent domain enable/disable,
service workers, cross-origin process frames, proxy/auth
combinations and the complete upstream Playwright suite remain unverified. Native RTMP reconnect/adaptive bitrate,
hardware encoding, signed binary distribution and a full upstream Playwright
compatibility suite remain unimplemented or unverified. Switching between
separate helper processes is rejected; use pages in one engine.

The [browser support matrix](BROWSER_SUPPORT.md) distinguishes verified web
behavior, deliberate SDK policies, Alloy/windowless constraints and remaining
adapter work. The resize bridge now applies per-target windowless bounds and
reports unsupported native-window states instead of acknowledging a no-op.

Memory sanitizers exercise the media core, including failure paths and offline
jobs. They do not instrument the prebuilt CEF/Chromium libraries or prove the
absence of every possible leak. See the saved benchmark report for measured
long-run memory and continuity, and exact tested builds.

FFmpeg and codec licensing depends on the linked build. The tested Debian build
includes GPL encoders. SuiteCut's MIT source license does not replace upstream
binary redistribution obligations. CEF/Chromium and libyuv notices also apply.

## Graphics selection

`launchEngine` accepts `graphics: 'auto' | 'swiftshader'`. Auto preserves Chromium
selection. Explicit SwiftShader provides software WebGL for CPU-only trusted
workloads, using Chromium's `enable-unsafe-swiftshader` opt-in with reduced
graphics security protections. Both WebGL versions were verified through shader
pixels, screenshots and native FFV1 output. See [browser support](BROWSER_SUPPORT.md)
for host-specific limits and reproduction commands.

## WebRTC with a network proxy

`launchEngine({ webrtcPolicy: 'disable-non-proxied-udp', ... })` requests
Chromium's `webrtc.ip_handling_policy = disable_non_proxied_udp` in both the
initial CEF request context and each new Playwright browser context. The default
is `webrtcPolicy: 'default'`, retaining Chromium behavior. The setting is
validated by the SDK/native entry; unsupported CEF preference setup fails
explicitly. It suppresses unproxied UDP, and Chromium can use proxy-supported
UDP or TCP, depending on the proxy and the website's ICE servers. It does not
create a TURN server, grant network access, or guarantee a connection.

For a proxy-only environment, a reachable authenticated TURN TCP/TLS endpoint
is needed. Jitsi supplied `turns:meet-jit-si-turnrelay.jitsi.net:443?transport=tcp`
in the October 10 trial; server addresses can change. The local policy
regression verifies default UDP candidates and suppression under the explicit
policy across root, isolated and proxied contexts. The policy deliberately
changes candidate availability; use default on ordinary networks.
