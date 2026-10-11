# Native audio gap investigation

Measured 2026-10-09 UTC. Folder label continues the previous codec evidence's
2026-10-10 local-date convention. Original engine and codec evidence is unchanged.

## Finding

The 70–210 ms gaps are not established as an unavoidable CEF latency cost or a
JavaScript single-core limitation. The newly reproduced baseline gaps originate
in the **timestamped audio delivered by CEF**, before encoding. Every uncovered
steady-state input frame matched an interval absent from the original CEF packet
timeline. Native copied input samples matched the source exactly. The lossless
PCM recordings also decoded byte-for-byte identically to their pre-encoder PCM.

Host scheduling contention is the strongest lead for the upstream failures.
Warm quiet controls and a controller-priority control were substantially better
than normal-priority CPU stress. This is evidence of a scheduling-sensitive
problem, not proof of a specific Chromium/PulseAudio root cause. A source can
fail upstream because SuiteCut's workload creates contention; this remains a
SuiteCut reliability problem to address.

The implementation also had an observability defect: `droppedAudioSamples=0`
only meant no queue overflow. It never established uninterrupted audio. New
`missingAudioSamples` and `lateAudioSamples` counters now expose uncovered
intervals and late input. No sample repetition, timestamp collapse, increased
jitter buffer, or other concealment of source gaps was introduced.

## Measurements

All completed runs used a real CEF page with the OBS comparison fixture, a
440 Hz browser oscillator, animated canvas, scrolling, H.264 video at 1920×1080
30 fps, and sequential approximately 30-second PCM and AAC recordings. Codec
choices isolate audio encoding from input capture. Native source and output
traces never travel through Node. No builds or test suites ran during these
completed captures.

| Run, two captures each | Largest CEF timestamp hole | Largest decoded silent interval, PCM / AAC | CPU quota throttling |
| --- | ---: | ---: | --- |
| Cold baseline, 4-second SDK busy loop per capture | 160 ms | 840 ms startup, 150 ms interior / 130 ms | Not sampled |
| Warm quiet control, no SDK busy loop | 20 ms | 30 ms / 10 ms | 0 events |
| Warm normal-priority stress, 4-second SDK busy loop | 1,280 ms | 90 ms / **9,490 ms** | 31 events; 7,271,646 µs aggregate throttled time |
| Warm stress, only controller main thread nice=10 after CEF launch | 40 ms | 30 ms / 20 ms | 0 events |

Warmup was a **2-second benchmark delay**, not an implemented audio-readiness
guarantee. Runs are sequential controls, not repeated randomized trials. The
priority experiment changes the controller after launching CEF so CEF retains
its ordinary priority; this is a diagnostic control, not a default product policy.
Aggregate cgroup throttled time is not wall-clock silence duration. CPU quota is
four CPU equivalents (`400000 100000`), with software graphics and no usable
GPU device. The whole host cgroup can contain work outside the sampled process tree.

The stressed AAC source itself delivered 363 near-silent packets, including a
6,840 ms consecutive silent sequence when measured by packet duration. Its
source timeline also contained multiple absent intervals and a backwards PTS
discontinuity. These findings mean this stress run was worse than the previous
short codec captures; **audio continuity is not fixed or guaranteed**.

### Exact attribution

`analysis.json` in each completed run compares the native coverage trace with
all source packet intervals and compares original source PCM with native output
PCM before encoding. Steady-state verification excludes the first packet at
output creation and the final 150 ms, where subscription start and stop-drain
semantics differ from ongoing capture.

| Run | Uncovered steady-state input, PCM / AAC | Source-timeline uncovered input, PCM / AAC | Native/source PCM mismatched frames |
| --- | ---: | ---: | ---: |
| Baseline | 457 / 320 ms | 457 / 320 ms | 0 / 0 |
| Quiet | 20 / 20 ms | 20 / 20 ms | 0 / 0 |
| Normal-priority stress | 800 / 4,073.8125 ms | 800 / 4,073.8125 ms | 0 / 0 |
| Controller-priority stress | 40 / 0 ms | 40 / 0 ms | 0 / 0 |

All post-counter runs reported zero late input and zero queue-overflow drops.
The encode worker can lag under pressure, but these observations do not show
it discarding available steady-state input. Source intervals remained absent
even after considering packets delivered later. A larger output buffer cannot
recover that data. Source-supplied zero PCM explains why decoded silence can
exceed uncovered-input duration. The 10 ms RMS analysis threshold is 0.001;
lossy-codec ringing and partial windows make decoded silence differ from exact
input coverage. These decoded measurements are not an A/V synchronization test.

