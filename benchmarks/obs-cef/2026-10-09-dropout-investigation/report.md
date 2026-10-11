# SuiteCut dropout investigation — 9 October 2026

The original failure includes a browser/page animation stall; a Node-only reader pause does not reproduce its source-counter behavior. Separately, a controlled four-second Node pause proves that the shared raw-media pipe loses audio under backpressure. The exact OS/CEF trigger of the original capture stall remains unresolved. No media behavior or codec change was made in this investigation, and this is not evidence that a full C++ rewrite would cure it.

## Original failure, aligned with retained telemetry

The recording held marker 28,201 from 1112.118 to 1123.519 seconds, then resumed at 28,205: only four webpage animation frames advanced across an 11.401-second interval. At normal 30 fps, a reader-only pause would allow hundreds of source frames to advance. Early freezes likewise resume with one or two increments. This binary counter is incremented inside the fixture's `requestAnimationFrame`; it is not an encoder frame number.

Node received no new frames or audio packets between recording times 1112.361 and 1123.445 (11.084 s). Across that sampled interval it used 1.260 CPU seconds, or 11.36% of one core. Its saved maximum event-loop delay was 7.336 seconds. An event-loop delay or a high wall-clock utilization value alone does not prove that JavaScript consumed that time. Blocking native work and descheduling remain possible.

An independent Python process scan also took 9.599 seconds around the worst failure. Its relative clock starts before the recording, with no exact realtime/monotonic anchor, so its timestamps are presented separately. That coincident slowdown supports investigating broader scheduling/native blocking, but does not identify a kernel, disk, CEF or quota cause. The old sampler divided CPU deltas measured after each read by timestamps from before scans; long scans invalidate its apparent CPU spikes. Conclusions here use Node's directly saved `process.cpuUsage` deltas instead.

The fixture schedules pulse audio only two seconds ahead, from a page timer. A long renderer timer stall can exhaust that scheduled audio while animation also stops. Missing PCM then becomes silence in the recording; the live audio buffer also emits silence for absent samples. This explains a plausible chain behind the simultaneous missing content. The old run has no native callback/queue counters, so it cannot establish exactly how much loss occurred at each stage.

![Actual source frame counter recovery](source-counter-comparison.png)

## Controlled reader-pause reproduction

The 90-second test deliberately blocks Node with `Atomics.wait` for four seconds at about 40 seconds of active work. It does not deliberately pause CEF. It must not be presented as a clean performance control.

CEF paint/audio callbacks continued, with maximum gaps of 458.4/196.3 ms. A complete video packet write blocked for 4.145 s. The audio queue reached its 100-packet cap and dropped **108 packets**, nominally 2.16 seconds of 20 ms PCM. The recording's held content resumed with **128 source-counter increments**, unlike the original four.

The writer is already C++. It prioritizes audio when choosing a packet, but cannot interrupt a partially written length-prefixed video packet. At 1080p an I420 payload is 3,110,400 bytes. Node must consume that payload before later audio can be decoded. A two-second audio cap bounds memory but silently discards older PCM if the reader is blocked longer. Raising the cap postpones loss and increases latency/memory; a second pipe alone still depends on Node's event loop. A complete solution needs independent audio servicing and an explicit overload policy, plus investigation of the capture stall itself.

## Clean 30-minute diagnostic control

One caller-owned sandboxed CEF source records VP9/Opus and streams H264/AAC to a local receiver at 1920×1080, requested 30 fps, using the same fixture, 250 ms flash/tone pulses and alternating wheel commands. Native timing diagnostics, child pipe timing and independent raw CPU/RSS/scheduling samples run concurrently. No deliberate pause, media decoding, compilation, unit tests or other browser/encoder workload runs during capture. Native stderr receives content-free timing counters once per second. Node GC timing is enabled for the clean run. Earlier OBS results remain separate, from a different browser/capture/codec path and run time.

Completed active time: 30.002 minutes. Scroll commands/events: 347/347. Complete decoded-output verdict: **passed**.

| Output | Longest silence s | Longest content hold s | Distinct active FPS | Sync windows | Verdict |
|---|---:|---:|---:|---|---|
| recording | 2.10 | 1.702 | 16.79 | 5s: 41.50ms passed, 900s: 81.50ms passed, 1780s: 59.33ms passed | passed |
| receiver | 3.10 | 1.643 | 14.08 | 5s: 174.67ms passed, 900s: 64.67ms passed, 1780s: 26.67ms passed | passed |

