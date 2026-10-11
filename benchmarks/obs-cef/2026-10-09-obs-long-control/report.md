# OBS matched 30-minute recording and streaming control — 9 October 2026

OBS complete-media verdict: **passed**. SuiteCut reference verdict under these checks: **failed**. Read continuity, synchronization and frame-loss results separately; a continuity pass does not mean zero dropped frames.

## Workload and scope

OBS ran for 30.003 active minutes at 1920×1080, targeting 30 fps. One sandboxed external Chromium page uses the identical fixture, moving binary frame marker, scrolling every five seconds with alternating direction, and one-second 250 ms flash/440 Hz tone pulses. Independent OBS record and RTMP encoders run concurrently. Scrolling commands: 354; observed browser scroll events: 354. No additional browser/encoder benchmark or media decode ran during capture. Preparing the lightweight analysis scripts included brief syntax checks; no production build or unit suite ran.

The same host has a four-CPU-equivalent quota, 16 GiB memory limit and software Mesa llvmpipe. The OBS profile is copied into an isolated directory; both x264 encoders use four threads. Recorder settings are veryfast/high CRF18 H264/AAC160 MKV; streaming uses veryfast/high CBR4500 H264/AAC160 with normal B frames and a local RTMP receiver. SuiteCut’s reference uses CEF/I420, VP9/Opus recording and H264/AAC128 streaming. Browser version/capture path/codecs differ; the runs also occur at different times. This controls the supported browser workload, duration and concurrency, not every implementation variable.

An exclusive runtime lock prevents duplicate display/audio/OBS launches. Runtime PIDs, source hashes, effective video settings, before/after OBS settings and all errors are saved. Lightweight CPU/RSS/thread/descriptor monitoring runs every half second without PSS/USS walks. OBS/controller status is sampled about once per second; browser state about once per thirty seconds. Reference SuiteCut monitoring differs in observable counters and lacks the added cgroup pressure samples. Neither monitoring is a full native allocation audit.

## Full-output continuity

| Pipeline | Output | Media seconds | Audio continuity | Longest silence s | Video continuity | Longest held content s | Distinct active FPS | Repeated-frame fraction |
|---|---|---:|---|---:|---|---:|---:|---:|
| suitecut | recording | 1803.835 | failed | 13.18 | failed | 11.401 | 11.97 | 7.702% |
| suitecut | receiver | 1801.770 | failed | 13.18 | failed | 11.400 | 10.90 | 25.488% |
| obs | recording | 1802.666 | passed | 1.10 | passed | 0.533 | 28.81 | 3.951% |
| obs | receiver | 1800.599 | passed | 1.10 | passed | 0.534 | 28.81 | 3.951% |

![Full continuity comparison](continuity-comparison.png)

Complete audio is decoded to mono 8 kHz PCM, with 20 ms RMS blocks and the identical 0.015 audible threshold used for SuiteCut. Natural silence between pulses is expected. All silent intervals over two seconds are reported; intervals over five seconds fail the continuity check. Startup before 3 s and finalization after the requested end minus 5 s are excluded consistently.

Complete video is decoded to binary motion markers and paired with every actual presentation timestamp. Constant-rate packets containing repeated images count as held content. Every hold over one second is saved; a hold over five seconds fails the long video continuity check. Unique timestamps alone do not establish motion. Distinct FPS and repeated frames remain reported even when continuity passes. The existing SuiteCut outputs are reanalyzed read-only with this same content checker; the original evidence files/archive are unchanged.

## Audio and video synchronization

| Pipeline | Output | Window offset s | Matched flashes | p95 absolute offset ms | Result |
|---|---|---:|---:|---:|---|
| suitecut | recording | 5 | 12 | 69.67 | passed |
| suitecut | recording | 900 | 12 | 79.00 | passed |
| suitecut | recording | 1780 | 11 | 53.33 | passed |
| suitecut | receiver | 5 | 8 | 298.33 | failed |
| suitecut | receiver | 900 | 8 | 113.00 | passed |
| suitecut | receiver | 1780 | 9 | 76.67 | passed |
| obs | recording | 5 | 13 | 15.33 | passed |
| obs | recording | 900 | 13 | 83.33 | passed |
| obs | recording | 1780 | 13 | 46.67 | passed |
| obs | receiver | 5 | 13 | 15.33 | passed |
| obs | receiver | 900 | 13 | 83.33 | passed |
| obs | receiver | 1780 | 13 | 46.67 | passed |

The unchanged fixture limits are at least five matches and p95 absolute offset below 200 ms in 15-second beginning, midpoint and end windows. Periodic matching resolves offsets within half a second and is not a measurement of network/input-to-display latency. Full continuity analysis additionally checks the intervals outside these sync windows.

## OBS skipped-frame and process observations

OBS rendering skips: 2,037/54,027 (3.770%). OBS output/encoding skips: 70/54,022. Final stream congestion: 0; reconnecting: False. These counters cover OBS’s own rendering/encoding, not missing in-page animation updates; decoded marker metrics provide the content check.

Controller event-loop maximum delay: 301.73 ms. Independent sampler gaps over two seconds: 0. Per-process CPU/RSS/thread/descriptor samples and cgroup CPU/pressure/OOM counters are preserved. RSS sums double-count shared pages and cannot prove absence of native leaks.

![Process observations](memory-monitor.png)

## What the control establishes

OBS sustained this comparable dual-output workload without exceeding the saved silence, held-content or synchronization failure limits. SuiteCut did not. That is actionable evidence to prioritize SuiteCut’s long-run capture/backpressure/audio servicing investigation. It does not prove the entire host was healthy during the earlier run, isolate CEF versus codecs/IPC, or establish JavaScript as the cause. A full C++ rewrite is not justified by this control alone. OBS rendering skips and repeated frames remain explicit; this is not an assertion that OBS dropped nothing.

The next investigation should trace CEF frame/audio callback timing, raw-pipe write latency, Node packet handling/event-loop delays, FFmpeg drain waits and native audio-queue overflows together. The existing C++ writer already has a separate thread and prioritizes queued audio, but it finishes a complete video packet before selecting the next packet. Its audio queue is capped at 100 packets (about two seconds at the configured 20 ms buffer size). That bounded shared transport is a concrete pressure point to instrument; this control does not establish it as the root cause.

Low-resolution raw marker export can log duplicate debug-muxer DTS when its rawvideo time base rounds irregular VFR timestamps. Those warnings are retained. Source frame presentation times must be strictly increasing and decoded marker/frame counts must match; continuity uses the source timestamps, not the raw export muxer clock.

## Saved artifacts

- `result.json`, `driver.json`, `settings.json`: actual workload status, effective settings, source hashes and isolated runtime identity.
- `media-validation.json`, `telemetry-summary.json`: per-output verdicts, thresholds, drop counts and monitor gaps.
- `analysis/{obs,suitecut}/{recording,receiver}/`: actual frame timestamps, every decoded motion marker, full audio RMS traces, probes and three audio/video sync windows. Newly decoded OBS PCM is included; SuiteCut’s PCM/media remains in the original optimization bundle.
- `status-samples.jsonl`, `processes.jsonl`, `logs/`: every status/process observation, cgroup pressure samples, errors and encoder settings.
- Recordings and RTMP receiver media: `.suitecut/obs-long-control/2026-10-09/media/`.
- Original SuiteCut reference: `../2026-10-09-optimization/soak/`; verified original archive: `/workspace/shared/suitecut-optimization-evidence-2026-10-09.tar.gz`.
- The new control evidence archive has a file hash manifest and archive checksum; runtime dependencies are excluded and pinned by the existing setup/version records.
