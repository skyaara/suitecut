# SuiteCut and OBS: 1080p / 30 FPS browser capture

Generated 2026-10-10T22:15:20.241249+05:30 (IST). Series status: **completed**, 55/55 runs saved.

This report compares historical SuiteCut TypeScript capture, the native C++ implementation, and OBS on a deterministic local web application. Recording and streaming are separate workloads. Results describe this software-only cloud host and these versions; they are not a universal ranking or proof of memory safety.

**The native external-browser capture adapter is unavailable.** The new `connectBrowser` SDK supports browser automation, but does not implement native recording/streaming of an external browser. That requested row is deliberately shown as unavailable.

## Measurement protocol

- Fresh owned browser/display/audio/OBS processes per run, fixed 1920×1080 page viewport, 30 FPS output, four CPU affinity slots (0–3), and an actual four-core cgroup quota.
- Requested RAM budget: 8 GiB. This host has a 16 GiB kernel limit; an 8 GiB sampled sum-of-RSS guard terminates the workload if exceeded. It is **not an enforced 8 GiB kernel limit** and can miss subsecond peaks.
- Five 30-second trials per available backend per mode, serially rotated/reversed in order. Each trial runs 10 seconds of low-motion marker-only content, 10 seconds of animation and form input, and 10 seconds of actual wheel scrolling.
- A fixed 30 Hz wall-clock marker is decoded from every output video frame. The marker remains active in the low-motion phase. Distinct marker FPS and longest held image measure delivered motion; a 30 FPS container alone does not establish 30 FPS visual updates.
- Recorded action markers verify click, text entry, selection, and scroll states in order (0→1→3→7→15). Historical CEF text fill dispatches DOM input; Playwright paths use its own input implementation. These are representative operations, not full Playwright conformance.
- H.264 software encoding, `veryfast`, zero latency, one video encoder thread, no B frames, GOP 60. Recordings use CRF 18; streaming targets 4500 kbit/s video and AAC 48 kHz stereo at 128 kbit/s. Encoder implementations/library generations can still differ.
- Recording source is silent. SuiteCut records video only; the tested OBS recording profile imposes an additional silent AAC track. This is a disclosed unmatched audio-encoding cost; streaming is the matched audio workload.
- Continuous deterministic AudioWorklet audio during streaming; no intentional interior silence. Streaming goes to a local RTMP receiver, saved without re-encoding. This tests capture/encode/mux, not remote Internet or service performance. The receiver accepts one connection; it does not model production RTMP reconnection. The historical startup retry failure is retained under this condition.
- Audio silence is measured from decoded samples in 5 ms RMS bins, threshold 0.001. Leading/trailing silence is reported separately from interior silence. It is signal-specific evidence, not proof that every nonzero sample is correct.
- Audio/video offsets use deterministic waveform matching against the video’s 100 Hz AudioContext marker; only correlation ≥0.85 anchors count. Quantization includes 10 ms marker steps and video-frame timing. These are synchronization observations, not end-to-end latency.
- Browser/encoder/controller process resources sampled every 500 ms; descendant inventory every second. PSS/USS sampled every 10 seconds for memory runs when allowed by `/proc`. Receiver, Xvfb and Pulse are saved separately and excluded from product totals. Common Node harness imports are included for every row.
- Sum of RSS double-counts shared pages. Complete PSS requires every product process to be readable; partial readings are not presented as complete PSS. CPU cores 1.00 means 100% of one CPU core.
- No forced GC, sanitizer, tracing preload, other benchmark, build or video analysis during the active published captures. Media analysis happens after each runtime has shut down.
- Memory soaks use the same repeating 30-second workload for 10 minutes per backend; C++ CEF additionally runs to 30 minutes. Memory slope uses samples after the first two minutes. Compare common first-10-minute windows before making duration-sensitive claims.

## Versions and comparison boundaries

