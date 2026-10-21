# SuiteCut 2.0 embedded browser

The default Chromium Embedded Framework (CEF) capture backend for SuiteCut 2.0. Each launch owns a
sandboxed CEF helper that can host isolated contexts and multiple native pages. Video comes from `CefRenderHandler::OnPaint`;
audio comes from `CefAudioHandler::OnAudioStreamPacket`. No screenshot loop, browser extension, or exposed remote-debugging port participates in capture.

The root `suitecut` API uses this backend.

## Runtime modules and local qualification

Production code is organized into compiled `protocol/`, `runtime/`, `page/` and
`process/` modules, with private controller fragments grouped by domain. See
[architecture and ownership](ARCHITECTURE.md) and the
[component build requirements](../../docs/building.md).

The October 21 macOS ARM64 qualification passed the full 90-case
[CEF regression package](../../tests/cef/README.md) with one worker, plus 30
frame handoff repetitions under the original three-second deadline. The local
checks cover navigation-close shutdown, frame metadata ordering and libc++
completion ownership. All 23 module translation units also passed compiler
checks without forced precompiled headers. These results are scoped to macOS
27.0.1; the historical Linux checks below are separate evidence. No CEF CI job
is enabled. See [macOS packaging and signing](../../docs/native-full-runtime-macos.md).

## Historical callback retry build a238fec

Native callback retries are integrated at production `a238fec`, with Runner
`343b915d`, Source `d29461c1` and merge tool `c3889a7e`. All native targets and
strict frontend compilation pass. Eight affected registered checks and four
affected public scenarios pass; the earlier first three public scenarios retain
their separate `14b8e08` positives. Current Source acceptance passes two genuine
browser tests with three ordered retirement/resume fences. This is composed
incremental evidence among 517 registered checks, not a fresh full-suite run.

`expectNative(callback).toPass()` uses native invocation, interval, deadline and
physical-retirement control. Global cancellation now holds cleanup through both
managed invocation and parent callback retirement under the original budget.
Twelve charged/clockless native lifecycle compositions pass optimized and executed
sanitizers. Using toPass in fixtures/all-hooks, nested toPass, callback modifiers and
`expect.poll` remain unsupported; Source fencing remains conservative over the
whole Engine, without concurrent Source isolation or browser-child reuse.

The unchanged SDK has 1,276 passing tests in 163 files, full type checking, and
composed lint evidence (prior full lint plus typed lint of the only changed file).
All 692 emitted outputs and 1,269 SDK quality inputs are unchanged in the final
native-only repair; independent review checks 2,801 unique recorded paths and 230
runtime assets. Network eviction avoids repeated body-metadata serialization in
one admission. No end-to-end latency speedup is inferred from test wall times.

Original failures remain recorded, including timeout315 and historical cleanup
negatives. Automatic snapshot baseline updates and broader cookie-date parsing
remain blocked by automatic review; the custom CEF build remains user-deferred.
Full feature parity is unfinished. Compact evidence:
`.suitecut/diagnostics/native-to-pass-a238fec-result-v1.json`.

Current Source `d29461c1` also passes one authored, unfiltered network attribution
diagnostic: child-frame and main-frame fetch requests each reach the real origin
once with exact frame/context identity and one successful terminal event. A
separate favicon attempt fails with `net::ERR_ABORTED` before reaching the server.
All extra traffic is retained. The five-check scope preserves 2,045 input hashes
and retires all 15 observed process identities. Generic zero command counters
coexist with retained network metadata; actual helper exit supplies the separate
physical evidence. Historical 57-case results and broader redirect/popup coverage
remain unchanged. Evidence: `.suitecut/diagnostics/native-network-attribution-current-a238fec-v1.json`.

## Previous native build 64cc681

The preceding incremental Runner `1a7dafcc` and merge tool `8ac59db7` pass all native
build targets, strict frontend compilation and 29 affected native checks. All 1,229
SDK tests in 159 files, types and lint pass. Native fail-on-flaky policy preserves
actual retry and scheduling outcomes while controlling final run status. Prior
poll/report performance improvements remain integrated. Source `4f8fefac` is
unchanged. Earlier timeout and cleanup negatives remain open; see the active
workboard for exact scopes, measured limits and pending features.

The preceding common Source `4f8fefac` passes matching HAR11 and clock11 browser
checks. Its common native run is 490 pass / 2 fail, with the corrected directory
checker scope passing separately and one output-preparation timeout unresolved.
SDK1,196/type/lint and all native builds pass. This is unsealed evidence; see
[the active workboard](../../docs/native-feature-workboard.md) and
[validation ledger](AUDIT.md) for exact limits and current feature work.

