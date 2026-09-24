# Native browser implementation audit

Date: 2026-09-24. Scope: the optional native backend, its public Node/IPC APIs,
native packaging, and changes to the shared live publisher. This is an internal
implementation review with executable tests, not an independent security audit.

## Release disposition

Suitable for open-source review as an **experimental, opt-in backend**. The
Playwright backend remains the default. No npm package, native binary, website,
or production stream was published or migrated by this work.

macOS ARM64 is the verified platform. Linux has a CMake branch but has not been
built or exercised here. Windows configuration explicitly rejects the build until
its sandbox and packaging are implemented. Do not advertise a cross-platform
native release yet.

## Architecture reviewed

- CEF owns a sandboxed, offscreen browser source. Native paint callbacks supply
  BGRA pixels; native audio callbacks supply stereo PCM. libyuv optionally
  converts BGRA to BT.709 limited-range I420 before IPC.
- Node exposes launch, navigation, DevTools commands, frame/audio subscriptions,
  lifecycle state, failure/closure promises, and idempotent shutdown.
- Broadcasts accept either pixel format, preserve capture timestamps, and reuse
  the existing persistent publisher, audio pump, bitrate control, and reconnect
  supervision. Source replacement does not restart the publisher.
- Recording, narration, and presentation workflows are also available through
  `recordNative`, with shared timeline actions, opt-in page audio, narration,
  artifacts, and rendering.
  Existing Playwright recording and test APIs keep their capture implementation. The native API is not a Playwright Page
  adapter and makes no compatibility claim for the full Playwright protocol.

## Issues found and resolved

1. **Shared default CEF session:** two sources could collide with Chromium's
   process singleton. Every source now gets a private temporary root profile;
   cleanup follows child closure. No user browser profile is selected.
2. **Console output corrupting media:** Chromium can print to stdout. The native
   writer now uses a private duplicate descriptor marked close-on-exec, while
   ordinary stdout is redirected to stderr. Protocol validation fails closed.
3. **Initial navigation race:** the initial blank document's load-end could
   resolve the first requested navigation. The handshake now waits for the blank
   document's load-end before accepting commands.
4. **Audio clock origin mismatch:** this CEF build supplied monotonic PTS, despite
   the header describing Unix timestamps. Epoch subtraction produced stale audio
   and silent output. Audio now anchors its first PTS to the native receipt clock,
   preserves subsequent deltas, and re-anchors after large discontinuities.
5. **Raw BGRA throughput:** the 1080p60 BGRA benchmark observed only about 36
   captured/29 delivered FPS, 318 replaced frames, and 337 ms p95 A/V offset.
   The I420 path reduced IPC bytes by 62.5% and passed the final stream benchmark.
   BGRA remains available for exact pixel capture; it is not the recommended
   high-frame-rate broadcast path.
6. **Raw frame retention:** the previous compressed-frame byte cap was too small
   for the audio-delay window at high raw resolutions. Raw input now has a fixed
   format/size-dependent retention cap. FFmpeg's raw input queue is two frames.
7. **Malformed result cleanup:** command response JSON is parsed before removing
   its pending request, so a corrupt result cannot leave a promise unresolved.
8. **Shutdown reentrancy:** broadcast shutdown removes its abort listener before
   aborting its own controller. Repeated stop calls share one shutdown promise.
9. **Operator UI interruptions:** modal JavaScript dialogs/context menus are
   suppressed; downloads and new browser popup windows are rejected. Native
   select popups are composited into the frame.

## Validation evidence

Artifacts live under `.suitecut/native-verification/`,
`.suitecut/native-recording-verification/`, and
`.suitecut/native-stream-verification/`, excluded from the npm package and Git.

- CEF 152.0.8 / Chromium 152.0.7977.134 compiled on macOS ARM64 with renderer
  sandboxing enabled. The complete framework and helper bundles are assembled by
  CMake. No `--no-sandbox` or certificate bypass is supplied.
- Native CTest checks pass for both epoch/monotonic audio origins, callback jitter,
  clock discontinuities, and BGRA-to-I420 color/range/channel ordering.
- Native browser integration passes for rendered output, audible PCM, native
  mouse input, dialog suppression, navigation errors/recovery, 2x scaling,
  populated 3840x2160 frames, cookie isolation between sources, deliberate
  renderer crashes, and idempotent shutdown. The captured PNG was inspected.
- The latest I420 1080p60 RTMP integration run measured **59.99 captured FPS,
  60.03 delivered FPS, no replaced native frames, 40 ms p95 encoded A/V offset, and 11 matched
  flashes**. It decoded received video and audio, replaced a deliberately crashed
  source, and restarted the local RTMP receiver to verify reconnection. Its steady
  capture measurement covers 15 seconds, not a long-duration soak.
