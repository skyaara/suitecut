# CEF 154 promotion and mandatory native audio readiness

Measured on 2026-10-09 UTC. This report supersedes the selectable startup policy
in the earlier sealed [version comparison](../2026-10-10/report.md).

## Result

SuiteCut now pins **CEF 154.0.34+g14c5a08 / Chromium 154.0.8037.98** for Linux x64
and macOS ARM64. The ordinary Linux `.suitecut/cef-sdk` and native build use 154.
The older SDK is retained under `.suitecut/cef-sdk-152.0.8`. CMake checks the
platform-specific pin and rejects a mismatching SDK. A clean rebuild of all
265 native build steps avoided reusing the earlier CEF wrapper. The Linux
runtime reports the expected version. The macOS archive was downloaded and its
size, upstream SHA-1, and SHA-256 verified; macOS execution remains untested.

Realtime audio-enabled recording and RTMP streaming always wait for video and
native source sound in C++. Their timeline starts at the first available sample
above 32 S16 magnitude in either channel, no earlier than the earliest buffered
video. There is no immediate or packet-only alternative. Both TypeScript and
native IPC reject the removed `audioStartup` field. The SDK requires the helper
to confirm mandatory readiness and stops outputs from incompatible helpers.
Native IPC defaults omitted `audio` to true; malformed flags are rejected.

CEF initialization, an AudioContext reporting `running`, and actual captured
sound are different states. Activate sound before or concurrently with the
pending output. Audio-enabled silent pages fail explicitly after the readiness
timeout: 5000 ms by default, configurable from 1–60000 ms after output setup.
Intentionally silent flows use `audio: false` and produce video-only output.
Offline file rendering retains intentional source silence and does not wait for
live CEF capture. Stop interrupts the native wait. Later silence and timeline
gaps are retained; the gate does not collapse them.

```ts
await page.getByRole('button', { name: 'Start audio' }).click()
const recording = await engine.record(page, { destination: '/tmp/flow.mp4' })
// No startup policy field. The same rule applies to engine.stream().
```

## Controlled default-policy captures

All cases use a fresh CEF process/profile, 1920×1080 at 30 fps, H.264, and native
48 kHz stereo audio. The three cold cases run for 6 seconds; quiet and stress
run for 20 seconds. Scroll commands exercise the page during capture. The stress
case includes a 4-second busy loop in Node. No builds, checks, other SuiteCut
captures, or analysis processes ran during this controlled matrix. The isolated
X display and Pulse sink are shared across its five cases; “cold” means a fresh
browser, not a cold physical audio device or cleared global driver caches.

| Case | Ready wait ms | Native first sound ms | Decoded first sound ms | Largest interior silence ms | Native CPU cores | Changed-frame fps | Largest image hold ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| cold-signal-pcm | 778.6 | 0.00 | 0.00 | 0 | 0.533 | 24.26 | 66.7 |
| cold-signal-aac | 166.6 | 0.00 | 6.31 | 0 | 0.552 | 30.00 | 33.3 |
| cold-signal-aac-mp4 | 115.7 | 0.00 | 15.71 | 0 | 0.569 | 30.00 | 33.3 |
| quiet-signal-aac | 211.7 | 0.00 | 5.42 | 30 | 0.542 | 29.95 | 66.7 |
| stress-signal-aac | 170.0 | 0.00 | 13.27 | 10 | 0.601 | 29.90 | 66.7 |

Every native PCM trace starts with sound at sample zero. PCM decoding matches the
pre-encoder bytes exactly. AAC first detectable sound is 5.42–15.71 ms in these
captures; the native gate cannot remove lossy codec onset behavior. The decoded
video frame counts equal the native counters in all five cases. The ordinary
recording/streaming calls use no startup-policy option.

