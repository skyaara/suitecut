# SuiteCut optimization and OBS comparison — 9 October 2026

The retained-frame leak is fixed in the measured reuse tests, and short browser-flow checks pass. Long-running media reliability remains unresolved: the final 30-minute dual-output run contains a 13.18-second silent interval in both outputs, video gaps over 11 seconds, and a failed received A/V window. Memory observations are finite; no indefinite leak guarantee or universal performance advantage over OBS is established.

Profiles found a stale-frame catch-up loop, encoder thread contention and a real recording-session retention bug. The C++ capture path already handles browser rendering/conversion, and FFmpeg handles native encoding. The measured fixes target scheduling and ownership; no whole-application language rewrite was made.

## Production changes and reasons

- The recorder forwards bounded latest/pending frames with real capture timestamps in raw Matroska instead of repeatedly writing stale pixels to catch up with a constant-rate clock. Recording remains VP9, with variable presentation intervals preserved. A held final frame preserves the requested ending interval.
- The new raw-video container supports I420 and correctly oriented BGRA. Real FFmpeg checks verify irregular presentation times, frame count and orientation.
- Streaming retains the timestamp of the selected pixels through asynchronous encoding. H.264 reference dependencies are retained; repeated timestamps are made increasing without discarding dependent encoded frames.
- Streaming explicitly uses four x264 threads and one filter thread for the four-core quota. The production VP9 recorder already used four threads. The rejected frame-thread and H.264-recording experiments remain diagnostic.
- Removable browser lifecycle listeners, shared weakly cached page lifetime signals, and cleared session/recorder/parser references prevent completed recordings from retaining one full frame each.
- The C++ capture path borrows the existing BGRA view for I420 conversion when no popup composition is needed. A negotiated frame-subscription capability avoids conversion/transfer when there are no frame consumers and clears inactive caches. Older binaries retain the compatible continuous-frame path.
- Observed capture receipt tightens the native clock offset. Live audio accepts a bounded 500 ms future presentation window consistent with the native allowance, while history remains bounded. This future allowance is distinct from the playback delay.
- Media probing handles an unknown average frame-rate field by using a valid nominal rate, allowing timestamped VFR recordings to be read correctly.

Source snapshots, the final patch, compiled JavaScript/native executable, test logs and run-specific source hashes are saved. Heavy rendering/conversion/encoding already execute in native code; the profiles do not justify attributing the remaining long media failure solely to JavaScript or replacing the complete API/control layer in C++.

Node’s control event loop and the web page’s JavaScript/DOM execution are separate. Rewriting the host API in C++ would not make page scripts or DOM updates automatically parallel. CEF has multiple processes/threads and FFmpeg already uses native encoder threads. Reducing copies, pipe traffic and scheduling overhead can help, but language alone is not a measured explanation for the remaining failure. The independent process traces retain the evidence and uncertainty.

## Updated serial recording comparison

Three ten-second trials per pipeline/case, alternating order. Motion uses decoded marker transitions and presentation timestamps, including the final held interval. Nominal container FPS is not used as elapsed time. Requested rates are targets, not success claims.

| Case | Pipeline | Passed/attempted | Distinct FPS median | Longest hold ms | Finalize s | CPU % | Peak summed RSS MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| 720p60-canvas | obs | 3/3 | 56.90 | 83.00 | 1.18 | 222.23 | 1513.87 |
| 720p60-canvas | native | 3/3 | 59.65 | 53.00 | 0.38 | 200.97 | 1308.79 |
| 1080p60-canvas | obs | 3/3 | 41.03 | 66.00 | 1.47 | 351.10 | 1806.89 |
| 1080p60-canvas | native | 3/3 | 34.09 | 128.00 | 0.90 | 278.89 | 1542.66 |
| 4k30-canvas | obs | 3/3 | 5.02 | 300.00 | 2.79 | 394.71 | 3081.20 |
| 4k30-canvas | native | 3/3 | 6.82 | 653.00 | 4.27 | 316.28 | 2645.85 |
| 4k60-scroll | obs | 3/3 | 1.70 | 1667.00 | 14.17 | 396.35 | 3209.33 |
| 4k60-scroll | native | 3/3 | 5.34 | 656.00 | 6.63 | 337.21 | 2784.64 |