- The complete TypeScript unit suite passed **291 tests across 37 files** with
  two workers.
- New regression tests cover fragmented/coalesced/truncated IPC, size limits,
  I420 dimensions, immutable delivered buffers, malformed command results,
  abnormal process exit, source failure, source switching, raw FFmpeg input,
  command bounds, closed subscriptions, and audio discontinuities.
- The existing Playwright stream integration passed visual effects, idle pacing,
  receiver restart, and page switching. Its encoded tab-audio checks measured
  74 ms and 66 ms p95 A/V offsets, with no unmatched flashes. An initial zoom-pixel
  check failed; comparison against the original publisher and a rerun of the
  changed publisher both passed without changing that check.
- TypeScript build/typecheck and focused ESLint checks pass. The npm dry-run
  includes the `suitecut/native` export and native source files and excludes CEF
  SDKs, browser profiles, native build outputs, and test media. The packed-package
  consumer smoke test passed for SuiteCut and all eight split audio packages,
  including both native entry points, Playwright Test, standalone Playwright,
  and a VITS-only installation.
- A macOS ARM64 GitHub Actions workflow builds against checksum-verified pinned
  CEF/libyuv archives and runs the native checks. The workflow has been reviewed
  locally; no hosted CI run is claimed.

## Security and resource boundaries

- Commands are private process IPC. No HTTP control server or remote debugging
  listener is opened for CEF. DevTools commands are a trusted operator capability,
  not an API exposed to page JavaScript.
- There is no shell command construction for launching a source. Executables
  must be absolute paths without null bytes. Temporary profile directories are
  created by the launcher and removed on closure.
- IPC version, header length, payload length, dimensions, pixel format, audio
  length, and finite timestamps are validated before delivery. Parser allocation
  is bounded; it does not repeatedly concatenate growing frame buffers.
- Native retention is bounded to one pending video frame, 100 audio packets, and
  128 control packets. Node limits commands to 64 KiB, 64 pending requests, and
  30-second deadlines. Recording retains at most 30 seconds of PCM while waiting
  for its first video frame. Native result payloads are limited to 1 MiB.
- Browser diagnostics are drained without writing page URLs, tokens, or browser
  messages into reports. No page-to-Node bindings or credential export are added.
- CEF and libyuv revisions/archive hashes are pinned in adjacent manifests.
  Builds use explicitly supplied SDK/source directories. Binary distribution
  must retain CEF/Chromium and libyuv notices.

## Remaining release gates

- A 24-hour soak with bounded RSS, clock drift checks, renderer/GPU failures,
  repeated navigation, and sustained congestion on each supported platform.
- Windows implementation; Linux runtime validation; macOS x64 validation;
  signed/notarized native artifacts and a maintained Chromium update process.
- Sustained 4K60 streaming and GPU shared-texture integration. A 4K frame is not
  evidence of 4K60 throughput. This implementation still copies CPU buffers.
- Broader site compatibility: WebGL, media codecs, fonts, popups, downloads,
  authentication flows, color management, and a deliberate Playwright bridge.
- Native audio clock anchoring is a receipt-time estimate; source startup and
  clock discontinuities can shift it. The short encoded A/V measurement does not
  prove long-term drift is absent.
- The caller owns fallback/restart policy. A failed source exposes `failure`;
  its broadcast holds the last frame and supplies silence until the caller
  selects a replacement or stops. Automatic restart is not implemented.

These gates are reasons to keep this backend opt-in, not reasons to disable
validation, weaken the sandbox, or replace the current production stream now.

## Recording integration follow-up

The browser-control subset used by narration/presentation is now independent of
Playwright. Typed native pages and strict CSS locators implement that subset;
there are no casts pretending a CEF source is a Playwright Page. A native recording
session writes standard source-video/checkpoint artifacts, and both backends use
the same manifest construction, narration worker, presentation actions, and final
renderer. There is no manifest schema fork.

The raw recorder retains an in-flight frame, a held frame, and one replaceable
pending frame, repeats video to preserve duration, and permits 60-second encoder
finalization. Synthesis preparation is excluded from the recording clock.
A cached frame from a newly registered source is anchored to its registration
time, so it cannot incorrectly place a late source at the start of the recording.
First-frame epoch time and pause-adjusted source time are stored separately.

Recording sources are caller-owned and may also feed a broadcast. Recording
selection and preparation pauses affect only its timeline; they do not switch or
pause another consumer. Callback errors produce a failed manifest when media can
be finalized. Encoder/source failures are observable and are never promoted to a
successful recording. Cancellation rejects authored waits and prevents subsequent input requests; a
DevTools command already dispatched to CEF cannot be recalled.

