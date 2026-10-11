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

The IPC transport delivers each CDP message in a separate Node task, preserving
packet order while allowing promise continuations to run between packets.
Delivering a decoded batch synchronously can lose execution contexts during page
initialization. The SDK bounds queued delivery to 1,024 messages / 64 MiB of wire
payload, closes on overflow, and drains final events before native disconnection.
This scheduling belongs to the thin automation adapter; recording, mixing,
encoding and synchronization remain in C++.

Independent proxy and website credentials require the pinned Playwright 1.62.1
[authentication patch](../../patches/README.md). The repository applies it with
pnpm; downstream SDK users must explicitly configure their browser driver too.
Importing SuiteCut does not modify another project's installed Playwright.

Set `SUITECUT_CDP_TRACE=1` for bounded native CDP diagnostic metadata on stderr.
The trace omits credentials, URLs, script arguments, bodies and video payloads;
normal operation leaves it disabled.

## External browser automation

`connectBrowser()` is exported from `suitecut`, `suitecut/engine`, and
`suitecut/playwright`. It returns an actual Playwright `Browser` connected to an
independently launched browser. It does not launch a CEF source or create a native
media engine. External pages cannot be passed to `engine.record()` or
`engine.stream()`; those require a page belonging to that engine's CEF browser.

```ts
import { connectBrowser } from 'suitecut/engine'

const browser = await connectBrowser({
  endpoint: 'http://127.0.0.1:9222',
  timeoutMs: 10000,
})
try {
  const context = browser.contexts()[0]
  if (!context) throw new Error('No existing browser context')
  const page = context.pages()[0] ?? await context.newPage()
  await page.goto('https://example.com')
  await page.mouse.wheel(0, 500)
} finally {
  await browser.close()
}
```

| Option | Meaning |
| --- | --- |
| `endpoint` | Required endpoint URL. CDP accepts HTTP, HTTPS, WS or WSS; Playwright accepts WS or WSS. |
| `protocol` | `cdp` by default, or `playwright`. The endpoint must speak the selected protocol. |
| `browserName` | CDP requires `chromium`. Playwright supports `chromium` (default), `firefox`, or `webkit`. |
| `headers` | Optional connection authentication headers. |
| `timeoutMs` | Connection deadline, default 30000; 0 disables the deadline. |
| `slowMoMs` | Optional action delay, default 0. |
| `noDefaults` | CDP only, default true: avoids Playwright's default download, focus and media overrides on the existing context. |
| `exposeNetwork` | Playwright protocol only: forwards Playwright's client-network exposure rules. |

CDP uses Chromium's debugging endpoint, for example an independently launched
Chrome with `--remote-debugging-address=127.0.0.1 --remote-debugging-port=9222
--user-data-dir=/absolute/path/to/separate-profile`. Modern Chrome requires a
non-default user-data directory. This does not attach to an arbitrary browser
that has no automation endpoint. A remote endpoint must be reachable from the SDK
host. Keep debug endpoints private and authenticate access as appropriate.

For a Playwright server, pass `protocol: 'playwright'` and its `wsEndpoint()`;
the client and server Playwright major/minor versions must match. CDP has lower
fidelity than the Playwright protocol and does not imply complete Playwright
conformance. Actual Chromium HTTP-CDP, WS-CDP and Playwright WS connections are
tested here; Firefox/WebKit selection and authentication forwarding are unit
tested, but their live remote servers and TLS endpoints are unverified.

Calling `browser.close()` on these connected objects disconnects the client and
leaves the external browser/server running. CDP's existing default-context tabs
are retained, including their page and storage state in the integration test.
Explicit `page.close()` or `context.close()` still closes those objects. Playwright
server contexts created by this connection are cleaned up on disconnect. Close
temporary contexts you create, and let the browser owner stop its process.

Run `pnpm test:browser:connect` for real local external-browser interaction and
reconnection tests. Detailed results are saved in
`benchmarks/external-browser/2026-10-10/`.

## Native output lifecycle