![Repeated comparison](comparison-fps.png)

OBS captures external Chromium through XSHM/Xvfb and records x264 veryfast CRF18/H.264 MKV with silent AAC. SuiteCut captures CEF/I420 and records VP9 CRF18/realtime/cpu-used5/four threads WebM without audio. This is an end-to-end production-pipeline comparison; codecs and capture paths differ. Software Mesa llvmpipe, four CPU equivalents, 16 GiB memory limit. Summed RSS counts shared pages more than once. This run restarted OBS; use its concurrent repeated results for the current ranking, not the old OBS medians.

## Encoder-thread control comparison

OBS automatic threading is a default-policy result, while SuiteCut explicitly limits native encoder threads for this four-core quota. A second runtime applies OBS `x264opts=threads=4` and repeats all 24 alternating trials on the same final SuiteCut build. Preset, codec, rate control and capture paths remain unchanged. This controls that threading choice, not all codec/browser/GPU differences. Raw settings before/after are in `checks/obs-thread-tuning.json`.

| Case | Pipeline | Passed/attempted | Distinct FPS median | Longest hold ms | Finalize s | CPU % | Peak summed RSS MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| 720p60-canvas | obs | 3/3 | 57.62 | 50.00 | 0.64 | 212.46 | 1457.09 |
| 720p60-canvas | native | 3/3 | 59.84 | 33.00 | 0.41 | 191.25 | 1284.04 |
| 1080p60-canvas | obs | 3/3 | 40.00 | 50.00 | 0.71 | 345.84 | 1716.78 |
| 1080p60-canvas | native | 3/3 | 31.82 | 100.00 | 1.11 | 264.28 | 1539.64 |
| 4k30-canvas | obs | 3/3 | 4.92 | 267.00 | 2.12 | 395.48 | 2791.95 |
| 4k30-canvas | native | 3/3 | 6.56 | 550.00 | 4.75 | 323.68 | 2661.67 |
| 4k60-scroll | obs | 3/3 | 1.29 | 1817.00 | 20.72 | 397.05 | 3049.62 |
| 4k60-scroll | native | 3/3 | 6.43 | 568.00 | 8.49 | 333.91 | 2755.11 |

![Thread-limited comparison](comparison-thread-limited-fps.png)

Active encoder log excerpts are saved in `checks/obs-runtime-thread-settings.json`, and captured on-disk settings are restored after shutdown. An earlier control launch collided with a duplicate runtime: X/Pulse binding failures, conflicting PID records and profile restoration exposed the problem. Its media checks passed, but isolation/process accounting was uncertain, so it is excluded from this ranking and preserved with `-runtime-collision` suffixes. The final control coordinator uses an exclusive lock and verifies its runtime and restored settings. See `checks/thread-control-runtime-collision.json` for relocation and failure details.

## Bottleneck experiments

| Experiment | Mode | Pixels | Decoded distinct FPS | Node % of one core | Loop utilization | Loop p99 ms | Finalize s |
|---|---|---|---:|---:|---:|---:|---:|
| baseline-record | record | 1280x720 | 0.59 | 42.31 | 0.56 | 30.03 | 15.69 |
| baseline-stream | stream | 1920x1080 | 5.17 | 68.96 | 0.71 | 39.06 | 2.97 |
| latest-4k-record | record | 3840x2160 | 7.76 | 84.49 | 0.72 | 25.43 | 5.91 |
| timestamp-h264-record | record | 1920x1080 | 21.06 | 59.20 | 0.65 | 31.65 | 0.94 |
| timestamp-record | record | 1280x720 | 22.40 | 62.14 | 0.64 | 30.61 | 1.15 |
| tuned-frame-threads-stream | stream | 1920x1080 | 10.75 | 84.49 | 0.76 | 35.29 | 1.28 |
| tuned-threads-stream | stream | 1920x1080 | 14.35 | 81.33 | 0.71 | 30.57 | 1.54 |