Native locator support is deliberately narrower than Playwright: main-document
CSS selectors, unique matches, basic visibility/hit testing, and Chromium input.
There is no full Playwright adapter or automatic actionability/retry contract.
Recording and streaming audio use explicit boolean flags and default to disabled.
The shared Playwright stream parser retains the old string values as migration
aliases, while the native API rejects them. When recording audio is enabled, bounded PCM is aligned to the
pause-adjusted video clock, encoded as Opus in each source WebM, trimmed according
to page selection, and mixed with narration by the shared renderer. Sources with
no audio and authored holds contribute silence. These limits and controls are
documented in the public guide.

Validation includes raw-frame backpressure/cadence, idle finalization, encoder
failure, finalization beyond five seconds, nested preparation pauses, cached
late-source timestamps, and caller ownership. The real CEF integration records
I420 and BGRA sources, native input, captions/highlights/zoom, scrolling, two
checkpoints, source switching, delayed narration, and a 60 FPS rendered MP4.
Decoded output audio is checked for source-specific 660/880 Hz selection and a
440 Hz narration mix. A separate successful recording confirms omitted audio stays
video-only by default. The rendered caption frame is extracted, and the test also
exercises callback failure and cancellation. Deterministic tones test timing and
mixing; they are not evidence of speech-model quality.

Follow-up validation: **291 unit tests across 37 files** passed, plus all **nine
existing Chromium presentation/checkpoint/popup integration tests**. The native
recording integration rendered 960x540 at 60 FPS using two sources, produced two
checkpoint clips, verified page-audio selection and narration mixing, and excluded
about 2.1 seconds of deliberately slow synthesis preparation. Callback-failure
and cancellation runs wrote failed manifests and left caller-owned sources open.
A regression test also verifies stopping during the first blocked encoder write
preserves the final held duration.

The final packed-package consumer checks also passed with audio-enabled
`recordNative` and `createNativePage` imported from the installed tarball. Build, typecheck, focused
ESLint, formatting, and whitespace checks passed. Hosted native CI has not run.

## Native-default beta and benchmark

`suitecut/beta` defaults to native I420 capture, owns its primary recording browser,
and reports the chosen backend. Playwright is an explicit typed fallback; missing
CEF never triggers silent substitution. The local `1.2.0-beta.0` tarball redirects
its root import to this beta API and retains stable entry points. The checkout's
stable version/root export remain unchanged. Packaging does not publish anything.

A new paired benchmark runs an identical local canvas and decodes binary frame
IDs from source WebM output. It checks actual distinct frames, not merely encoded
FPS metadata. One warmup per backend is excluded, trial order alternates, and each
run uses an isolated process. Reports retain browser/host versions, configuration,
clips, manifests, repetitions, startup/finalization, file size, and sampled
process-tree resource data.

The benchmark exposed an idle-pump defect: a timer inserted repeated native frames
while capture was active. The pump now extends video only after capture has been
idle for over 100 ms. A regression test ensures active frames are not displaced.
The decoder also excludes the blank pre-navigation capture before interpreting
fixture frame IDs, while still rejecting invalid/nonmonotonic IDs inside the
measured sequence.

Local macOS ARM64 results: three six-second trials per backend at 1920x1080/60,
plus excluded warmups. Median native output was **60.0 distinct FPS, 0.0% repeated
frames, 1200 MiB peak sampled process-tree RSS, and 17.1 sampled CPU seconds**.
Playwright measured **57.7 FPS, 3.9%, 827 MiB, and 10.5 CPU seconds** respectively.
Native improved frame cadence on this fixture but consumed more sampled resources.
These are complete pipeline comparisons with different Chromium builds/input
formats/encoder thread budgets, not a controlled browser-engine microbenchmark.
RSS sums may double-count shared pages; CPU sampling may miss short-lived children
and includes output verification; GPU memory is unmeasured. No streaming or
long-duration performance conclusion follows from these short recording trials.

Validation: **291 unit tests in 37 files**, typecheck, and lint passed. An isolated
consumer installed the beta tarball and verified the native root default, stable
fallback exports, real CEF recording, and owned-browser closure. Nothing was
published. The beta guide documents entry points, build/install commands, migration
limits, benchmark commands, methodology, and remaining release gates.

After the idle-pump correction, native recording/presentation/narration integration
and the stable package-consumer suite passed again. Interrupting the benchmark
was exercised with live child processes; sampled worker/browser/encoder processes
all exited. Native CI includes beta packaging/consumer verification but has not
been executed on hosted runners in this session.