### Startup

In the cold baseline, output origin was 580.065293 ms on the helper's clock.
The first CEF packet arrived at 1373.014677 ms: **792.949384 ms later**. The first
two 20 ms source packets were silent. First non-silent input arrived at
1413.039242 ms, about **833 ms after output origin**, and decoded leading silence
was 840 ms. This reproduces the startup mechanism, although it is longer than
the previous 520 ms result. Returning `AudioContext.state === 'running'` did not
mean CEF's capture stream was delivering audio. Native output `ready` currently
depends on encoder initialization and video, not audio input readiness.

Production startup needs an explicit native audio-readiness contract for flows
that require audio from their first recorded frame. A fixed warmup delay is not
that contract. Recording meaningful silence must also remain possible.

### CEF timestamp semantics

The saved source at the exact linked CEF revision `1ce985c` converts
`audio_capture_time - base::TimeTicks()` to integer milliseconds and calls
`OnAudioStreamPacket`. Its public header describes Unix-epoch milliseconds,
which does not match this implementation. SuiteCut already supports this
monotonic form; baseline and quiet traces had no timestamp mapping re-anchors,
and their mapped deltas matched the original PTS deltas. The severe stress run
contained one backwards source PTS jump. No timestamp mapping changes were
made for this investigation.

CEF's `Capture` implementation discards Chromium's `AudioGlitchInfo` rather than
exposing it through the handler. The pinned Chromium input-device source forwards
capture time and glitch metadata to that implementation. Capturing this metadata
in a patched CEF build, alongside AudioContext progress and per-thread scheduling,
would distinguish audio-service deadline misses, source rendering starvation,
and device/loopback scheduling. Current evidence localizes the missing coverage
to CEF input but does not distinguish those upstream causes.

Saved upstream references:

- [CEF pinned audio capturer](https://github.com/chromiumembedded/cef/blob/1ce985c/libcef/browser/audio_capturer.cc)
- [Chromium pinned audio input device](https://github.com/chromium/chromium/blob/152.0.7977.134/media/audio/audio_input_device.cc)

## Changes and verification

- Opt-in buffered C++ source PCM/timestamp/RMS and output PCM/coverage tracing.
- Native and TypeScript output status now expose missing and late audio frames.
- A deterministic native regression verifies that source-provided silence is
  covered, an absent 20 ms block is counted, and a late 20 ms block is counted
  independently of queue overflow.
- Four CTest targets and 306 SDK unit tests passed; typecheck and changed-file
  ESLint passed. Six real-CEF Playwright checks also passed, including recording
  during a blocked Node event loop, pause, and page selection. The media
  regression passed ASAN/UBSAN with leak detection and
  no suppressions. Only SuiteCut media/test code was instrumented, not prebuilt
  CEF or third-party libraries. These are short-run checks, not a new memory soak.

GPU encoding remains unverified: this host has no usable GPU device. The native
pipeline is already C++; another language rewrite would not replace the source
packets that Chromium fails to deliver. Hardware acceleration could reduce CPU
contention, but these results do not establish that it would eliminate the gaps.
No new OBS comparison was run in this investigation.

## Replay and saved evidence

The collector is `benchmarks/native-audio/trace.mjs`; the independent analyzer is
`benchmarks/native-audio/analyze.py`. Build and SDK instructions remain in the
native engine guide. Examples, from the repository root:

```sh
mkdir -p /absolute/output/directory
AUDIO_WARMUP_MS=2000 SUITECUT_AUDIO_TRACE_DIRECTORY=/absolute/output/directory \
  python3 benchmarks/native-engine/runtime.py benchmarks/native-audio/trace.mjs
python3 benchmarks/native-audio/analyze.py /absolute/output/directory
```

Add `AUDIO_NO_STALL=1` for the quiet control, or `AUDIO_CONTROLLER_NICE=10` for
the priority control. `AUDIO_DURATION_MS` changes each capture's duration.
The analyzer uses NumPy, FFmpeg, 48 kHz stereo source traces, and a single target.

Each completed run saves encoded media, original source PCM and per-packet CSV,
pre-encoder PCM and coverage CSV, status/results, decoded analysis, and logs.
Controls after baseline also save process/cgroup profiles. An aborted capture
that overlapped SDK tests is retained in `aborted-test-overlap`; it was stopped
and excluded from every result above. Source/binary snapshots, a scoped change
diff, and checksums preserve the final tested implementation. The initial
trace-only baseline executable was subsequently replaced by the counter-enabled
build; its raw measurements are retained, but its executable was not snapshotted.
