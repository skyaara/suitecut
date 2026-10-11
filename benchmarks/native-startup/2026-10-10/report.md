# CEF version comparison and native audio readiness

Collected 2026-10-10 in Asia/Kolkata (2026-10-09 UTC in raw logs). Previous sealed
engine, codec, and audio-gap evidence remains unchanged.

## Result

**The startup-silence mechanism is fixable, and native audio-readiness gating is
implemented and verified.** Recording/streaming can wait for the first captured
packet or the first qualifying sound sample before starting their shared native
timeline. The source still needs time to initialize: gating moves that wait
before the recorded timeline, rather than making initialization instantaneous.

The original CEF 152.0.8, newer 152.0.13 patch, and CEF 154.0.34 stable all build
with the same SuiteCut source and pass the eight real Playwright checks. The
newer versions do **not** eliminate later audio gaps. CEF 154 had the best quiet
capture in this trial, but still lost input under CPU stress. These short serial
trials do not establish a statistically reliable performance winner or long-run
continuity. No new GPU or OBS benchmark was possible/performed in this comparison.

## Startup measurements

All cases start a fresh CEF process/profile and new page AudioContext. The
virtual PulseAudio sink and X server are shared within one collector invocation;
this is a cold browser start, not a cold physical audio-device test. No fixed
warmup delay is used. Audio activation precedes the recording request. The page
reports `AudioContext.state === 'running'` in every capture.

| CEF / Chromium | Immediate PCM: first sound | Packet-gated PCM: first sound | Signal-gated PCM: first sound | Signal PCM ready wait |
| --- | ---: | ---: | ---: | ---: |
| 152.0.8 / 152.0.7977.134 | 785.98 ms | 53.79 ms | **0 ms** | 224.8 ms |
| 152.0.13 / 152.0.7977.158 | 845.90 ms | 53.85 ms | **0 ms** | 220.9 ms |
| 154.0.34 / 154.0.8037.98 | 847.81 ms | 52.44 ms | **0 ms** | 128.2 ms |

PCM measurements are exact signed 16-bit samples, not 10 ms RMS windows. The
native threshold is magnitude greater than 32 in either channel. Packet mode
correctly retains the approximately 53 ms of silence actually supplied by the
source. Signal mode deliberately excludes that silence. The source and
pre-encoder PCM comparison confirms every signal-gated capture starts with a
qualifying sound sample at **native sample zero** (15 captures).

For the default H.264/AAC MP4 path, decoded first signal appeared at 5.42 ms
(152.0.8), 13.27 ms (152.0.13), and 6.31 ms (154.0.34). Native first signal was
0 ms in each. AAC's initial decoded lead-in/codec behavior is distinct from the
hundreds of milliseconds previously recorded while waiting for CEF input.
The coarse 10 ms RMS leading-silence windows were 20 / 20 / 10 ms respectively.
Other AAC cases had 5–16 ms to their first decoded qualifying sample. We do not
claim encoded AAC has an exactly zero-silence first sample.

The final guarded builds were then checked with six additional cold captures:
PCM starts at sample zero both before encoding and after decoding on all three
versions; default AAC/MP4 starts at native sample zero and first decoded signal
at **6.3125 ms** on all three. These final ready waits ranged from 115 ms to
2.104 seconds. Detailed results are in `final-validation.json`.

Startup wait varies: signal-mode ready latency across the tested cases ranged
from about 117 ms to 2.11 seconds. A timeout remains necessary. This does not
establish a guaranteed startup-time bound below the configured deadline.

## Matched quiet and CPU-stress capture

Each version records H.264/AAC Matroska at 1920×1080, 30 fps, with the same
animated marker, 440 Hz browser tone, and repeated scrolling. Quiet and stress
captures are approximately 20 seconds each. Stress adds one four-second busy
loop in the SDK controller at approximately five seconds. Codec, native jitter
buffer (150 ms), source parameters (48 kHz stereo, 960-frame packets), and output
worker count are identical. Captures run serially without builds/test suites.

| Version | Quiet: maximum interior decoded silence | Stress: maximum interior decoded silence | Quiet native CPU cores | Stress native CPU cores | Quiet distinct video fps | Stress distinct video fps |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 152.0.8 | 70 ms | 90 ms | 0.620 | 0.628 | 26.00 | 29.46 |
| 152.0.13 | 110 ms | 150 ms | 0.630 | 0.673 | 28.60 | 29.21 |
| 154.0.34 | 0 ms | 120 ms | 0.609 | 0.888 | 29.85 | 28.12 |

Native CPU is the observed helper/CEF process tree excluding Node and the
sampler; it excludes the separately owned PulseAudio/X server. Distinct fps and
image holds come from full decoded marker frames, not encoder frame counters.
Maximum decoded image holds in these quiet/stress pairs were 166.7/200 ms,
166.7/166.7 ms, and 100/200 ms respectively. All files decoded fully, and decoded
video-frame counts equalled the native encoder counts. Other startup cases and
raw per-process/cgroup samples are retained in `comparison.json` and each case.

The source attribution remains important:

- 152.0.8 quiet/stress: 140 / 200 ms of steady-state uncovered input, exactly
  matched by absent CEF timestamp intervals; no late input.
- 152.0.13 quiet/stress: 240 / 300 ms of steady-state uncovered input, exactly
  matched by absent CEF timestamp intervals; no late input.