The quiet case has 40 ms of steady-state uncovered input, matching a 40 ms hole
in the CEF source timeline. Its decoded interior silence spans 30 ms under the
10 ms RMS window detector. Stress has 20 ms uncovered source input and 10 ms
interior decoded silence. All five cases have zero mismatched timely source
samples and zero late-sample counters. This verifies correct copying of available
PCM, not uninterrupted CEF delivery or a universal explanation for every gap.

Ready wait moves capture startup ahead of the output timeline. It does not make
CEF startup instantaneous. These are single serial observations, not a statistically
reliable speed comparison between browser versions or against OBS.

## Simultaneous recording and RTMP

The initial 60-second simultaneous run completed 1818 frames per output, ten
scroll commands, a four-second parked Node thread around second 40, and clean
finalization with empty queues and zero queue-overflow/late counters. Both files
fully decode. Its sound fixture schedules 250 ms tones every second, so ordinary
750 ms silence is intentional.

However, it also contains an **unexpected 7730 ms decoded silence span**, source
callback delay up to 1565 ms, and source timeline discontinuities up to 4267 ms.
Both outputs have approximately 19687.52 ms of steady-state uncovered source
input; available PCM is copied exactly, with zero mismatched frames. Encode
operations stall for 1944 ms (recording) and 2049 ms (streaming). The host reports
151 throttled CPU periods and 28.12 cumulative CPU seconds throttled during this
run. Timing correlation alone does not prove throttling causes all source gaps.
The lifecycle harness's `passed` result does **not** mean gap-free media.

Older OBS/browser entries observed after the run are zombies, not live CPU
consumers. They were not killed. A separate continuous-tone simultaneous run
is recorded to distinguish planned silence from audio loss; its independently measured results
are recorded below.

### Continuous-tone repeat

The 60-second repeat uses a continuous 440 Hz tone while retaining the same
1920×1080 recording/RTMP workload, scrolling, visual overlay, and blocked Node
thread. Both outputs start with native sound at sample zero, fully decode, and
have **zero interior decoded silence and zero steady-state missing input**.
Available source samples are copied exactly, with zero mismatched, late, and
queue-overflow frames. Small total missing counters at the final partial output
edge (6.67/8.00 ms) are not interior gaps. The CPU cgroup reports zero throttling.

| Output | Video frames | Duration ms | Native first sound ms | Decoded first sound ms | Steady-state missing ms | Maximum encode ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Recording | 1818 | 60600.0 | 0 | 4.15 | 0.00 | 54.49 |
| RTMP | 1818 | 60600.0 | 0 | 3.48 | 0.00 | 55.09 |

This clean repeat establishes that continuous-tone simultaneous capture works
on this host in this observation. It does not establish that intermittent sound
is itself the cause of the earlier failures: the runs were serial, browser/audio
state and caches vary, and there is one observation per pattern. The earlier
7.73-second gap remains an unresolved reliability issue. Removing optional
startup policies fixes the start of the timeline, not those mid-run failures.


### Resource observations

Aggregate native/CEF RSS (which double-counts shared mappings) peaks at
1256.2 MiB in the intermittent run and
1278.2 MiB in the continuous run. Mean native-tree
CPU is 1.823 and 1.038 cores,
respectively. The continuous run's Node heap after cleanup/GC is
49.1 MiB, with
0.33 MiB of array buffers. These
short-run RSS observations include allocator/cache warmup and cannot establish
long-term leak freedom. Exact samples and counters are in `resource-summary.json`
and the process/node JSONL files.

## Validation and limitations

- **18 complete media files decoded without errors**: ten controlled/calibration
  matrix files, four simultaneous recording/receiver files, and four final
  Playwright recording/stream/video-only files.
- SDK: **309 tests passed** in 37 test files; final type checking and scoped lint pass.
- Native CTest: **5/5 passed**, including capture/render, twelve codec pairings,
  audio-clock conversion, pixel conversion, and mandatory audio readiness.
- Native startup tests verify no startup from video or silent PCM, exact first
  samples with early/late video, explicit timeout, interruptible stop, video-only
  readiness, and intentional silent offline input.
