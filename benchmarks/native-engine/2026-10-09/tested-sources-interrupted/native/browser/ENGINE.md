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
Explicitly stop outputs before closing their page/context. The legacy health
adapter uses queue depth for capture lag and historical maximum encoding time
for output lag; use native counters and decoded media for quality conclusions.

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
manifest planning remains TypeScript; pixel and PCM processing does not.

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
CEF's sandbox remains enabled. No hardware encoder or GPU texture-sharing path
is implemented.

## Ownership and limits

CEF callbacks enqueue shared immutable buffers; no encoder or network write runs
on a CEF callback. Each output owns one native worker and separate bounded queues:
up to 20 video frames / 64 MiB and 500 audio packets / 1.92 MiB. Slow outputs replace
old video frames and explicitly count discarded audio samples. Offline checkpoint
input applies backpressure instead of dropping frames. Native file jobs release
FFmpeg contexts, packets, frames and filter graphs through RAII.

The control protocol bounds commands at 4 MiB, responses/events at 16 MiB,
queued UI commands at 64 / 16 MiB, DevTools requests at 256 per page, and control
output at 4096 messages / 16 MiB. The larger command limit is needed for
Playwright's utility script. Output count is eight per source, and concurrent
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