`engine.stream(page, {destination: 'rtmp://host/app/key'})` starts an independent
native output. `engine.select(output, otherPage)` switches its browser page inside
the same engine without replacing the encoder. `output.pause(true/false)` excludes
preparation time. `output.status()` exposes measured encoder and queue counters;
`output.failure` reports runtime failures. `output.stop()` finalizes the muxer,
joins the native workers and releases queued media, and is idempotent in the SDK.

Realtime recording and streaming wait in C++ for both initialized encoders, the
muxer/header, and the first captured video frame. There is one startup policy.
Once CEF requests an audio capturer, output readiness waits for its first PCM
block, including a block containing silence. Both tracks start at the common
captured epoch. `source.command('audio.status')` reports whether CEF requested
capture, whether the stream is active, its packet count and last callback time.
This is useful for fixtures that deliberately start continuous audio before
recording. A future page sound cannot be predicted from those callbacks.

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
integer from 1 to 60000 (default 5000) bounding the first-video/first-requested-audio
wait after encoder and muxer initialization. Missing required capture produces an explicit timeout; stop
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

### Playwright transport and context initialization

Use the explicitly installed [Playwright 1.62.1 patch](../../patches/README.md)
with the current native helper. `Browser.getVersion` forwards Chromium's real
version and user agent and adds `suitecutWindowless: true`. The patched driver
uses this capability to avoid toolbar/border insets and sends a context's locale
as `Target.createBrowserContext.suitecutLocale`. Native clients can use the same
extension: a nonempty language tag of at most 128 ASCII letters, digits and
hyphens. CEF receives the language in its request-context settings before any
page or popup is created. Target-scoped CDP emulation keeps its own scope.

The thin SDK submits at most 16 CDP IPC commands concurrently, leaving room
within the native client's 64-command limit. Its FIFO is bounded to 8,192
queued/in-flight commands and 64 MiB of serialized CDP payload; overflow closes
the transport. Native acknowledgement frees a slot; it does not wait for the
renderer response, which could depend on another command. Disconnect/failure
clears queued commands. Incoming packets continue to yield between deliveries
so frame registration promise continuations run before execution-context events.

The command count accommodates Playwright adopting thousands of element handles
at once; concurrency and the byte limit remain bounded. Incoming delivery has a
separate 1,024-message / 64 MiB limit. CDP forwarding preserves the original JSON
property order and rewrites only bridge metadata.

`launchEngine({ ..., artifactsDir: '/absolute/worker-artifacts' })` and
`connectCEF(source, { artifactsDir })` pass the directory to Playwright's public
transport API. The caller owns its cleanup. This configures Playwright artifact
storage; it is not a native capture setting.

Automation-page renderer crashes emit a page `crash` event, fail that page's
active media outputs and pending commands, and preserve healthy sibling pages.
The primary capture/control source remains a fatal runtime boundary. Modifier
clicks create independent tabs in the same request context, with no JavaScript
opener; managed tab navigation waits for automation initialization. Ordinary
`window.open` first-script timing still has separate limitations.

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
owns its context/frame/packet; H.264 uses one encoder thread through 1080p and
two slice threads above 1080p and three at 4K, avoiding added frame-thread latency. Other video
codec budgets remain bounded at one thread. `videoEncoderThreads` reports the
configured FFmpeg thread budget. A mutex serializes muxer writes.
H.264 uses `veryfast` through lower resolutions and `ultrafast` at 4K to leave
CPU headroom for browser/audio capture on a four-core host. The latter produces
larger files at fixed CRF and lower compression efficiency at fixed bitrate;
`videoEncoderPreset` reports the selected preset. Neither track may
start its next block until the other track has completed through that block's
start timestamp. Encoder progress can therefore lead by at most one media block
(one video frame or one audio codec block). This holds encoder output while CEF
capture continues, and bounds the muxer
interleaver backlog. Network writes can still delay either track, with a bounded
I/O deadline.