| Component | Version / boundary |
| --- | --- |
| SuiteCut C++ | 2.0.0 unpublished development checkout; pinned source/SDK/binary snapshots |
| Historical SuiteCut TS | 2d4158c95217b2b4099898a2508a40f3fa4f121a; original capture/queues plus isolated CEF build compatibility patch and encoder-settings adapter |
| SuiteCut CEF | 152.0.6 / Chromium 152.0.7977.83; identical libcef SHA-256 for TS and C++ |
| OBS | 32.2.0, embedded Chromium 127.0.6533.120; OBS-patched CEF distribution |
| Standalone browser | Playwright Chromium 151.0.7922.34 / Playwright 1.62.1 |
| FFmpeg | SuiteCut/system 7.1.5; OBS bundled libav generation 6 |
| Host CPU | INTEL(R) XEON(R) PLATINUM 8573C; Four affinity CPUs and four-core cgroup quota on a shared managed cloud container, not dedicated physical cores |
| Graphics | No /dev/dri hardware GPU device; software rendering/encoding |
| OBS external source | XSHM screen capture of standalone Chromium, 87-pixel UI crop; Pulse monitor for streaming audio |
| C++ external browser | Automation verified separately; native capture unavailable |

Different CEF/Chromium versions, OBS software graphics/composition, screenshot versus native OSR capture, FFmpeg/libav versions, codec rate-control details, and the OBS silent recording track limit causal claims. The historical CEF shim uses the same CEF as C++ to control one of these differences; its small compatibility patch is saved. The historical snapshot is not modified to fix its capture algorithm. These whole-pipeline tests do not isolate programming-language choice or prove that JavaScript single-thread execution alone causes any difference.

## Principal measured findings

Across the short decoded trials, C++ CEF used median 0.67 CPU cores recording and 0.94 streaming, versus OBS CEF’s 2.79 and 2.97. This is 76.1% and 68.4% lower product CPU in this configuration, with median distinct motion 29.4 / 28.3 FPS for C++.

Streaming audio continuity remains a correctness issue: C++ median interior silence was 1080 ms per saved short output versus OBS CEF’s 305 ms. CPU savings do not establish superior overall capture quality. Every available path had at least one short streaming trial with detected interior silence. One historical CEF streaming attempt failed before completing its active workload; it remains in the denominators and evidence.

C++ did not use less summed RSS than OBS CEF: median short-trial peak RSS was 1230 MiB recording and 1354 MiB streaming, versus OBS CEF’s 1068 / 1129 MiB. Different browser generations and shared mappings limit causal interpretation.

Native external-browser capture remains unimplemented. These results support a CPU-efficiency improvement for the available native CEF path; they do not support declaring every browser flow ready or leak-free.

The native C++ thirty-minute output still contained 41.69 seconds of interior silence, a longest quiet interval of 555 ms, and a longest held video image of 11.40 seconds. Peak summed RSS was 1539 MiB, with RSS/PSS growth of 1.04/0.97 MiB/min after the first two minutes. These observations leave capture-fidelity work and memory-growth investigation open; they do not justify a leak-free or production-ready claim.

## Record trials

Cells show median [minimum–maximum] across all decoded trials, including quality failures. Every individual trial is also saved in `trials.csv` and `runs.json`. A validation pass requires decoded marker/action checks and, for streaming, no detected interior silence. It does not certify 30 distinct FPS; motion performance is reported separately. Process cleanup success is reported independently.

| Backend | Decoded | Validation pass | Mean CPU cores | Peak RSS MiB | Distinct FPS | Longest hold ms |
| --- | --- | --- | --- | --- | --- | --- |
| SuiteCut historical TS / CEF | 5/5 | 2/5 | 2.43 [2.38–2.57] | 1293 [1273–1313] | 6.89 [1.63–20.37] | 3234 [267–5933] |
| SuiteCut historical TS / Chromium | 5/5 | 5/5 | 1.97 [1.91–2.28] | 1288 [1266–1315] | 25.97 [22.23–27.54] | 367 [334–467] |
| OBS / embedded CEF | 5/5 | 5/5 | 2.79 [2.70–2.89] | 1068 [1065–1069] | 26.83 [23.42–29.30] | 266 [133–600] |
| OBS / Chromium screen capture | 5/5 | 5/5 | 2.73 [2.58–2.84] | 1370 [1362–1375] | 25.08 [19.40–27.20] | 267 [200–566] |
| SuiteCut C++ / CEF | 5/5 | 5/5 | 0.67 [0.63–1.10] | 1230 [1228–1278] | 29.40 [28.73–29.93] | 200 [67–267] |
| SuiteCut C++ / external browser | Unavailable | — | — | — | — | — |