These diagnostics used instrumentation and different builds/settings, so they are attribution experiments, not the serial comparison table. Baseline recorder forwarding wrote 606 pixel buffers, 600 reusing the preceding Buffer, and spent 24.65 seconds blocked on FFmpeg drain across a ten-second capture and finalization. Only six decoded marker transitions remained. Timestamped Matroska input removes the stale-frame catch-up loop. The four-thread x264 experiment improves streaming; disabling sliced threads was worse and was rejected. The H.264 recorder substitution was diagnostic only; the production recorder remains VP9. Static unpaced encoder results and complete arguments/stderr are in `profiles/isolated-encoders.json`; they do not measure browser capture throughput.

## Recording-session retention

25 recordings reused one live 960x540 I420 source. Post-GC array buffers grew by 791,253 bytes per recording before the fix (one full pixel frame), ending at 19.06 MiB. After removable failure/close listeners and reference cleanup, the last ten recordings stayed between 0.266 and 0.266 MiB. Raw samples and manifests are retained. Forced GC isolates retained memory here; it was not used during the continuous soak.

![Source reuse memory](reuse-memory.png)

Page wrappers now share one weakly cached browser lifetime signal. A regression creates 2,000 wrappers and verifies only one failure and one close observer, while preserving source failure and caller cancellation. The final comparison and continuous soak include this change. The earlier diagnostic comparison is preserved separately.

An extended lifetime-fix build test completed 200 recordings on one live source. Heap snapshots after recording 11 and 200 show Buffer objects 60 → 60, Promise objects 241 → 241, ChildProcess objects 1 → 1 and AbortController objects 2 → 2. Post-GC heap increased by about 0.52 MiB overall; the largest shallow-size increases are V8 code/byte-array structures, and the harness deliberately retains 200 measurement rows. AbortSignal objects increased by six, not one per recording. These object counts and shallow sizes support the lifetime fix but are not a complete dominator/allocation audit. Raw snapshots, memory samples and `profiles/extended-reuse-after/heap-comparison.json` are preserved.

## Streaming audio regression found during the first long test

The first complete 30-minute run had bounded Node heap and passed recorded-audio windows, but the received AAC audio was silent in the midpoint window. It is preserved as `soak-before-audio-window-fix/`, with its failed result and diagnostic packet traces. It does not count as a passing long run. Lossless remuxing reproduced the silence, excluding a seek-only artifact. Native audio allowed presentation times up to 250 ms ahead of receipt while the live buffer discarded packets more than 100 ms ahead. The mapped handshake clock also included transit delay. Capture timestamps now refine that clock upper bound; the live buffer has a bounded 500 ms future allowance. Regression tests exercise both cases. The final run adds observed audio timestamp offsets; its complete media results, rather than the plausible mechanism alone, determine whether streaming passes.

A second 30-minute run with the audio window/clock fix delivered audible midpoint audio, but its live received midpoint p95 synchronization error was about 219 ms and the finalized recording’s midpoint error was about 217 ms, exceeding the unchanged 200 ms fixture limit. Finalization stopped at the recording assertion, so the live receiver check is retained separately rather than represented as a completed final receiver validation. It is preserved separately as `soak-before-capture-stamp-fix`. The encoder had associated selected pixels with their scheduled time slot instead of their actual capture time; backpressure or sparse capture can make those differ. The encoded output now carries the selected frame’s capture timestamp, retaining valid codec dependencies. A delayed-processing regression exercises that association. The overlapping short pilot failed the unchanged validation count/sync criteria under contention and is preserved with all six decoded windows. The first serial pilot had p95 errors below 84 ms in all six windows but only four flashes at its end, failing the unchanged five-match requirement. A 250 ms pulse replaces the 120 ms diagnostic pulse to improve visibility under sparse capture; the following serial pilot and final long run use that fixture. Source/codec paths are unchanged, and all earlier failed results remain saved. Both failed runs remain evidence, not passing results.

## 60 fps stress failure and system/sampler stall

