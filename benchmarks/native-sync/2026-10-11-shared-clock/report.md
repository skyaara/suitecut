# SuiteCut native shared-clock implementation and verification

Saved 11 October 2026 (Asia/Calcutta). All output media, logs, process samples, source/encoder PCM diagnostics, fuzz corpora and build evidence remain in this directory; JSON measurements retain UTC timestamps. Historical publication results were preserved.

## Result

Native audio and video workers now hold at a common media boundary while CEF capture continues. This fixes the old 500 ms encoder lead allowance. Captured samples are retained during recoverable worker delays; absent source samples are counted and remain visible. This is not a claim that CEF capture continuity or perceptual lip sync is solved.

## Implementation

- Both tracks use one monotonic origin. A worker starts a block only after its partner completes through that block’s start. The maximum completed-position lead is one video/audio codec block, including codec-specific frame sizes.
- Requested CEF audio capture waits for its first PCM block, including silent PCM, before choosing the common epoch. Silent pages with no requested stream remain recordable. Stock CEF creates loopback capture after detecting audibility; it cannot announce a future page sound in advance. These continuous-audio benchmarks first start the AudioWorklet and wait for eight captured PCM callbacks; the per-run readiness metadata records that warmup. This is not a cold-first-sound guarantee.
- CEF now requests 1,024 frames at 48 kHz stereo, matching OBS Browser Source. The two 60-second pilots do not establish that block size alone causes a reliability improvement.
- The jitter buffer starts at 150 ms and adapts up to 960 ms. Capture queues remain bounded; late, missing and overflow-dropped frames are separate counters. Stop drains and releases retained ownership.
- Realtime video selects the nearest buffered capture within half a frame interval; distant future frames remain held. A decoded FFV1 regression verifies three jittered captures remain ordered/distinct at 60 FPS. Offline ordering is unchanged.
- H.264 uses one slice thread through 1080p, two above 1080p and three at 4K. 4K uses ultrafast instead of veryfast to leave capture headroom on this four-core quota. This changes compression efficiency; no matched perceptual-quality or GPU-encoder comparison is claimed.
- The persistent-audio candidate was rebased onto CEF revision 708dc14 and dry-applies with zero patch fuzz. It is not compiled or enabled in these binaries; this host has a 32 GiB filesystem with under 2 GiB free. It addresses silent/first-sound capture lifecycle, not recovery of interior lost Chromium samples.
- New status includes active-source missing frames, each worker’s coordination waits and maximum encoder-progress lead. The latter is not a lip-sync measurement.

## Verification

- Eight CTest cases pass in the normal build and again with Clang 19 AddressSanitizer/UndefinedBehaviorSanitizer and LeakSanitizer enabled. Includes 160 concurrent ownership/lifecycle cycles, codec checks, scaler-failure cleanup, silent/requested startup, timeouts, pause/resume and shutdown.
- A traced 60-second real-browser repeat blocks the Node controller for 2,000 ms: native capture receives 94 audio callbacks while encoders finish 60 video frames and 96,256 audio frames, with no missing input, PCM copy mismatch or decoded quiet interval. The untraced earlier case’s severe interruption preceded its intentional stall; its cause remains unisolated. See tests/controller-stall-observation.json.
- 24 focused SDK tests pass; TypeScript build passes. These are existing native adapters; full Chromium and external-browser memory safety are outside this run.
- Four injected 250 ms encoder stalls (audio/video × 30/60 FPS) preserve every decoded PCM sample in order, with zero missing/late/dropped frames and a one-block progress bound. Restoring only the old 500 ms allowance fails this regression; see logs/tests-negative-control.log.

- bounds libFuzzer: 601.7 seconds, 68693807 executed units, exit 0, artifacts 0, corpus 46 files, peak sanitizer-process RSS 492 MiB. See logs/fuzz-bounds.log.
- media libFuzzer: 601.4 seconds, 19823 executed units, exit 0, artifacts 0, corpus 186 files, peak sanitizer-process RSS 233 MiB. See logs/fuzz-media.log.

## Capture results