![record comparison](figures/record-comparison.png)

## Stream trials

Cells show median [minimum–maximum] across all decoded trials, including quality failures. Every individual trial is also saved in `trials.csv` and `runs.json`. A validation pass requires decoded marker/action checks and, for streaming, no detected interior silence. It does not certify 30 distinct FPS; motion performance is reported separately. Process cleanup success is reported independently.

| Backend | Decoded | Validation pass | Mean CPU cores | Peak RSS MiB | Distinct FPS | Longest hold ms |
| --- | --- | --- | --- | --- | --- | --- |
| SuiteCut historical TS / CEF | 4/5 | 0/5 | 2.77 [2.73–2.87] | 1487 [1485–1512] | 22.38 [19.34–25.09] | 1015 [759–1137] |
| SuiteCut historical TS / Chromium | 5/5 | 0/5 | 2.77 [2.44–2.85] | 1616 [1592–1638] | 21.47 [19.72–23.40] | 334 [266–467] |
| OBS / embedded CEF | 5/5 | 0/5 | 2.97 [2.85–3.08] | 1129 [1126–1135] | 26.07 [22.36–28.00] | 200 [167–533] |
| OBS / Chromium screen capture | 5/5 | 0/5 | 2.93 [2.82–3.01] | 1456 [1451–1461] | 25.23 [18.10–25.88] | 333 [234–667] |
| SuiteCut C++ / CEF | 5/5 | 1/5 | 0.94 [0.80–1.08] | 1354 [1346–1451] | 28.26 [25.21–29.46] | 300 [67–467] |
| SuiteCut C++ / external browser | Unavailable | — | — | — | — | — |

![stream comparison](figures/stream-comparison.png)

| Backend | Interior silence total ms | Longest interior ms | Leading silence ms | Median audio−video ms |
| --- | --- | --- | --- | --- |
| SuiteCut historical TS / CEF | 1078 [380–1830] | 178 [95–380] | 152 [60–170] | -56.94 [-126.15–2.12] |
| SuiteCut historical TS / Chromium | 1525 [825–3025] | 135 [110–255] | 190 [190–225] | — |
| OBS / embedded CEF | 305 [135–1610] | 100 [55–465] | 5 [5–10] | 1.13 [-34.00–21.31] |
| OBS / Chromium screen capture | 695 [620–1570] | 235 [135–375] | 5 [0–5] | -24.79 [-55.25–-16.83] |
| SuiteCut C++ / CEF | 1080 [0–1500] | 215 [0–265] | 5 [5–10] | 50.29 [6.71–72.00] |

## Audio loss during the active workload

The continuous test source has no intentional silence. Interior-only measurements can hide a stream that loses audio and never recovers: the historical external-Chromium ten-minute output contains about 550 seconds of trailing silence. The table below counts all quiet bins within the video-marker active window, including silence that continues to its end. The first and last selected output frames bound this decoded window; these are 5 ms signal-specific measurements.

| Backend | Active quiet total ms | Longest active quiet ms | Full-output trailing silence ms |
| --- | --- | --- | --- |
| SuiteCut historical TS / CEF | 628 [380–1830] | 170 [95–380] | 0 [0–0] |
| SuiteCut historical TS / Chromium | 1510 [785–3000] | 135 [110–255] | 0 [0–0] |
| OBS / embedded CEF | 300 [135–1610] | 100 [55–465] | 0 [0–0] |
| OBS / Chromium screen capture | 695 [620–1570] | 235 [135–375] | 0 [0–0] |
| SuiteCut C++ / CEF | 1080 [0–1500] | 215 [0–265] | 0 [0–5] |