The latest sealed Linux candidate is `0d116d39`: all eight checks pass afresh,
with 423 native checks and 1,133 SDK tests in 150 files. Independent audit verifies
3,096 sealed files and 230 assets, complete before-gate inputs and unchanged SDK
outputs. Matching process-registration5 passes; deadline body/hook cases and a
declared navigation-promise fixture variant pass on exact sealed native/SDK bytes.
Unchanged original soft6 also passes, including its original 40 ms cleanup
mismatch case. Listing29 and attachment mutation8 pass in full CTest. Browser child reuse and
full parity remain unfinished. See [the validation ledger](AUDIT.md) for the
reader/fixture corrections, original negatives and exact runtime boundaries.
Previously sealed `b873192a` retains its scoped URL matcher6/HAR chronology6 and
mutable-array worker reuse4 evidence; these are separate earlier invocations.
Previously sealed `f01357a7`:395/395 native checks pass, with
1,048 SDK tests in 139 files. Strict native frontend/build, diff validation and full CTest are fresh;
four SDK gates/API reuse original passing evidence after exact SDK inputs and all 620
emitted artifacts match. The immutable manifest contains 2,156 hashes plus 230 assets.
The original global-timeout cleanup regressions and two new cleanup scopes pass;
real soft/reuse/fixture layers and HAR MIME/ZIP changes are integrated. Matching
browser proofs remain separately scoped. See [the validation ledger](AUDIT.md).
Previously sealed Linux candidate `4ba1c9d9`: all eight checks pass, with
373 native checks and 986 SDK tests in 133 files. Full native CTest and lint are
fresh; successful SDK and native compilation evidence is inherited after complete
input/artifact equality. The immutable inventory contains 2,075 hashes and 230
runtime assets. Native HAR aggregation/cookies, document/frame debugger binding,
and cached assertion deadline publication are compiled; matching browser checks
are pending. See [the validation ledger](AUDIT.md) for exact proof boundaries.
Previously sealed Linux candidate `9cffe672`: all eight checks pass, with
361 native checks and 984 SDK tests in 133 files. All SDK gates and full native
CTest are fresh; strict native compilation is explicitly reused after exact native
input equality. Its immutable inventory contains 2,046 hashes and 230 assets.
Combined checks include anonymous-file parallel groups and native attachment
append/read declarations. Exact changing-screenshot2 passes; unchanged HAR10 is
6 pass / 4 fail with actual archive ordering and remaining negatives preserved.
See [the validation ledger](AUDIT.md) for proof scopes and pending work.
Previously sealed Linux candidate `b074f3e2`: all346 fresh native checks pass;
SDK964/130 and strict native compilation are transparently inherited from exact
unchanged R2 inputs/artifacts. The manifest records2002 hashes plus 230 runtime
assets. Exact drag5, Page/Context route removal9, script inventory6 and evaluation8
pass cleanly. Declared held-Drop3 proves real callback retention through cancellation
and genuine late-ACK retirement of all16 fields. Original drag19+2skips preserves
its66-byte Fontconfig warning; request387/popup478 and five HAR failures remain
visible. See [the validation ledger](AUDIT.md) for exact evidence and pending work.
The previously sealed Linux candidate is `ff9863a9`: all331 native checks and946 SDK
tests in128 files pass, with1951 recorded hashes plus 230 runtime assets. Matching
recursive JSON9/YAML7, unchanged accessibility79, checkbox/select50, chooser3,
context route removal4 and paused evaluation8 pass with empty native stderr and
complete immutable audits. Two reviewed checker-only description/wait variants
pass5 each; original checker failures remain recorded. See [the validation
ledger](AUDIT.md) for native compile evidence reuse and remaining gaps, including
coordinator emergency cleanup and deferred vendor capabilities.
Previously sealed `fb2068e8` passed320 native and903 SDK tests/121files, with its
own trusted drag5, original drag19+2skips, route8 and breakpoint8 scopes retained.
Previously sealed `97916950` passed 307 native and 877 SDK tests in 117 files;
its matching shared-snapshot publication passed 8/8 and prior ARIA negatives remain
recorded independently.
The previous sealed Linux candidate is `e38ddd29`:294 native checks and845 SDK tests
in114 files pass with all eight fresh gates and1804 hashes plus 230 runtime assets.
Matching locator predicates12, bounding boxes12 and unchanged predicate10 pass,
with empty native stderr, exact immutable audits and confirmed physical retirement.
Matching root debugger10 also passes; post-open page-owner abort needs a
following SDK cleanup fix.
The previous sealed Linux candidate is `31a44e69`:280 native checks and810 SDK tests
in110 files pass, all eight gates are fresh, with1761 recorded hashes plus 230
pinned runtime assets. Exact sealed highlighting12, bounding-box12 and actual
worker coverage9, fullquery110 and unchanged original bounding-box9 pass. The new
request-header wire6 remains1/6; raw receipt diagnostics identify absent post-redirect
ExtraInfo from pinned CEF. A separate held-selection group-transfer followup remains
uncompiled; same-process opaque child dispatch is still under repair.
The previous sealed Linux candidate is `c6fbefa8`:270 native checks and798 SDK tests
in107 files pass, with all eight fresh gates successful and1731 recorded hashes
plus 230 pinned runtime assets. Matching sibling bootstrap4 and unchanged
ownerFrame10 pass. The new highlight checker exposes a real loading-document
selector error after its first9 groups; follow-up repair remains pending.
Previous d9b5f817 has its own separate passing runtime scopes. Matching pristine10, dispatch12, unchanged
original dispatch17 and worker ownership/reattachment/history12 pass. Native
runner attachments, pending-attachment skip and EOF/kill retirement also have
matching sealed process evidence. Matching browser proofs are scope-specific;
see [the validation ledger](AUDIT.md) before treating a capability as verified.
The implementation transfers CPU BGRA or I420 buffers. It does **not** implement GPU
texture sharing, hardware encoding, a desktop UI, or a complete browser-automation API.