Realtime video chooses the nearest buffered capture within half a frame
interval (8.33 ms at 60 FPS; 16.67 ms at 30 FPS), avoiding extra duplicates from
small callback jitter. Distant future frames remain held back. Offline
checkpoints keep their exact ordering/timestamps.

Audio packets use their delivered sample clock for timestamp
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
`activeMissingAudioSamples` counts uncovered frames while source capture is
active/requested. `audioSyncWaits` and `videoSyncWaits` count coordination waits.
`maximumTrackLeadMs` measures completed encoder positions, not audible lip sync;
content markers and source PCM traces are needed to assess the latter.

Set `SUITECUT_AUDIO_TRACE_DIRECTORY` to an existing directory for buffered native
source PCM/timestamp and output PCM/coverage traces. Tracing is opt-in, includes
page audio, and writes local files rather than SDK events. See
`benchmarks/native-audio/2026-10-10/report.md` for source/encoder gap attribution,
startup behavior, and CPU scheduling controls. The current output jitter buffer
starts at 150 ms and adapts up to 960 ms when active-source input is delayed.
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
and tracing completion are regression-tested. Service-worker evaluation and
lifecycle, process-frame interaction, first-party IndexedDB state restoration,
HTTP/HTTPS proxy/auth, HAR replay and decoded Playwright video now have focused
and upstream checks. Worker main-script routing/HAR attribution, third-party
localStorage restoration, lone UTF-16 values, redirected HAR history and offset
iframe input now pass after adapter/configuration fixes. Windowless video
repaint/visibility and duplicate-dialog handling have dedicated regressions.
The unpatched Playwright credential overwrite is corrected by the explicitly
configured pinned dependency patch described above. CDP task delivery corrects
the reproduced video initialization race. See the saved
[follow-up report](../../benchmarks/playwright-compatibility/2026-10-11-followup/report.md)
for exact results and remaining boundaries, and the preceding
[compatibility report](../../benchmarks/playwright-compatibility/2026-10-11/report.md)
for baseline failures. Independent domain enable/disable
and the complete upstream suite remain unverified. Native RTMP reconnect/adaptive
bitrate, hardware encoding and signed binary distribution remain unimplemented
or unverified. Switching between
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


## Cookie and screenshot capture limits

Browser-context cookie operations use Chromium's page-scoped `Network` domain,
including CHIPS partition keys. A context without pages temporarily owns a tiny
private control page; it is retired once a user page can service the requests.
It is never exposed in the SDK's page/target lists or captured. Control requests
are limited to 64 pending commands and 16 MiB of wire data per agent, with a
10-second timeout. Closing a context cancels its pending requests and closes its
control page before native shutdown completes.

Screenshot capture temporarily resizes the offscreen surface while preserving
the emulated CSS viewport. The native bridge serializes captures per source,
including independent CDP sessions, with up to eight queued requests (4 KiB of
arguments each). Captures allow at most 8,294,400 physical pixels and 16,384
pixels per axis. For example, a 500×10,000 screenshot fits; a 32,767-pixel-tall
full-page image is outside this policy. Recording and streaming dimensions keep
their existing 3840×2160 bound. Temporary screenshot paint is excluded from native
media output, which holds its preceding video frame until normal paint resumes.
Browser surfaces use Chromium’s default white background, including JPEG
capture. PNG and WebP still honor explicit transparent background overrides.
Encoded CEF DevTools replies are limited to 16 MiB. Exceeding this limit fails
the source explicitly and closes its process, instead of leaving a capture or
command pending indefinitely. Use smaller captures or compressed formats when
an encoded screenshot would exceed this limit.

These verified workflows do not establish full upstream Playwright compatibility.
Early popup initialization, initial blank-origin srcdoc frames and some permission
overrides remain unresolved. Directory upload, DevTools promise-collection
behavior and an intermittent cross-site iframe click also need investigation.
See the [deep compatibility investigation](../../benchmarks/playwright-compatibility/2026-10-12-deep-diagnosis/report.md)
for every original failing case, independent CEF controls, fixes and retained limits.