| Backend | Minutes | Active quiet seconds | Longest active quiet ms | Active quiet % | Full-output trailing silence ms |
| --- | --- | --- | --- | --- | --- |
| OBS / Chromium screen capture | 10 | 12.46 | 550 | 2.08 | 0 |
| SuiteCut historical TS / CEF | 10 | 407.29 | 165645 | 68.05 | 6995 |
| OBS / embedded CEF | 10 | 44.70 | 21760 | 7.43 | 2655 |
| SuiteCut historical TS / Chromium | 10 | 561.62 | 550190 | 93.64 | 550190 |
| SuiteCut C++ / CEF | 30 | 41.69 | 555 | 2.32 | 0 |

## Native audio and encoding counters

The native output distinguishes discarded packets from missing samples compensated with silence. A zero `droppedAudioSamples` counter does not mean loss-free audio. Decoded silence also includes quiet source samples and codec effects, so it need not equal the missing-sample counter. Empty final queues show drain completion rather than zero active buffering. These counters identify the observed compensation behavior; they do not by themselves establish whether source callback timing, scheduling or another implementation detail caused it.

| Run | Missing samples equivalent ms | Discarded audio samples | Maximum encode ms | Reported buffered delay ms | Decoded interior silence ms |
| --- | --- | --- | --- | --- | --- |
| stream-cpp-cef-01 | 1015.7 | 0 | 339.0 | 416.0 | 1500 |
| stream-cpp-cef-02 | 183.3 | 0 | 140.0 | 150.0 | 270 |
| stream-cpp-cef-03 | 573.5 | 0 | 326.2 | 541.9 | 1090 |
| stream-cpp-cef-04 | 830.9 | 0 | 264.5 | 201.6 | 1080 |
| stream-cpp-cef-05 | 3.9 | 0 | 92.1 | 150.0 | 0 |
| memory-cpp-cef | 22320.0 | 0 | 594.1 | 465.2 | 41690 |

## CPU by browser workload phase

Median CPU cores across decoded short trials; per-process-role and per-phase details remain in `enriched-runs.json`. Native linked encoders run within the browser process, OBS encoding runs within OBS, and historical FFmpeg encoders are separate processes, so role labels are not identical work boundaries.

| Backend | Mode | Low motion CPU | Animation/input CPU | Scrolling CPU |
| --- | --- | --- | --- | --- |
| SuiteCut historical TS / CEF | record | 2.33 [2.29–2.61] | 2.50 [2.35–2.69] | 2.48 [2.44–2.59] |
| SuiteCut historical TS / Chromium | record | 1.88 [1.79–2.20] | 2.00 [1.96–2.06] | 2.13 [1.89–2.57] |
| OBS / embedded CEF | record | 2.65 [2.54–2.80] | 2.80 [2.63–3.06] | 2.94 [2.87–2.99] |
| OBS / Chromium screen capture | record | 2.65 [2.53–2.73] | 2.70 [2.54–2.87] | 2.85 [2.68–2.98] |
| SuiteCut C++ / CEF | record | 0.53 [0.52–0.62] | 0.69 [0.62–1.58] | 0.77 [0.69–1.19] |
| SuiteCut historical TS / CEF | stream | 2.78 [2.74–2.90] | 2.68 [2.62–2.92] | 2.85 [2.70–2.92] |
| SuiteCut historical TS / Chromium | stream | 2.55 [2.46–2.99] | 2.70 [2.50–3.21] | 2.65 [2.37–3.23] |
| OBS / embedded CEF | stream | 2.86 [2.67–3.08] | 3.00 [2.85–3.07] | 3.04 [2.97–3.17] |
| OBS / Chromium screen capture | stream | 2.90 [2.70–2.98] | 2.96 [2.83–3.01] | 2.95 [2.94–3.12] |
| SuiteCut C++ / CEF | stream | 0.79 [0.63–0.92] | 0.79 [0.70–1.03] | 1.21 [0.86–1.63] |