The subsequent 1080p60 dual-output run completed 30 minutes with stable managed memory and passed all recorded A/V windows. Its first received sync window remained a strict failure at 203.33 ms against 200 ms (checker resolution is 10 ms audio and 16.67 ms resampled video; sparse capture adds uncertainty). Full-file audio analysis then found 15.20 s recorded silence and 19.86 s received silence near minute 24. Node had a 16.18 s event-loop gap and the independent process sampler had long concurrent gaps with little CPU progress across native processes. This is not evidence of a JavaScript-only stall, and observer/host causality is not established. The complete failed run, all six windows, decoded full PCM/RMS, process-sampler gaps and private-memory traces remain under `soak-60fps-private-sampler-stall/`.

The final long verification uses the supported 1080p30 target with lightweight RSS/CPU/descriptor sampling and no periodic private-memory walks. It too completed the workload but failed received start-window sync (298.33 ms, with one outlying periodic match) and full-file audio continuity (a 13.18 s silent interval in both outputs). Thus lowering the target and disabling private-memory walks did not establish reliable long media on this host. These failures are retained; no clean long-run media pass is claimed. Target and monitor both differ, so this is a supported-workload validation rather than an isolated proof that private-memory sampling caused the earlier stall. All 60 fps stress failures remain explicit.

## Continuous recording and streaming memory test

Observed status: **failed**. Requested 30 minutes; observed active interval 30.004 minutes. A single caller-owned 1080p30 I420 CEF source simultaneously records VP9 plus Opus and streams H.264 plus AAC to a local RTMP receiver. Canvas motion, periodic real wheel scrolling and synchronized 250 ms flash/440 Hz tone bursts remain active. Earlier diagnostics used 120 ms bursts; the final longer pulse improves observability when video capture is sparse, while the five-match and 200 ms synchronization requirements remain unchanged. This concurrent stress run is separate from FPS comparison trials.

Run error: `AssertionError [ERR_ASSERTION]: receiver 5: A/V drift 298.3333333333328ms
    at file:///workspace/suitecut/benchmarks/obs-cef/soak.mjs:66:86`

Recording: 1803.835 seconds, 52,020,733 bytes. Decoded A/V windows:

| Window offset seconds | Status | Matched flashes | p95 absolute A/V offset ms |
|---:|---|---:|---:|
| 5 | passed | 12 | 69.67 |
| 900 | passed | 12 | 79.00 |
| 1780 | passed | 11 | 53.33 |

Receiver: 1801.770 seconds, 48,410,109 bytes. Decoded A/V windows:

| Window offset seconds | Status | Matched flashes | p95 absolute A/V offset ms |
|---:|---|---:|---:|
| 5 | failed | 8 | 298.33 |
| 900 | passed | 8 | 113.00 |
| 1780 | passed | 9 | 76.67 |

Complete audio is separately decoded to 8 kHz mono PCM and analyzed in 20 ms RMS blocks. The one-second pulse fixture deliberately includes silence; the continuity check reports all gaps over two seconds and fails on any gap over five seconds between 3 s and five seconds before the requested end. This checks audible coverage, not continuous A/V synchronization. Full PCM, RMS traces and minute metrics are preserved.

Recording continuity: **failed**, longest silent interval 13.18 s, intervals over two seconds: 6, over five seconds: 3.
Receiver continuity: **failed**, longest silent interval 13.18 s, intervals over two seconds: 6, over five seconds: 3.

Full video packet analysis finds ordered, unique presentation/decode timestamps but maximum video gaps of about 11.33 seconds in the recording and 11.40 seconds in the receiver. Positive per-minute packet counts and motion in three sampled windows do not establish uninterrupted video. The final run also has a 7.34-second maximum Node event-loop delay and a 9.60-second independent process-sampler gap. Their correlation does not isolate JavaScript, IPC, observer or host causality. Full gap observations are retained in `soak/process-sampler-gaps.json` and `soak/packet-motion-validation.json`.

Captured audio offsets relative to Node receipt: `{'count': 86516, 'minMs': -11197.997845998732, 'maxMs': 110.89541699946858, 'futureOver100': 1010, 'futureOver500': 0, 'pastOver10000': 6}`. These diagnostics count source packets; the decoded receiver checks independently verify delivered sound.

![Continuous memory](soak-memory.png)