- Address/undefined-behavior/leak sanitizers report no finding for the instrumented
  SuiteCut startup test. Linked third-party libraries/CEF are not fully instrumented;
  this short test does not prove the complete application leak-free over long runs.
- **10/10 real CEF/Playwright cases pass**, including delayed sound for recording
  and actual RTMP, no-sound timeout and reuse, native bypass rejection and omitted
  audio defaults, locators/input/scroll/iframe/screenshot, request interception,
  storage/context isolation, popup/dialog/upload/download/CDP, recording during
  blocked Node, pause/page switching, repeated closure, permissions/geolocation.
  This is not the full upstream Playwright conformance suite.
- Linux software rendering on a host limited to four CPU equivalents and 16 GiB.
  No GPU device is exposed. GPU encoding and this CEF upgrade on macOS are unverified.
- No new OBS comparison or 30-minute capture was performed for this change. The
  older sealed evidence remains available; the startup fix does not establish
  production readiness or superiority over OBS.

Initial failures and a calibration matrix are retained. The first calibration
startup overlapped type/lint checks and is excluded from the controlled table;
that matrix also contains cold AAC decoded silence spans of 370–410 ms and is
not hidden. The RTMP test initially used a fixed receiver delay and occasionally
hit connection refusal; it now checks for a listening socket without consuming
the RTMP connection. Native timeout status is a successful status reply carrying
an error, while `media.stop` reports the timeout as a failed command; the test
harness now checks those distinct responses correctly. Final complete tests pass.

## Evidence and replay

`capture/` and `capture-initial/` contain source/encoder PCM and timing CSVs,
recordings, native counters, process samples, full-decode validation, source gap
attribution, and CPU/video measurements. `playwright-final/` holds the final
functional results and media. Failed/earlier test outputs remain separate.
`simultaneous*/` hold actual receiver media, source traces, node/process profiles,
and complete result records. Stream trace aliases map `native.*` (the RTMP key's
basename) to `receiver.flv.*` for the independent analyzer.

`tested-sources/`, `tested-sdk/`, `tested-binaries/`, build/linkage metadata,
exact platform pins and dependency hashes make the result reviewable. The combined runtime stderr may include earlier appended entries; per-case results
identify the measured helper and CEF version. Runtime
CEF libraries remain external due to size; obtain the checksum-pinned SDK and
keep its libraries/resources beside the helper. Copying only an executable is
insufficient. The source snapshots preserve the exact final build and replay
scripts. `checksums.sha256` seals every payload file; a separate archive checksum
and manifest accompany the shared archive. Earlier sealed evidence is verified
without modifications.

```sh
pnpm build
ctest --test-dir .suitecut/native-build --output-on-failure

SUITECUT_ENGINE_E2E_DIRECTORY=/absolute/new/evidence/playwright \
  python3 benchmarks/native-engine/runtime.py playwright

STARTUP_DIRECTORY=/absolute/new/evidence/capture \
  python3 benchmarks/native-engine/runtime.py benchmarks/native-startup/matrix.mjs
mkdir -p /absolute/new/evidence/capture/logs
python3 benchmarks/native-startup/analyze.py /absolute/new/evidence/capture

SOAK_DIRECTORY=/absolute/new/evidence SOAK_LABEL=continuous \
  SOAK_CONTINUOUS_AUDIO=1 SOAK_DURATION_MS=60000 \
  SUITECUT_AUDIO_TRACE_DIRECTORY=/absolute/new/evidence/continuous \
  python3 benchmarks/native-engine/runtime.py benchmarks/native-engine/soak.mjs
```

Run capture commands serially. The runtime owns and cleans its private X/Pulse
servers, preserves the Chromium sandbox, and uses a single-capture lock. To test
an alternate matching build, set `SUITECUT_RUNTIME_EXECUTABLE` to its absolute
helper path. Restore the snapshotted sources to reproduce this version rather
than mixing them with a future SDK/protocol.