## Memory profiles

A flat RSS curve does not prove absence of leaks; allocator caches, browser caches and shared mappings affect RSS. Positive slope is evidence to investigate, not a leak diagnosis. Sanitizer results, if supplied separately, cover instrumented SuiteCut code rather than the prebuilt CEF binary.

| Backend | Minutes | Peak RSS MiB | RSS MiB/min after 2m | Peak PSS MiB | Complete PSS samples | Node heap MiB/min | Validation pass |
| --- | --- | --- | --- | --- | --- | --- | --- |
| OBS / Chromium screen capture | 10 | 1465 | 2.05 | 844 | 59 | 0.13 | False |
| SuiteCut historical TS / CEF | 10 | 1576 | 3.18 | 978 | 58 | 0.04 | False |
| OBS / embedded CEF | 10 | 1138 | 1.46 | 715 | 57 | 0.11 | False |
| SuiteCut historical TS / Chromium | 10 | 1708 | 6.59 | 932 | 58 | 0.30 | False |
| SuiteCut C++ / CEF | 30 | 1539 | 1.04 | 893 | 179 | 0.07 | False |

![Memory profiles](figures/memory-profiles.png)


## Sustained streaming quality

These are separate long-run observations, not additional short-trial repetitions. Silence totals cover the full saved output; the percentage normalizes for output duration. Native C++ runs longer, so compare normalized rates and the common first-ten-minute resource window.

| Backend | Minutes | Distinct FPS | Longest hold ms | Interior silence seconds | Longest silence ms | Interior silence % | Process exit |
| --- | --- | --- | --- | --- | --- | --- | --- |
| OBS / Chromium screen capture | 10 | 25.19 | 1300 | 12.46 | 550 | 2.07 | 0 |
| SuiteCut historical TS / CEF | 10 | 17.94 | 7441 | 401.82 | 165645 | 66.68 | 1 |
| OBS / embedded CEF | 10 | 21.62 | 10633 | 42.19 | 21760 | 7.00 | 0 |
| SuiteCut historical TS / Chromium | 10 | 17.87 | 10358 | 11.45 | 3540 | 1.90 | 0 |
| SuiteCut C++ / CEF | 30 | 27.91 | 11400 | 41.69 | 555 | 2.31 | 0 |

![Sustained streaming audio continuity](figures/audio-continuity.png)

## Private memory and Node heap

Private-memory metrics use complete product-process snapshots. Node heap/external trends use natural garbage collection; they are not forced-GC retained-heap measurements.

| Backend | PSS MiB/min after 2m | Peak USS MiB | USS MiB/min after 2m | Node heap MiB/min | Node external MiB/min |
| --- | --- | --- | --- | --- | --- |
| OBS / Chromium screen capture | 1.99 | 630 | 1.97 | 0.13 | 0.13 |
| SuiteCut historical TS / CEF | 3.23 | 772 | 3.20 | 0.04 | -0.06 |
| OBS / embedded CEF | 1.79 | 548 | 1.76 | 0.11 | 0.11 |
| SuiteCut historical TS / Chromium | 6.49 | 696 | 6.35 | 0.30 | 0.30 |
| SuiteCut C++ / CEF | 0.97 | 671 | 0.96 | 0.07 | 0.14 |

## Common 10-minute memory window

The resource window compares the first ten active minutes. Audio values cover the first 600 seconds of saved output and include quiet output edges. The full 30-minute C++ profile is additional durability evidence.

| Backend | Mean CPU cores | Peak RSS MiB | RSS MiB/min after 2m | First minute RSS MiB | Last minute RSS MiB | First 600s quiet audio seconds | First 600s quiet audio % |
| --- | --- | --- | --- | --- | --- | --- | --- |
| OBS / Chromium screen capture | 2.97 | 1465 | 2.05 | 1452 | 1453 | 12.47 | 2.08 |
| SuiteCut historical TS / CEF | 2.66 | 1576 | 3.18 | 1477 | 1560 | 406.39 | 67.73 |
| OBS / embedded CEF | 3.13 | 1138 | 1.45 | 1125 | 1123 | 42.45 | 7.07 |
| SuiteCut historical TS / Chromium | 2.97 | 1708 | 6.59 | 1618 | 1679 | 559.35 | 93.22 |
| SuiteCut C++ / CEF | 1.00 | 1534 | 0.70 | 1340 | 1384 | 13.45 | 2.24 |