Node samples: 1809. Memory is sampled without forced GC during recording. The final 30 fps run samples per-process CPU, RSS, threads and open descriptors every half second and records sampler execution time. Periodic PSS/USS walks are disabled here after a correlated system/sampler stall in the 60 fps stress run; that earlier private-memory data remains saved. RSS sums count shared pages repeatedly and are not private-memory totals. See `soak/memory-summary.json` for five-minute buckets, `soak/processes.jsonl` for individual processes, and `soak/node-memory.jsonl` for Node RSS, heap and buffers.

The shaded first minute marks startup. The final soak has no concurrent source-reuse tests, OBS comparisons, unit/build checks or live media decodes. The prior stress run supplies PSS/USS measurements; final-run role RSS is explicitly shown as RSS. The red line marks the end of the active run; later process samples reflect finalization/media validation.

This finite run cannot prove leak freedom forever or cover all CEF/FFmpeg paths. Conclusions concern the measured workload/build/duration. Post-cleanup forced-GC memory is retained separately in the result; external native libraries are not fully allocation-audited.

## Repeated 1080p60 browser stream checks

| Pipeline | 20-second runs | Checks passed | Distinct FPS median | p95 A/V ms median | Received muxed kbps median |
|---|---:|---:|---:|---:|---:|
| obs | 3 | 12/12 | 38.08 | 128.00 | 4503.24 |
| native | 3 | 12/12 | 23.90 | 60.00 | 322.03 |

The restarted OBS runtime with explicit four-thread x264 settings repeats the same received-stream checks:

| Pipeline | 20-second runs | Checks passed | Distinct FPS median | p95 A/V ms median | Received muxed kbps median |
|---|---:|---:|---:|---:|---:|
| obs | 3 | 12/12 | 32.05 | 64.67 | 4510.40 |
| native | 3 | 12/12 | 23.81 | 53.33 | 318.79 |

Each stream is received as real RTMP media and decoded. The medians summarize three repetitions, not a codec/quality-normalized claim: OBS uses x264 veryfast/high with its normal B-frame settings and AAC160; SuiteCut uses x264 veryfast/zerolatency with four encoder threads, adaptive video control and AAC128. Both target 4,500 kbps, but actual muxed bitrate is measured above. Periodic flash/tone matching resolves offsets only within half a second and does not measure network/display latency. Raw receiver packets and media are preserved.

## Correctness and saved evidence

Native integration checks cover BGRA/I420 sources, browser input, navigation, narration preparation pauses, rendered audio/page selection, checkpoints, source ownership, callback failure/cancellation, streaming reconnect/source replacement and A/V sync. The real-FFmpeg container regression checks orientation and irregular 0/17/400/1000 ms timestamps without manufacturing missing frames. Logs and reports are saved under `checks/` and `logs/`. Test totals are read from saved test logs.

The final OBS-comparable feature runs contain four recording runs with 40 checks, six default-policy RTMP runs with 24 checks, and six additional encoder-thread-control RTMP runs with 24 checks. Each check’s measured outcome is retained; failed long media is a separate finding and is not overwritten by passing short checks. Read `features-record-final/results.json` and `features-stream-final-{1,2,3}/results.json` for each operation, source-state sequence, codec/packet check, tone analysis and A/V result. Streaming FPS is reported from received media duration and the original hold calculation is retained as a diagnostic approximation; the recording comparison uses exact frame PTS.

Final verification: 66 focused unit tests in ten files, run serially with a 30-second test timeout after one parallel worklet test exceeded its default five-second limit during overlapping encoder load; type checking, focused ESLint and formatting; two C++ CTest helpers; real FFmpeg timestamp/orientation regression. AddressSanitizer/UndefinedBehaviorSanitizer helper tests with leak detection are saved under `logs/asan-*.log`. The prebuilt CEF and libyuv libraries are not instrumented by those helper tests; they are not a full native/browser allocation audit.

The original 90-trial/56-functional-check report is `../2026-10-09/report.md`. Its original complete archive remains `/workspace/shared/suitecut-obs-evidence-2026-10-09.tar.gz`. This optimization report, source snapshots, patch, CPU profiles, repeated trials, raw memory samples, manifests, failed/rejected diagnostics, recordings and RTMP receiver files are saved separately. `notes.md` describes measurement boundaries; `reproduce.sh` records the commands for this run.