For the root API, migration from the beta, and native API usage,
see [the native-default guide](BETA.md).

## Build

For browser media playback, see [codec-enabled CEF builds and actual playback acceptance](CODECS.md).
The stock binary SDK below does not provide H.264/AAC decoding. Output FFmpeg
encoders and browser input decoders are separate.

Use CEF **156.0.7+g47dbc1d+chromium-156.0.8078.17 beta**, CMake 3.21+, Ninja, and a C++20
compiler. Obtain the matching minimal binary SDK from
[CEF's build distribution](https://cef-builds.spotifycdn.com/index.html).
Keep SDKs and outputs outside source control (for example under `.suitecut/`).
The Linux x64 archive URL, byte length, and SHA-256 checksum are pinned in
[`cef-build.json`](cef-build.json); verify the archive against that checksum before
extracting it. CMake rejects an SDK whose CEF version differs from this pin.
Builds do not silently fetch or update Chromium. The 156 beta archive has been
checksum-verified and builds/runs in this Linux workspace. See
[the validation ledger](AUDIT.md) for exact scope, upstream failures, capture
measurements, and the blocked whole-browser sanitizer experiment.
Fetch the exact [libyuv Git commit](libyuv-build.json) into `.suitecut/libyuv`.
Gitiles-generated source archives vary in metadata between requests, so their
compressed SHA-256 is not a stable pin. Git verifies the fetched objects; check
that `rev-parse HEAD` matches the pinned commit. libyuv supplies SIMD color
conversion for I420 and is linked statically.

The original CEF parser and string tests have a separate Linux harness:

```sh
python3 scripts/verify-cef-upstream.py \
  --cef-root .suitecut/cef-sdk \
  --cef-wrapper .suitecut/native-build/libcef_dll_wrapper/libcef_dll_wrapper.a
```

It requires Python 3.12+ and CMake, checks pinned source checksums, retains the
original assertions and upstream licenses, and records exact inputs and GoogleTest
results under `.suitecut/cef-upstream/runs/`. This validates CEF URL, JSON and
string behavior; browser-action conformance needs the separate automation suites.

```sh
git init .suitecut/libyuv
git -C .suitecut/libyuv fetch --depth=1 https://chromium.googlesource.com/libyuv/libyuv 7c85a3a0820fab29abb502c207d8b9a394e352cc
git -C .suitecut/libyuv checkout --detach FETCH_HEAD
git -C .suitecut/libyuv rev-parse HEAD
```

The pinned SDK targets this Linux x64 workspace. Build with:

```sh
cmake -S native/browser -B .suitecut/native-build -G Ninja \
  -DCEF_ROOT="$PWD/.suitecut/cef-sdk" \
  -DLIBYUV_ROOT="$PWD/.suitecut/libyuv" \
  -DPROJECT_ARCH=x86_64 -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/native-build --target suitecut-browser --parallel 2
export SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser"
```

On macOS ARM64, use the corresponding macOS SDK of the same CEF version:

```sh
cmake -S native/browser -B .suitecut/native-build -G Ninja \
  -DCEF_ROOT="$PWD/.suitecut/cef-sdk" \
  -DLIBYUV_ROOT="$PWD/.suitecut/libyuv" \
  -DPROJECT_ARCH=arm64 -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/native-build --target suitecut-browser --parallel 1
export SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser.app/Contents/MacOS/suitecut-browser"
```

The CMake bundle includes the CEF framework, sandbox helper apps, CEF license, and
Chromium credits. Keep the complete `.app` together. Local builds use the shared
signing policy, which defaults to ad-hoc signing on an unconfigured host. Pin a
fixed certificate for repeated certificate-backed builds. Distribution requires
separate notarization and platform qualification; local compilation alone does
not establish those gates.

Linux also needs Chromium's runtime dependencies. Set `headless: true` in native
launch options for display-free software rendering; the sandbox stays enabled.
Set `SUITECUT_NATIVE_HEADLESS=1` when running the native verifier scripts.
Native HTTP additionally requires libcurl 7.88 or newer and its development
package (`libcurl4-openssl-dev` on Debian), plus libpsl headers and library
(`libpsl-dev` on Debian; `libpsl` on macOS). CMake checks these dependencies;
the independent API cookie policy uses curl's native URL punycode API and the
actual public-suffix list. Associated/page request contexts use the genuine CEF
cookie store. The current source gives independent API contexts a bounded native
cookie jar; matching combined runtime verification is recorded separately in the
validation ledger. Their backing CEF contexts still own actual origin storage.
Native credential generation/import additionally requires OpenSSL 3 Crypto and
its development package (`libssl-dev` on Debian; `openssl@3` on macOS). CMake
checks the version and links the native credential controller to Crypto.

Native HTTP content validation requires zlib and Brotli development libraries
(`zlib1g-dev` and `libbrotli-dev` on Debian; `zlib` and `brotli` on macOS).
CMake links these explicitly. Brotli's encoder is used only by the content
validation component test; the browser uses its decoder. Native validation
accounts codec allocation requests before allocation (20 MiB per transfer,
64 MiB across transfers), bounds decoded output, and checks the owning HTTP
operation's original deadline. Incomplete HTTP framing retains the actual
libcurl/OS result; incomplete compressed representations produce a native
validation diagnostic without inventing an observed curl error code. A genuinely
successful HTTP transfer carrying zero representation octets remains empty when
supported encoding metadata is present. Nonempty compressed data must reach its
codec's terminal state; a zero Content-Length alone cannot bypass HTTP framing,
header validation, cancellation or the original deadline.

The macOS example requires the
corresponding macOS ARM64 SDK of exactly the same CEF version, with its own
archive checksum; the Linux archive cannot be used on macOS.
Windows configuration deliberately fails until its sandbox and packaging are
implemented. Do not work around platform problems by disabling the sandbox.

## Node API

The SDK exposes native contexts, pages and frames; CSS, text, role, test-ID and
custom selectors; locator and retained-element input; typed evaluation and
object handles; navigation; cookies and storage; networking; screenshots/PDF;
tracing; recording and publishing. C++ owns operation deadlines, retries, input,
physical protocol settlement and media processing. The SDK serializes requests
and hosts caller callbacks. Unsupported options fail explicitly. Each feature
family retains its verified scope and remaining gaps in
[the feature-matching spec](../../docs/playwright-feature-parity.md).

An actual crashed child page can recover through `page.reload()`, preserving
the Page identity and durable listeners. Old renderer handles, outstanding
operations and media jobs stay failed. Recovery waits for an actual new renderer,
navigation commit and main/utility execution contexts. The initial root renderer
still has helper-fatal crash behavior.

`tracesDir` publishes native trace/network/stack files and resources while a
chunk is active. Changes coalesce behind a native100ms flush deadline, including
console/network activity without a subsequent browser command. Stopping waits
for physical writers and exports the final chunk. See [native tracing](../../docs/native-tracing.md)
for publication ordering, limits and current coverage support.
Automatic CORS preflights stay in native network/HAR observations while being
excluded from public request events; early routing still has a separate gap.

The browser binary does not require Node. The commands above build the native
browser target. Internal browser checks remain available as explicit CMake
targets and can be built and run when verifying browser changes.

The separate test framework in [`native/runner`](../runner/README.md) is excluded
from the browser build by default. Its optional targets require an explicit
`-DSUITECUT_BUILD_TEST_RUNNER=ON`; test-framework parity is outside the current
browser-control task.
The private native protocol is not Playwright's wire protocol, and raw command
access does not establish API parity.

```ts
import { launchNativeBrowser, createNativeBroadcast } from 'suitecut/native'

const source = await launchNativeBrowser({
  executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE!,
  width: 1920,
  height: 1080,
  framesPerSecond: 60,
  pixelFormat: 'i420',
})

let broadcast
try {
  await source.navigate('https://example.com')
  broadcast = await createNativeBroadcast({
    source,
    ffmpegPath: '/absolute/path/to/ffmpeg',
    stream: { url: process.env.RTMP_URL!, audio: true },
  })
  await broadcast.ready()
  // Keep running until your application's stop signal or an observed failure.
  await Promise.race([source.failure, broadcast.failure, applicationStopSignal])
} finally {
  await broadcast?.stop()
  await source.close()
}
```

`applicationStopSignal` above is an application-owned promise. RTMP credentials
belong in environment variables or a secret store, never in examples or logs.
CEF and FFmpeg are explicitly selected executables; importing the module does not
download software, launch a browser, or start a broadcast.

Native broadcast audio is disabled by default. Set `stream.audio: true` to send
the selected CEF source's page audio; `false` creates video-only output. The C++
publisher receives CEF media directly; the SDK does not subscribe to frame/audio
callbacks or spawn a publishing process. See [native publishing](../../docs/native-publishing.md)
for actual progress, supported options, and current native capability limits.

- `navigate(url)` accepts HTTP(S), resolves after the main-frame load, and rejects
  HTTP errors, loading errors, concurrent navigation, closure, or a 30-second
  command timeout. Page subresources can continue loading afterward.
  Modal JavaScript dialogs and context menus are suppressed, downloads are denied,
  and new popup browser windows are rejected. In-page select popups are composited.
- `sendDevToolsCommand(method, params)` invokes CEF's native DevTools API over the
  private command pipe and returns the untrusted JSON result. Validate returned
  values. It supports input, DOM inspection, evaluation, and other Chromium
  DevTools commands; it is a trusted operator interface, not a page-facing API.
- `onFrame(callback)` delivers owned pixel buffers, their `pixelFormat`, physical width/height,
  a capture timestamp, and a cumulative native video-drop counter. The latest
  frame is delivered immediately to a new subscriber when available. Callbacks
  must be synchronous and return promptly. Do not mutate buffers shared with
  other subscribers; copy before modifying. Unsubscribe using the returned function.
- `pixelFormat: 'bgra'` (default) preserves full-resolution BGRA8 pixels. Use
  `pixelFormat: 'i420'` for streaming: native libyuv converts to contiguous Y, U,
  and V planes with BT.709 limited-range coefficients. I420 needs even CSS
  dimensions, discards alpha, and subsamples chroma; it reduces pipe bandwidth by
  62.5%. The publisher declares the matching matrix/range in the encoded video.
- `onAudio(callback)` delivers owned 48 kHz stereo PCM, signed 16-bit little-endian,
  plus frame count and first-sample timestamp. There is no extension or system
  microphone capture. Silence is supplied by the publisher when no audio arrives.
- Timestamps are mapped into Node's `performance.now()` clock at the startup
  handshake. Video timestamps describe capture callbacks. Audio PTS is anchored
  at its first callback and subsequent PTS deltas are preserved, including delayed
  callbacks under CPU pressure. A backward PTS or a jump more than 250 ms into the
  future re-anchors it. CEF builds have used different audio clock origins.
  This is not a hardware clock or a
  guarantee of zero latency. Clock and A/V drift need measurement in long runs.
- `failure` rejects for unexpected exit, renderer termination, audio error,
  malformed IPC, or a frame/audio subscriber exception. Observe it for each source.
  `closed` resolves once the child and stdio close; `close()` is idempotent and
  forces termination after five seconds if graceful shutdown stalls.
  Each source receives a private temporary Chromium root profile, removed after
  process closure. The user’s normal browser profile is never reused.
  `state` reports `running`, `closing`, `closed`, or `failed`; closed/failed sources
  reject new subscriptions and cannot be selected for a broadcast.
- A broadcast owns its publisher and retains its owner/selected page until the physical
  encoder exits. `await selectSource(next)` requires a real page in the same native
  helper with matching physical size and pixel format, and preserves the publisher
  connection while clearing previous audio. Graceful owner/selected page or context
  closure finalizes the publisher; crashes fail it. Automatic browser restart and
  game scheduling remain application responsibilities.
- `deviceScaleFactor` (1 or 2) preserves CSS dimensions while increasing physical capture
  dimensions, up to 3840×2160. Dimensions are immutable for each source. Broadcasts
  require even physical dimensions. Use a replacement source to change layouts.

### Frame rates and resampling

`framesPerSecond` accepts any integer from **1 through 120** (default 60).
The pinned CEF revision accepts this range without a 60 FPS clamp. This sets its
maximum offscreen paint rate; actual callbacks depend on page activity and host
capacity. Capture timestamps and native drop counts remain attached to frames.
Do not interpret a 120 FPS file as proof of 120 distinct captured frames.

Recording `capture.framesPerSecond` and native broadcast
`stream.framesPerSecond` independently select the constant output rate, accepting
any finite numeric value from 1–120, including 29.97 and 59.94. CEF capture
requests retain their genuine integer ceiling; output is resampled on the native
recording clock. Omitted recording cadence uses each source's requested rate; omitted
broadcast cadence uses the first source's rate and stays fixed across switches.
Pages in the same helper with matching physical dimensions and pixel format can
feed the same broadcast. Cross-helper frame/audio relaying is not implemented.

Recording samples the newest captured image at or before each output tick on a
timeline anchored at the first accepted capture. Faster input drops intermediate
images; slower or idle input holds the previous image. Frames are not blended or
motion-interpolated. Obsolete callbacks cannot replace newer held images.
Encoder backpressure replaces the pending capture rather than growing a queue.
Finalization fills ticks before the end time, without adding an extra tick at
the end boundary. Duration is quantized to output frame intervals.

Native streaming paces output slots against an absolute monotonic timeline,
holding the latest genuine image when input is slower. The 48 kHz audio sample
boundary at video tick `n` is `floor(n * 48000 / outputFPS)`, so fractional rates
emit whole-sample counts without cumulative rounding drift. Pause holds the image
and emits silence while this live timeline continues. Native counters disclose
held images, audio gaps/overlap, pause, and source-switch discards. Excessive
capture-clock or pipe lag fails the publisher rather than silently skipping time.

Final rendering can resample again using `config.output.framesPerSecond` or CLI
`--fps`; known constant-rate sources first recover their frame-index clock to
avoid WebM millisecond timestamp rounding changing selection. Its FFmpeg `fps`
filter then rounds forward to the next output tick, holding/dropping frames
without interpolation. Variable-rate sources retain their captured timing.
Source recording settings and final output settings are separate.

Run `pnpm test:native:cadence` without CEF to decode frame IDs and page-tone audio
through the real recorder/FFmpeg pipeline at 24→120, 120→24, 120→120 and 59→37,
plus final rendering at 120→60. Reports and media go under
`.suitecut/native-cadence-verification/`. This verifies resampling, decoded
pixels and audio continuity; it does not establish real CEF throughput at 120 FPS.

## Recording, narration, and presentations

The same `NativeBrowser` can now feed a recording, a broadcast, or both. Recording
uses native raw frames directly and produces ordinary SuiteCut WebM source clips,
checkpoints, narration artifacts, and a manifest consumable by `renderSuiteCut`.
It shares the existing narration worker, presentation actions, manifest writer,
and renderer.

```ts
import { launchNativeBrowser, recordNative } from 'suitecut/native'
import { renderSuiteCut } from 'suitecut/render'

const source = await launchNativeBrowser({
  executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE!,
  width: 1920,
  height: 1080,
  framesPerSecond: 60,
  pixelFormat: 'i420',
})
try {
  const recording = await recordNative(
    'Native product tour',
    async ({ page, suitecut }) => {
      await page.goto('https://example.com')
      await suitecut.highlight(page.locator('h1'))
      await suitecut.narrate('This tour uses the embedded browser.')
      await suitecut.checkpoint('Opening screen')
    },
    {
      source,
      capture: { audio: true },
      output: { directory: '.suitecut/native-tour', manifestPath: '.suitecut/native-tour.json' },
    },
  )
  await renderSuiteCut({
    manifestPath: recording.manifestPath,
    outputPath: '.suitecut/native-tour.mp4',
    config: { output: { width: 1920, height: 1080, framesPerSecond: 60 } },
  })
} finally {
  await source.close()
}
```

`recordNative(name, callback, options)` requires an already launched, caller-owned
source. It never closes that source or a broadcast using it. `ffmpegPath` is
optional and uses the existing SuiteCut media-tool resolver. Output directory,
manifest path, relative/absolute artifact paths, and audio plugin references use
the native output configuration.

The callback receives `{ source, page, suitecut, signal, addSource }`:

- `suitecut` supports narration/captions, checkpoint clips, holds, highlights,
  rendered zooms, cursor movement, clicks, typing, and scrolling. Narration model
  preparation pauses the recording clock and frame capture; narration playback
  duration remains in the video. This pause does not pause a concurrent broadcast.
- `await addSource(otherSource)` registers another caller-owned browser and returns
  its `NativePage`. Call `suitecut.selectPage(nextPage)` to switch the authored
  timeline. Sources may use different dimensions/formats/FPS; each records its
  own clip and the renderer composes the selected intervals. Selection is explicit;
  this does not enable popup windows in CEF.
- `capture.size` optionally scales recorded video while leaving CSS layout alone.
  `capture.framesPerSecond` optionally resamples source clips to a chosen 1–120 FPS cadence.
  `capture.narrationTailMs` controls the pause after speech. Set CSS dimensions,
  device scale, and capture FPS when launching the source. Unsupported capture
  options are rejected instead of silently ignored.
- `capture.audio` is a boolean and defaults to `false`. Set it to `true` to put
  48 kHz stereo page audio into each native source WebM. The final renderer follows
  authored page selection, inserts silence for holds or sources without audio, and
  mixes page audio with narration. `sourceAudioEnabled`, `sourceAudioVolume`, and
  `narrationVolume` provide final-render controls.
- The native `page` supports `goto`, `evaluate`, `locator`, waits, and private
  DevTools commands. `createNativePage(source)` exposes these controls without
  starting a recording. Evaluation functions must be self-contained and arguments
  and returned values JSON-serializable; there are no remote JS handles.
- Native locators use **strict CSS selectors in the main document**. They check
  uniqueness and dispatch input through Chromium. They do not provide role/text selectors, iframe/shadow-root traversal, automatic retries, or full
  actionability checks. Await application readiness explicitly.
- Recording observes source/encoder failures and cancellation, finalizes media
  when possible, and writes a failed manifest for callback errors. Sources remain
  caller-owned. Finalization permits up to 60 seconds for the encoder; it does not
  use streaming's five-second shutdown budget.

With `capture.audio: true`, recorded source clips contain Opus page audio alongside
VP9 video. Audio uses the same pause-adjusted recording clock as video, is trimmed
with the selected source intervals, and is mixed with synthesized narration during
rendering. Authored zoom is applied by the final renderer, not by changing the
native page's viewport. Concurrent broadcasts receive browser-rendered effects
and captions, but not the recording renderer's zoom transform or narration mix.

Run `pnpm test:native:recording` with `SUITECUT_NATIVE_EXECUTABLE` set. The verifier
records native input and presentation actions, excludes deliberately slow audio
preparation, switches sources, renders the manifest, decodes output audio, and
checks source-specific page tones, narration mixing, the default audio-off path,
and callback-failure/cancellation cleanup. Its narration fixture is a
repeatable tone that checks timing and mixing rather than speech-model quality.

## Memory and backpressure

CEF callbacks never wait for pipe writes. The native writer retains one pending
video frame, up to 100 audio packets, and up to 128 control packets. A stalled
consumer causes video replacement and old audio eviction. Node retains only the
latest source frame unless the caller retains more. Commands are capped at 64 KiB
and 64 outstanding requests; results at 1 MiB; packet headers at 16 KiB.

The native publisher retains a bounded queue of genuine timestamped pictures for
causal selection, including held and in-flight images. Picture payloads are capped
at 256 MiB per publisher and 1 GiB per Engine. Audio-enabled output defaults to a
120 ms capture buffer and independent 20 ms PCM scheduling; complete video slots
add their interval closure delay. PCM is capped at 30 seconds per publisher.
Original startup/stop deadlines and bounded pipe lag limit stalled encoders, and
overflow fails explicitly. At most four physical publishers are active. CEF
surfaces, FFmpeg memory and separately retained SDK frames are additional.
GPU texture sharing remains unimplemented.

## Native screenshot comparison slice

The ed097480 candidate adds `expectNative(page).toHaveScreenshot(pathOrPNG,
options)` and the locator equivalent. Native C++ owns accepted-paint gates,
consecutive decoded-RGBA stability, comparison retries, the original deadline,
and image/job retirement. Expected baselines must be explicit PNG paths or
Buffers; named runner baselines and update policies remain unfinished.

Comparison uses SuiteCut's normalized Oklab distance with alpha composited over
white, not an equivalence claim for another comparator's threshold or antialias
policy. Both dimensions must agree. When both pixel and ratio limits are given,
the stricter limit applies. Native image admission is bounded to2MiB encoded PNG,
16384 per dimension,2×1024×1024 pixels and64MiB retained comparison reservations.
Negation uses the native loop; timeout0 disables ordinary logical expiration.

A changing page can time out without two equal captures. Its diagnostics retain
the last fully admitted pre-deadline actual PNG and the difference from the
explicit baseline, while reporting `stable:false`. Difference generation shares
that capture's bounded native pixel job; timeout starts no additional capture or
diagnostic job. Late CPU/UI completion cannot replace the published pair, and
cancellation retains physical image ownership until the actual UI release. The
existing oversized difference-canvas limit still applies.

The d0def75c candidate accepts PNG Buffers through bounded24KiB chunks and
readonly native file paths. Native preparation owns the original assertion
deadline across configuration, chunks, actual file I/O, capture and comparison;
client nonces permit cleanup after a lost preparation reply. The64KiB wire-command
limit is preserved. Logical staging is bounded16; actual file/image work shares
a2-worker admission cap and64MiB retained-input/comparison budget. Path workers
check a genuine regular file, size and identity before/after reading, using
O_NONBLOCK and CLOEXEC. Caller cancellation retires logical work while any real
file/capture callback retains physical ownership until it settles.

Actual public checks pass7 input and8 screenshot groups, including232989-byte
PNG Buffer/path input and130KiB style configuration. Test-only pass-through syscall
gates establish4 public held-file cases and shared2-worker queue/deadline behavior.
The original authored wrong-transport counter check and its corrected checker are
retained separately. See the hashed d0 scope index in AUDIT.md. A following SDK fix
separates public failure publication from queued cleanup while retaining its
canonical staging slot; controlled sender proofs are distinct from actual CEF
admission. Ordinary screenshot/PDF large inline configuration remains separate
work. Named baselines/update policies and offscreen OOPIF full-page capture retain
their documented gaps.

## IPC protocol v1

The executable reads newline-delimited UTF-8 JSON on stdin (64 KiB per command).
Every command has `version: 1`, a positive integer `id`, and `command`:

```json
{"version":1,"id":1,"command":"navigate","url":"https://example.com"}
{"version":1,"id":2,"command":"devtools","method":"Runtime.evaluate","params":{"expression":"document.title","returnByValue":true}}
```

EOF requests graceful browser shutdown. Stdout is exclusively binary IPC:
four-byte little-endian JSON-header length, header bytes, then exactly `bytes`
payload bytes. All headers include `version`, `type`, and `bytes`.

| Type       | Header fields                                              | Payload                                        |
| ---------- | ---------------------------------------------------------- | ---------------------------------------------- |
| `ready`    | `timestampMs`, `cefVersion`                                | none                                           |
| `frame`    | `timestampMs`, `width`, `height`, `pixelFormat`, `dropped` | tightly packed BGRA8 or contiguous I420 planes |
| `audio`    | `timestampMs`, `frames`                                    | interleaved stereo PCM s16le                   |
| `response` | `id`, `ok`, optional `error`                               | optional JSON DevTools result                  |
| `error`    | `error`                                                    | none; source is no longer reliable             |

Native timestamps are milliseconds from process start. Audio presentation time
is anchored against callback receipt time. Unknown versions and malformed
lengths are fatal; consumers must not attempt to resynchronize a corrupt stream.
Stderr is drained without forwarding page URLs or browser diagnostics into reports.

## Open-source distribution

SuiteCut's source is MIT licensed. CEF and Chromium retain their own licenses and
third-party notices. SDK binaries are not committed or included in the npm package.
libyuv retains its BSD-3-Clause license. Keep `CEF-LICENSE.txt`, `CEF-CREDITS.html`,
and `libyuv-LICENSE.txt` in any native binary distribution.
See [AUDIT.md](AUDIT.md) for validation evidence and release limitations.

The HTTP/1 surplus parser examines only raw incoming bytes actually observed
past an agreed Content-Length, with no transfer encoding or bodyless method or
status. The native libcurl framing length/version must agree; folded/ambiguous
headers disable that parser. A real parser failure aborts through curl's native
write/progress callbacks so its connection cannot enter the idle pool. It does
not wait for unseen bytes, log raw messages, or relabel orderly EOF as reset.
Header continuations extend only their actual preceding field; derived cookie
metadata commits complete field values after the real header section ends.

The browser-control integration at `bf480072` built against pinned CEF156 and
passed a bounded live scope for activation, mobile capture restoration, locator
handlers, idle emulation, before-unload and native context video/file lifecycle.
Ordinary MIDI overrides now reject explicitly: that content-setting type is not
registered in this CEF build. See [the active backlog](../../docs/native-browser-active-backlog.md)
for exact remaining implementation and verification boundaries. Browser builds
must use `-DSUITECUT_BUILD_TEST_RUNNER=OFF` when reusing an older cache.

### Consolidated browser SDK build

The focused public SDK entry is `suitecut/browser`; see
[the browser SDK guide](../../docs/native-browser-sdk.md). The 17 October source
pass adds launch-context video, dynamic viewport fitting and odd video canvases,
retained-node locator handlers, delayed HAR header finalization, and native packet
write/clock-pause optimizations. The native browser target and matching SDK
compiled and are installed together from `746bdb7`. Dynamic-library resolution
and the public SDK import pass. E2E and fuzz verification remain deferred; this
build does not complete the remaining browser feature gaps. Build details are in
`.suitecut/diagnostics/native-browser-build.json`.

Default native builds leave component-check executables out of `all`. Explicit
check targets still work; `-DSUITECUT_BUILD_BROWSER_CHECKS=ON` includes them in
the default build. Keep `-DSUITECUT_BUILD_TEST_RUNNER=OFF` for browser-only work.

The current stock-CEF pass adds audited native handler callback I/O and native
snapshot-reference registrations, permission-scope fixes and native-reported
`browser.capabilities()` metadata. Custom-library candidates remain separately
[isolated](cef-patches/README.md); ordinary builds do not apply or activate them.
The current consolidated build/installation result remains
`.suitecut/diagnostics/native-browser-build.json`; broad E2E/fuzz work is deferred.

The current implementation pass adds version-probed root-target WebSocket
control, native file-mutation receipts/serialization, precise credential-import
identity and retained-coverage interval/source helpers. Backend-dependent
activation remains explicit; see `docs/native-browser-active-backlog.md` and
`docs/cef-issues/README.md`. Compilation is not full runtime parity.

The controller completion adds page/context WebSocket URL callbacks and target
inheritance, bounded 2 MiB message staging, production retained-coverage lifecycle
coordination, explicit original-widget mouse receipts, exact clock-document
ownership, deduplicated acknowledged credential synchronization, and handler-driven
recovery of another failed child page. Each optional backend is enabled only by
its actual protocol contract. The stock library remains unchanged. See the current
completion section in the active backlog for backend dependencies and the build
receipt for the installed source revision.

## Installed controller completion build

The native browser and matching browser-only SDK compiled and installed from
`9e7ad4180a259d722c2fd1c0b89ab1e3d4985ffa`. All 976 selected source inputs stayed
unchanged during compilation. The emitted SDK contains 226 files; its 43 runtime
exports and page/context WebSocket APIs import successfully. Dynamic libraries
resolve. Native and SDK build supervisors retired their descendants normally.

The installed binary SHA-256 is
`1bd2b22c7f16656f92ac923b1433aa2f7d96357962476b54aca52553c43653f1`.
The authoritative receipt is `.suitecut/diagnostics/native-browser-build.json`.
This supersedes the earlier installed-build entries above. Custom CEF compilation
and broad E2E/fuzz remain deferred; prepared upstream reports remain unpublished
because the GitHub integration returned HTTP 403.

## Automatic background workers

The engine sizes its shared background worker pool from usable CPU capacity
and effective memory limits. The default CPU share is half the usable capacity,
with at least one worker. A stack-reservation allowance of 1/64 of detected
memory can reduce the worker target further, using the platform's default
thread stack size. At least one worker remains available on tight limits.
These are explicit default policies, not a guarantee of optimal throughput.
There is no fixed four-worker ceiling. No SDK thread-count setting is required.

Linux queries affinity and ancestor cgroup v1/v2 CPU and memory limits. macOS
queries active CPUs, RAM, and pthread defaults. The Windows policy queries CPU
groups/affinity, queryable job CPU/memory limits, physical RAM, and the executable
stack reservation. Unavailable queries fall back to available platform data.
Configured limits are sampled at pool construction; this is not a live resource
pressure controller.

Workers start only when jobs arrive and are reused across subsequent jobs.
The admission bound is four jobs per worker, including running and queued work.
Thus a four-CPU, 16-GiB environment still uses two workers and eight accepted
jobs, while larger environments can scale beyond those numbers. Thread creation
failure preserves accepted work on existing workers; failure to create even the
first worker rejects admission. Diagnostics count thread-start failures.
Shutdown drains accepted work and releases captured owners before retiring
physical reservations. Screenshot processing, HAR files, trace previews and
sources, and context video preparation already submit through this shared pool.
Dedicated transport, media, and Chromium threads retain their own lifetimes;
the shared pool limit is not a cap on the complete Chromium process tree.
The stack allowance does not bound task buffers or total browser RSS. Existing
feature-specific byte budgets remain in force, and exhausted admission returns
a capacity error rather than growing without a bound.

Diagnostics expose `nativeTraceBackgroundWorkerLimit` and
`nativeTraceBackgroundWorkers`, plus `nativeTraceBackgroundCapacity` and
`nativeTraceBackgroundWorkerStartFailures`. The independent-read window example
is optional application-level concurrency and does not control these threads.

The standalone `native/resource-policy` CMake project checks this component
without CEF. Its CI workflow covers Linux, Windows, and macOS; a prepared
workflow is not evidence that those remote jobs have run. Resource-policy
portability does not implement missing browser backend support: Windows CEF
packaging/sandbox and the native client's Windows transport remain unimplemented.
Linux runtime checks do not establish complete macOS application support.