Recording and loopback RTMP streaming run separately. Each resolution uses two active minutes; the controller-stall case uses one minute; the memory soak uses 30 minutes. Every decoded output is inspected for actual dimensions, marker/PTS progress, captured click/text/select/scroll states, continuous-audio quiet intervals and known sample-content correlation against the recorded AudioContext clock. The table shows final-code trials only. All earlier tuning/failed/interrupted trials remain in summary.csv and runs/*/analysis.json, with their original media and logs.

| Case | Mean CPU cores | Peak RSS MiB | Distinct FPS | Max visual hold ms | Audio quiet total / max ms | Encoder lead ms | Quality checks |
|---|---:|---:|---:|---:|---:|---:|---|
| 720p60-record | 0.66 | 1304.2 | 59.44 | 150.0 | 505 / 145 | 21.33 | Defects observed |
| 720p60-stream | 0.63 | 1340.3 | 59.48 | 133.0 | 745 / 90 | 21.33 | Defects observed |
| 1080p30-record | 0.60 | 1408.1 | 29.44 | 433.0 | 1255 / 220 | 33.33 | Defects observed |
| 1080p30-stream | 0.72 | 1411.1 | 29.52 | 234.0 | 1720 / 200 | 33.33 | Defects observed |
| 4k30-record | 1.55 | 1723.4 | 28.26 | 133.0 | 50 / 25 | 33.33 | Defects observed |
| 4k30-stream | 1.51 | 1677.2 | 28.60 | 166.0 | 130 / 100 | 32.00 | Defects observed |
| controller-stall-1080p30 | 1.08 | 1495.1 | 27.95 | 2334.0 | 2780 / 2170 | 33.33 | Defects observed |
| soak-1080p30-stream | 0.60 | 1408.0 | 29.70 | 400.0 | 15030 / 275 | 33.33 | Defects observed |

Final encoder budgets: 720p60-record: 1 threads / veryfast, 720p60-stream: 1 threads / veryfast, 1080p30-record: 1 threads / veryfast, 1080p30-stream: 1 threads / veryfast, 4k30-record: 3 threads / ultrafast, 4k30-stream: 3 threads / ultrafast, controller-stall-1080p30: 1 threads / veryfast, soak-1080p30-stream: 1 threads / veryfast

![Resolution results](resolution-results.png)

Earlier one-thread 4K trials failed to drain before the SDK stop timeout. Three slices alone with veryfast still accumulated substantial backlog. Ultrafast plus three slices is the final 4K configuration; the retained tuning trials show the performance/compression tradeoff.

A separate set of trials encountered cgroup file-cache pressure despite modest live process RSS. Those trials are labeled cache-pressure and excluded from the final table. Only owned clean files were advised out of the page cache between trials; no file bytes were deleted or modified. The interruption and cleanup records remain under tests/. Final trials record cgroup memory events so this confound remains inspectable.

## Remaining defects and attribution

- **Failed capture 1080p30-record-cache-pressure**: AssertionError('Matrix interrupted after severe host file-cache pressure; finalization was not captured in the last running snapshot.'). Retained under runs/1080p30-record-cache-pressure; not counted as a successful quality or performance trial.
- **Failed capture 4k30-record-single-thread**: AssertionError('Error: Native browser command timed out'). Retained under runs/4k30-record-single-thread; not counted as a successful quality or performance trial.
- **Failed capture 4k30-stream-single-thread**: AssertionError('Error: Native browser command timed out'). Retained under runs/4k30-stream-single-thread; not counted as a successful quality or performance trial.
- **1080p30-record**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 98.98 ms across 4 accepted anchors.
- **1080p30-record-before-nearest-video**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 110.56 ms across 4 accepted anchors.
- **1080p30-record-initial**: Silence in continuous source; Video marker held beyond two frame intervals; Video does not cover the requested active duration; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 67.42 ms across 3 accepted anchors.
- **1080p30-stream**: Silence in continuous source; Video marker held beyond two frame intervals Content-clock max absolute offset: 40.08 ms across 3 accepted anchors.
- **1080p30-stream-before-nearest-video**: Silence in continuous source; Video marker held beyond two frame intervals; Video does not cover the requested active duration; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 119.42 ms across 4 accepted anchors.
- **1080p30-stream-initial**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 103.17 ms across 4 accepted anchors.
- **4k30-record**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 85.50 ms across 4 accepted anchors.
- **4k30-record-before-nearest-video**: Silence in continuous source; Video marker held beyond two frame intervals; Video does not cover the requested active duration; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 111.65 ms across 4 accepted anchors.
- **4k30-record-three-threads-veryfast**: Silence in continuous source; Video marker held beyond two frame intervals; Video does not cover the requested active duration; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 113.46 ms across 3 accepted anchors.
- **4k30-record-two-threads**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 74.69 ms across 4 accepted anchors.
- **4k30-stream**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 51.08 ms across 3 accepted anchors.
- **4k30-stream-before-nearest-video**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 52.40 ms across 4 accepted anchors.
- **4k30-stream-three-threads-veryfast**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 67.48 ms across 2 accepted anchors.
- **720p60-record**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 32.71 ms across 4 accepted anchors.
- **720p60-record-before-nearest-video**: Silence in continuous source; Video marker held beyond two frame intervals Content-clock max absolute offset: 24.77 ms across 4 accepted anchors.
- **720p60-record-cache-pressure**: Recorded input-state sequence missing/out of order: 0; Silence in continuous source; Video marker held beyond two frame intervals; Video does not cover the requested active duration; Native audio dropped/arrived after publication Content-clock max absolute offset: 11.06 ms across 2 accepted anchors.
- **720p60-record-initial**: Silence in continuous source; Video marker held beyond two frame intervals Content-clock max absolute offset: 22.06 ms across 3 accepted anchors.
- **720p60-stream**: Silence in continuous source; Video marker held beyond two frame intervals Content-clock max absolute offset: 25.81 ms across 4 accepted anchors.
- **720p60-stream-before-nearest-video**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 75.60 ms across 4 accepted anchors.
- **720p60-stream-cache-pressure**: Silence in continuous source; Video marker held beyond two frame intervals Content-clock max absolute offset: 23.42 ms across 4 accepted anchors.
- **720p60-stream-initial**: Silence in continuous source; Video marker held beyond two frame intervals Content-clock max absolute offset: 21.56 ms across 4 accepted anchors.
- **compare-1024-1080p30**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 97.29 ms across 2 accepted anchors.
  Traced interior missing input: 319.67 ms; eventually uncovered: 319.67 ms; source-copy mismatched frames: 0; silent source packets across the full trace (including warmup/shutdown): 4; silent packets fully within the output window: 1; raw timestamp gaps: 5; normalizer-introduced gap events: 0. Full source timestamps and raw PCM are retained.
- **compare-960-1080p30**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 121.98 ms across 2 accepted anchors.
  Traced interior missing input: 560.00 ms; eventually uncovered: 560.00 ms; source-copy mismatched frames: 0; silent source packets across the full trace (including warmup/shutdown): 7; silent packets fully within the output window: 4; raw timestamp gaps: 11; normalizer-introduced gap events: 0. Full source timestamps and raw PCM are retained.
- **controller-stall-1080p30**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 80.98 ms across 2 accepted anchors.
- **controller-stall-1080p30-before-nearest-video**: Silence in continuous source; Video marker held beyond two frame intervals Content-clock max absolute offset: 19.40 ms across 1 accepted anchors.
- **controller-stall-1080p30-initial**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 44.92 ms across 2 accepted anchors.
- **controller-stall-traced-1080p30**: No listed delivery defects observed. Content-clock max absolute offset: 21.08 ms across 2 accepted anchors.
  Traced interior missing input: 0.00 ms; eventually uncovered: 0.00 ms; source-copy mismatched frames: 0; silent source packets across the full trace (including warmup/shutdown): 2; silent packets fully within the output window: 0; raw timestamp gaps: 0; normalizer-introduced gap events: 0. Full source timestamps and raw PCM are retained.
- **diagnostic-4k30-record-ultrafast**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 69.98 ms across 4 accepted anchors.
  Traced interior missing input: 170.67 ms; eventually uncovered: 170.67 ms; source-copy mismatched frames: 0; silent source packets across the full trace (including warmup/shutdown): 5; silent packets fully within the output window: 2; raw timestamp gaps: 5; normalizer-introduced gap events: 0. Full source timestamps and raw PCM are retained.
- **lifecycle-1080p30**: Silence in continuous source; No accepted content-clock alignment anchors Content-clock max absolute offset: unavailable ms across 0 accepted anchors.
  Traced interior missing input: 0.00 ms; eventually uncovered: 0.00 ms; source-copy mismatched frames: 0; silent source packets across the full trace (including warmup/shutdown): 2; silent packets fully within the output window: 2; raw timestamp gaps: 0; normalizer-introduced gap events: 0. Full source timestamps and raw PCM are retained.
- **nearest-pilot-720p60**: Silence in continuous source; Video marker held beyond two frame intervals Content-clock max absolute offset: 24.35 ms across 1 accepted anchors.
  Traced interior missing input: 85.33 ms; eventually uncovered: 85.33 ms; source-copy mismatched frames: 0; silent source packets across the full trace (including warmup/shutdown): 8; silent packets fully within the output window: 4; raw timestamp gaps: 4; normalizer-introduced gap events: 0. Full source timestamps and raw PCM are retained.
- **pilot-1080p30**: Silence in continuous source; Video marker held beyond two frame intervals Content-clock max absolute offset: 24.54 ms across 1 accepted anchors.
  Traced interior missing input: 200.00 ms; eventually uncovered: 200.00 ms; source-copy mismatched frames: 0; silent source packets across the full trace (including warmup/shutdown): 5; silent packets fully within the output window: 4; raw timestamp gaps: 4; normalizer-introduced gap events: 0. Full source timestamps and raw PCM are retained.
- **soak-1080p30-stream**: Silence in continuous source; Video marker held beyond two frame intervals; Source audio content differs from AudioContext marker beyond one video interval + 10 ms marker resolution Content-clock max absolute offset: 110.48 ms across 59 accepted anchors.

The previous matched OBS/CEF audio report also observed quiet source PCM and output interruptions on this host (a 105 ms longest quiet interval in the 60-second tapped matched OBS recording). That historical trial used a different capture sequence and is not a direct performance comparison with this matrix.

A zero copy-mismatch count establishes preservation of the available traced PCM, not that Chromium generated every required sample. CEF’s capture implementation discards Chromium glitch metadata. The retained pinned source shows audibility-triggered loopback creation; larger capture timestamp discontinuities and provided zero PCM survive container multiplexing. A container cannot recover those samples.

Content-clock offsets use deterministic waveform correlation and the nearest encoded 10 ms AudioContext marker. They include browser scheduling/capture effects and marker quantization; they exclude display/network latency and are not a direct perceptual lip-sync certificate. Every correlation/anchor is retained.

## Sustained memory profile

30-minute 1080p30 streaming: peak RSS 1408.0 MiB, peak PSS 722.54 MiB, peak USS 497.98 MiB. Slopes after the first two minutes: RSS 1.04 MiB/min, PSS 0.94 MiB/min, USS 0.92 MiB/min, Node heap 0.07 MiB/min. Summed RSS double-counts some shared pages; PSS/USS are recorded every ten seconds. Peak retained queues: video 32.63 MiB and audio 0.06 MiB; adaptive delay changed from 150.00 to at most 297.09 ms. The fourth chart shows that retained-buffer history alongside memory. Growth alone does not prove a leak, and a finite test cannot establish universal leak freedom.

![Memory profile](memory-profile.png)

## Scope and reproduction

Full fuzz campaigns use 600 seconds per target, a 512-byte input limit, five-second per-input timeout and 1 GiB RSS limit. Earlier short smoke and interrupted campaigns are retained separately and are not counted as the final campaigns.

Kernel CPU quota: four cores. Kernel RAM cap: 16 GiB. Capture trees use affinity on four workload CPUs and an 8 GiB aggregate summed-RSS termination guard sampled every 500 ms; this is not a strict 8 GiB kernel cap, and shared pages can be counted repeatedly.

Product CPU/RSS includes the SDK controller, native capture/encoder process and browser children. The loopback receiver, Xvfb and PulseAudio are excluded from product totals but retained in the raw resource inventory and included in the RSS guard. Samples are every 500 ms; no intentional capture/build/decoder/fuzzer overlap. Four-core affinity does not provide an isolated physical host.

Loopback HTTP fixture and loopback RTMP receiver; no external network streaming, WebRTC or GPU validation.

CEF and the dynamically linked FFmpeg libraries were not rebuilt with sanitizers. The campaigns instrument SuiteCut native code and inspect allocator leaks in the tested processes. They do not certify complete CEF/Chromium, FFmpeg, graphics-driver, browser-automation or GPU memory safety.

Run from the repository root:

```sh
python benchmarks/native-sync/shared-clock-matrix.py /absolute/path/to/new-evidence-dir
SYNC_DIRECTORY=/absolute/path/to/new-evidence-dir/runs/soak-1080p30-stream SYNC_DURATION_MS=1800000 SYNC_MODE=stream SYNC_PROFILE=memory python benchmarks/native-sync/shared-clock-runtime.py
python benchmarks/native-sync/analyze-shared-clock.py /absolute/path/to/new-evidence-dir/runs/*
python benchmarks/native-sync/fuzz-shared-clock.py /absolute/path/to/new-evidence-dir 600
python benchmarks/native-sync/report-shared-clock.py /absolute/path/to/new-evidence-dir
```

The fuzz campaign requires the retained Clang sanitizer build and initialized corpus directories. Build flags, binary/source hashes, runtime versions and host scope are saved under tests/ and logs/. Failed calibration captures remain under runs/pilot-1080p30 and runs/lifecycle-1080p30; they are diagnostics, not successful final matrix trials.

OBS behavior was taken from the pinned review in ../../obs-cef/2026-10-10-audio-source-review and the existing matched captures in ../../obs-cef/2026-10-10-native-audio-profile. This run does not add a new OBS performance comparison or claim better capture quality than OBS.