Native callback maximum gaps: paint 1514.9 ms; audio 1260.4 ms. Maximum complete-packet write: video 975.2 ms; audio 204.3 ms. Audio queue peak: 30/100; audio drops: **0**. Replaced pending native video frames: 3327. Node's cumulative-monitor maximum event-loop delay: 1382.0 ms. Independent sample maximum interval: 1.750 s. Largest observed GC pause: 402.9 ms.

Native counters are cumulative through the one-second snapshot nearest the end of active work, and include startup. Final lifetime counters additionally include finalization and the short sync decodes, and remain saved separately. GC and cumulative-loop maxima cover the Node process lifetime. Per-second histograms are supplementary: resetting them can miss delays at interval boundaries, so their smaller maximum is not substituted for the cumulative maximum. Memory medians below use active-work samples. Child-stdin timings do not instrument the recorder’s separate filesystem PCM writes.

The cgroup used an average of 3.84 CPU cores over the sampled active interval against its four-core-equivalent quota. Raw per-process CPU budgets and scheduling counters are in `findings.json`. Throttling counters accumulate across threads and are not themselves a measurement of a single wall-clock stall.

Full-output checks use 20 ms mono 8 kHz RMS blocks, audible threshold 0.015 and silent-gap failure over five seconds. Video uses every decoded marker and its actual presentation timestamp; a content hold over five seconds fails. Startup before three seconds and the final five requested seconds are excluded consistently. Three 15-second sync windows per output require at least five pulse matches and p95 absolute offsets below 200 ms. A continuity pass is not a zero-drop claim. The controlled reader-pause test passed those thresholds despite demonstrable packet loss; always read timing and drop counts too.

Low-resolution raw marker export can warn about duplicate raw-muxer DTS because its debug time base rounds VFR timestamps. Those warnings are retained. The content checks require every source presentation timestamp to increase strictly and every decoded marker to match the source frame count; they do not use that debug-muxer time base.

## Memory and limits

| Node measure | Minutes 10–15 median MiB | Last five minutes median MiB | Maximum MiB |
|---|---:|---:|---:|
| rss | 280.53 | 288.84 | 298.86 |
| heapUsed | 49.19 | 49.28 | 50.07 |
| external | 75.54 | 76.82 | 147.09 |
| arrayBuffers | 71.67 | 71.99 | 143.28 |

After cleanup and two explicit GC passes, Node retains 0.329 MiB of ArrayBuffers and 48.53 MiB of used heap. Process RSS, descriptors, thread counts, main-thread scheduling runtime/wait counters and available cgroup CPU/memory counters are saved. Linux PSI files were unavailable on this host. RSS includes shared pages and caches; these bounded observations do **not** prove absence of native leaks or indefinite stability. CEF and the complete encoder pipeline were not instrumented with an allocation sanitizer in this test.

## Changes and validation

The only production change is an opt-in native diagnostic switch and bounded counters; media protocol and behavior are retained. The observer does not run by default, and does not hold the media queue lock while writing stderr. A clean native build, both C++ clock/pixel checks, and 37 tests across native protocol/browser/recorder and live audio passed. The controlled pause also exercises the diagnostic thread, queue cap and normal shutdown. Python/Node/shell syntax and diff checks pass. Existing unrelated working-tree changes from the earlier optimization are preserved. No commit, push or pull request was created.

## Saved evidence and next decision

`findings.json` preserves both old correlations and the new run summaries. Each run folder has the workload result, source/binary hashes, raw native logs, every Node/process/pipe sample, effective FFmpeg commands, GC log, full decoded marker/PTS/RMS traces, and all six sync windows. Actual source recording and receiver media are included in the new evidence archive. Tested source snapshots and a SHA-256 file manifest make the bundle reviewable. Both prior evidence archives are rehashed and remain unchanged.

This investigation supports targeted capture/scheduling and independent media transport work. It does not establish a single-core JavaScript bottleneck as the original cause. Rewriting the orchestration language would still leave CEF page JavaScript, browser rendering and encoder scheduling to solve. A passing fresh control cannot erase the original intermittent failure; a failing fresh control should be assessed against its new native callback, queue and scheduling telemetry before changing architecture.

For reproduction, build the helper with the pinned CEF/libyuv dependencies, then use `run-pipeline-investigation.py` with a fresh `INVESTIGATE_LABEL`. Set `INVESTIGATE_DURATION_MS=1800000` for the clean test; omit `INVESTIGATE_STALL_MS`. The labeled reproduction uses 90000 and 4000 respectively. The runner refuses an existing output directory, acquires the runtime lock and stops the display/audio runtime afterward. Run `analyze-pipeline-investigation.py OUTPUT_DIRECTORY` only after capture completes. Original bundled evidence should not be overwritten.