## Observer and host context

Observer, receiver and display costs are excluded from the product CPU/RSS tables but consume the same host CPU quota. Host throttling counters cover the entire cgroup; unavailable CPU/memory pressure files are explicitly flagged in raw samples. Audio statistics cover the full saved output, including startup/finalization, while product resource totals cover the marked active workload.

| Backend | Observer CPU cores | Observer p95 ms | Host throttled-period fraction |
| --- | --- | --- | --- |
| SuiteCut historical TS / CEF | 0.017 [0.014–0.035] | 41.6 [29.2–84.7] | 0.004 [0.000–0.034] |
| SuiteCut historical TS / Chromium | 0.019 [0.011–0.039] | 29.2 [16.8–126.7] | 0.000 [0.000–0.055] |
| OBS / embedded CEF | 0.018 [0.014–0.037] | 34.1 [23.8–91.2] | 0.004 [0.000–0.047] |
| OBS / Chromium screen capture | 0.019 [0.014–0.027] | 38.9 [23.1–68.7] | 0.012 [0.004–0.043] |
| SuiteCut C++ / CEF | 0.015 [0.009–0.030] | 22.1 [9.7–53.7] | 0.000 [0.000–0.012] |


## Verification and preserved earlier evidence

Fresh ASan/UBSan verification passed 7/7 native tests with leak detection and assertions enabled. Instrumented scope: SuiteCut media, audio clock/startup, codec, pixel conversion, memory lifecycle and scaler failure. Prebuilt CEF and FFmpeg libraries are not fully instrumented. Saved configuration, compile flags and test logs are included. No sanitizer errors were reported in these tests; this does not prove universal browser or long-running leak freedom.

All 15 checksum manifests covering 9066 saved payloads pass. Three older evidence-files manifests also pin mutable live checkout/build paths, with 82 changes from later authorized development. No failed entry is inside a dated benchmark directory; no original manifest was edited or current source rolled back.

The historical default recording codec was VP9. This controlled comparison adapts it to H.264 with the common encoder settings, and rebuilds its shim on common CEF 152. It is a reconstructed pipeline comparison, rather than a benchmark of untouched historical shipping defaults. OS caches were not flushed; trial order is balanced and browser profiles are fresh.

The exact executed capture versions are saved as `capture-before-final-scroll-validation.mjs` and `tested-sources/benchmarks/publication/capture.mjs`. The current repository harness was formatted after measurements. The validation-only correction is explained in `scroll-validation-correction.md`; the runner continuation change is explained in `runner-control-changes.md`.

The reusable analyzer now checks all quiet bins in the active video window, including unrecovered end silence and missing audio coverage over a 60 ms mux/frame-edge allowance. Eight regression tests passed, and all 29 completed streaming datasets reproduce the published active-window quiet totals and maximum intervals exactly. The original executed analyzer and trial validation results remain preserved; this forward correction is saved separately and explained in `audio-oracle-correction.md`. The validation-pass counts above retain the original interior-silence criterion; the active-window audio tables expose additional end silence.

## Failures and saved evidence