- 154.0.34 quiet: no steady-state uncovered input. Stress: 340 ms absent from
  CEF's eventual source timeline, plus **371 late frames = 7.729 ms** that missed
  the native buffer deadline. The new counter exposes those late samples.

The large remaining gaps are not removed by waiting at startup. CPU-quota
throttling was zero in the current/patch runs and one event in the stable stress
run. Consequently, quota throttling alone cannot explain all observed failures.
Audio-service/device scheduling, renderer audio starvation, and callback jitter
remain unresolved upstream leads. Native/source PCM mismatches were zero in the
steady-state comparison of timely input. Some source packets themselves contain
silence, so decoded silent intervals need not equal missing-input intervals.
This does not establish lip-sync accuracy; the fixture uses a continuous tone.

## Implemented contract

```ts
await page.getByRole('button', { name: 'Start audio' }).click()
const recording = await engine.record(page, {
  destination: '/absolute/output.mp4',
  audioStartup: 'wait-for-signal',
  audioStartupTimeoutMs: 5000,
})
```

- `immediate` remains the default for silent pages and flows whose sound begins
  later. The audio-ready guarantee requires selecting a wait mode.
- `wait-for-packet` accepts genuine silent PCM; `wait-for-signal` waits for PCM
  above approximately −60 dBFS and starts at that sample's original timestamp.
- The whole gate is native C++; the SDK sends policy and polls status. Both
  audio and video start from the selected common timeline origin. Later packet
  timestamps are unchanged; missing intervals are neither collapsed nor filled
  with repeated sound.
- The gate also requires a start point with buffered video available. A delayed
  output initialization cannot make audio begin before the surviving video queue.
- Waiting requires realtime audio-enabled output. Timeout is an integer in
  1–60000 ms (default 5000), and failure is explicit. Stop interrupts waiting.
- The helper confirms the accepted mode. The SDK stops an output and rejects
  when an older helper silently ignores the requested wait policy.

Audio must already be active or activated concurrently with the pending call;
waiting for recording to return before activating required audio would reach
the timeout. Silent capture remains supported through immediate/packet modes.

## Validation and version selection

Five CTest targets passed, including exact demuxed PCM-start verification,
intentional silence, no-video-before-audio readiness, timeout, cancellation, and
option validation. The startup regression passed ASAN/UBSAN with leak detection
and no suppressions. Only SuiteCut's media/test code was instrumented; prebuilt
CEF and linked codec libraries were not rebuilt with sanitizers. This is not a
new long-running memory soak or a guarantee that no leaks exist.

307 SDK unit tests passed, including the older-helper rejection/cleanup check.
Typecheck and changed-file lint passed. Each version passed the eight final
real-CEF Playwright cases: locators/input/scroll/iframe/screenshot; delayed native
audio startup; silent timeout/reuse; interception/storage isolation;
popup/dialog/upload/download/CDP; blocked-Node recording/pause/page switching;
repeated shutdown; permissions/geolocation. **This remains a representative
suite, not complete upstream Playwright conformance.** Twenty-one comparison
capture files were fully decoded and checked. Six final cold-start PCM and default
AAC/MP4 verification also checks the guarded builds on all three versions.

The Linux newer builds are available under
`.suitecut/cef-version-tests/build-152.0.13/suitecut-browser` and
`.suitecut/cef-version-tests/build-154.0.34/suitecut-browser`; use `executablePath`
in the SDK or `SUITECUT_RUNTIME_EXECUTABLE` with the saved runner. The existing
cross-platform baseline pin still identifies 152.0.8. Newer macOS binaries and
Windows were not validated. CEF 154 is a viable upgrade candidate for the tested
features; these results do not justify claiming it fixes continuity or performs
better across workloads.

## Evidence and reproduction

`cef-downloads.json` preserves exact official Linux minimal-SDK URLs, byte sizes,
upstream SHA-1 checks and independently calculated SHA-256 hashes. Downloads were
verified before extraction. Initial linking required adding this host's private
X11 development-library directory; no CEF API adaptation was required.

`matrix.json` per version and `comparison.json` save actual reported CEF versions,
options, readiness waits, status samples, exact startup samples, probes/hashes,
coverage attribution, decoded audio intervals, decoded video-marker measures,
and CPU/cgroup samples. Each case retains encoded media, native source PCM/CSV,
pre-encoder PCM/coverage CSV, profiler data, and validation. Full commands and
logs are retained. Replay instructions are in `benchmarks/native-startup/README.md`.

The capture binaries/SDK/source snapshots preserve the implementation used for
the timed comparisons. Final guards then added the native mode confirmation
response and required the audio start point to have buffered video available
(covering slow output initialization). Final binaries/SDK/source snapshots,
additional cold-start recordings, and eight-case checks per version preserve
that guarded implementation separately. Steady-state encoding/buffering did not
change; the comparison numbers above identify their exact capture build.

The first delayed-sound test used an about:blank document and awaited audio
activation without a click; it timed out. Its outputs/logs are retained separately
in `current/playwright-first-failure`. The final test uses HTTP content and an
explicit Start audio click and verifies readiness while the recording is pending.
The initial new-build linker failures and an evidence-folder typecheck failure
are retained; the private X11 path and standard excluded snapshot naming fixed
them. Previous evidence folders were not rewritten.

Runtime dependencies are recorded by hash, not redistributed in the evidence
archive. Download the exact SDKs and provide the documented FFmpeg/X11/Pulse
dependencies to replay executable snapshots. This host has four CPU equivalents,
software graphics, and no usable GPU device; hardware encoding remains unverified.