| Run | Process exit | Validation issue | Runtime/cleanup issue |
| --- | --- | --- | --- |
| stream-ts-cef-01 | 1 | Unexpected interior audio silence in continuous signal | Error: Native browser: Native audio capture failed |
| stream-ts-external-01 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-obs-cef-01 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-obs-external-01 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-cpp-cef-01 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-ts-cef-02 | 1 | Unexpected interior audio silence in continuous signal | Error: Native browser: Native audio capture failed |
| stream-cpp-cef-02 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-obs-external-02 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-obs-cef-02 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-ts-external-02 | 0 | Unexpected interior audio silence in continuous signal | — |
| record-ts-cef-02 | 0 | Recorded input-state sequence missing/out of order: 3 | — |
| stream-obs-cef-03 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-obs-external-03 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-cpp-cef-03 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-ts-cef-03 | 1 | AssertionError('Error: RTMP receiver exited before the workload completed\n    at file:///workspace/suitecut/benchmarks/publication/capture.mjs:203:80') | Error: RTMP receiver exited before the workload completed
    at file:///workspace/suitecut/benchmarks/publication/capture.mjs:203:80 |
| stream-ts-external-03 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-obs-cef-04 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-ts-external-04 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-ts-cef-04 | 1 | Recorded input-state sequence missing/out of order: 3; Unexpected interior audio silence in continuous signal | Error: Native browser: Native audio capture failed |
| stream-cpp-cef-04 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-obs-external-04 | 0 | Unexpected interior audio silence in continuous signal | — |
| record-ts-cef-04 | 0 | Recorded input-state sequence missing/out of order: 3 | — |
| record-ts-cef-05 | 0 | Recorded input-state sequence missing/out of order: 1 | — |
| stream-ts-cef-05 | 1 | Unexpected interior audio silence in continuous signal | Error: Native browser: Native audio capture failed |
| stream-ts-external-05 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-obs-cef-05 | 0 | Unexpected interior audio silence in continuous signal | — |
| stream-obs-external-05 | 0 | Unexpected interior audio silence in continuous signal | — |
| memory-obs-external | 0 | Unexpected interior audio silence in continuous signal | — |
| memory-ts-cef | 1 | Recorded input-state sequence missing/out of order: 1; Unexpected interior audio silence in continuous signal | Error: Native browser: Native audio capture failed |
| memory-obs-cef | 0 | Unexpected interior audio silence in continuous signal | — |
| memory-ts-external | 0 | Unexpected interior audio silence in continuous signal | — |
| memory-cpp-cef | 0 | Unexpected interior audio silence in continuous signal | — |

Calibration attempts are retained under `calibration-*`, `smoke*`, `preflight`, `failed-runtime-lock-attempt`, and `failed-resource-sampler`. They include rejected scene/crop setup, missing pressure files, incomplete process inventory, and synthetic-audio/encoder pacing experiments. They are excluded from the tables above; their measurements must not be quoted as final product performance.

Each accepted trial contains output media, actual fixture HTML, input/output snapshots, encoder arguments where applicable, browser/OBS/runtime logs, original process samples, decoded markers, frame timestamps, audio RMS/anchors, FFprobe metadata and analysis JSON. `tested-sources`, `tested-sdk`, historical snapshots and native binaries pin the exact tested implementations. `environment.json` records CEF/OBS/library hashes and host limits. The final checksum manifest and archive inventory make the saved evidence reviewable.

## Reproduction

Use the pinned source/SDK snapshots and matching native/CEF/OBS dependencies from `environment.json`. This workspace already contains private OBS/Pulse/Xvfb runtime trees and the codec-enabled CEF SDK; these binaries are not installed by the benchmark script. The historical build compatibility patch and source snapshots are included. Validate all executable versions/hashes and the 87-pixel external-browser UI crop on the target host before reusing this runtime.

```sh
python benchmarks/publication/test_analyze.py
python benchmarks/publication/run.py
python benchmarks/publication/publish.py
```

`PUBLICATION_DIRECTORY` selects a fresh results directory. `PUBLICATION_TRIALS`, `PUBLICATION_TRIAL_MS`, `PUBLICATION_MEMORY_MS`, and `PUBLICATION_NATIVE_MEMORY_MS` override the saved defaults. `run.py` resumes completed IDs from `runs.json`; it rejects unfinished existing run directories rather than overwriting evidence. A single-workload lock prevents accidental overlapping captures. Ensure adequate free disk for media and the final archive. Move rejected attempts into a clearly labeled calibration folder before resuming.

The report is prepared for publication; no public upload or deployment is performed.
