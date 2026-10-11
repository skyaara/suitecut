# OBS embedded CEF: unexpected quiet-audio control

Fresh OBS Browser Source captures are compared with the sealed SuiteCut shared-clock captures. Recording and streaming are separate. This report measures decoded quiet audio during a source that intentionally plays continuously; “quiet” does not by itself identify where samples were lost.

The 30-minute 1080p30 stream produced 15.020 s quiet audio in OBS CEF (longest 280 ms), versus 15.030 s in SuiteCut (longest 275 ms). These individual sustained runs have nearly identical quiet totals, despite quieter short OBS streaming controls. Both outputs contain quiet audio inside continuous AAC packet timelines; this alone does not identify the responsible stage or prove source-sample loss.


| Case | Product | Quiet total ms | Longest ms | Quiet ms/min | Quiet % | CPU cores | Peak RSS MiB | Distinct FPS |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 720p60-record | SuiteCut | 505.00 | 145.00 | 252.44 | 0.42 | 0.66 | 1,304.25 | 59.44 |
| 720p60-record | OBS CEF | 460.00 | 55.00 | 230.01 | 0.38 | 2.16 | 1,073.11 | 59.13 |
| 720p60-stream | SuiteCut | 745.00 | 90.00 | 372.42 | 0.62 | 0.63 | 1,340.33 | 59.48 |
| 720p60-stream | OBS CEF | 115.00 | 50.00 | 57.50 | 0.10 | 2.13 | 1,060.64 | 56.52 |
| 1080p30-record | SuiteCut | 1,255.00 | 220.00 | 627.40 | 1.05 | 0.60 | 1,408.06 | 29.44 |
| 1080p30-record | OBS CEF | 600.00 | 150.00 | 299.95 | 0.50 | 2.36 | 1,177.25 | 29.15 |
| 1080p30-stream | SuiteCut | 1,720.00 | 200.00 | 860.07 | 1.43 | 0.72 | 1,411.08 | 29.52 |
| 1080p30-stream | OBS CEF | 25.00 | 5.00 | 12.49 | 0.02 | 2.35 | 1,143.72 | 29.84 |
| 4k30-record | SuiteCut | 50.00 | 25.00 | 25.02 | 0.04 | 1.55 | 1,723.40 | 28.26 |
| 4k30-record | OBS CEF | 685.00 | 125.00 | 343.19 | 0.57 | 3.50 | 1,547.55 | 8.44 |
| 4k30-stream | SuiteCut | 130.00 | 100.00 | 65.01 | 0.11 | 1.51 | 1,677.16 | 28.60 |
| 4k30-stream | OBS CEF | 665.00 | 80.00 | 332.72 | 0.55 | 3.49 | 1,516.42 | 8.12 |
| soak-1080p30-stream | SuiteCut | 15,030.00 | 275.00 | 501.00 | 0.84 | 0.60 | 1,408.01 | 29.70 |
| soak-1080p30-stream | OBS CEF | 15,020.00 | 280.00 | 500.66 | 0.83 | 2.33 | 1,151.43 | 28.39 |

![Quiet comparison](quiet-comparison.png)

![30-minute quiet-audio timeline](soak-quiet-timeline.png)

![30-minute resource profiles](soak-resources.png)

## Method and limits

- Actual OBS embedded CEF Browser Source, 1 source, reroute_audio=true, volume 1, unmuted, no microphone or desktop capture. All inputs from the copied profile are removed before creating the browser source.
- The fixture HTML hashes match SuiteCut for every paired case. Deterministic AudioWorklet continuous noise, CSS/canvas movement and browser scrolling; click, text-entry and selection are exercised. No intentional interior silence.
- Nominal settings are validated, but achieved distinct FPS and PTS/hold failures are reported separately. The OBS 4K software-rendered cases may fail to achieve the requested 30 FPS; treat their audio results in that overload context.
- Each short case requests 120 seconds; the 1080p30 streaming soak requests 1,800 seconds. OBS runs are fresh single trials and SuiteCut trials were captured earlier; this is not a randomized repeated-trial estimate.
- Same shared decoder: AAC to 48 kHz mono float PCM; 5 ms RMS bins below 0.001 count as quiet. Only the encoded workload marker interval is counted, excluding warmup and shutdown. Missing audio coverage is reported separately. Quietness is not a verified count of dropped source samples.
- Both use H.264 and AAC 48 kHz stereo 128 kbps. Recording CRF 18 / MKV. Streaming local RTMP / FLV copy receiver, bitrate 3500 / 4500 / 12000 kbps for 720p / 1080p / 4K. x264 veryfast, threads=1 below 4K; ultrafast, threads=3 at 4K; zerolatency, no B frames, GOP 2 seconds. OBS and SuiteCut use different encoder integrations; equal knobs do not guarantee identical bitstreams.
- OBS 32.2.0 embeds Chromium 127.0.6533.120; SuiteCut embeds Chromium 152.0.7977.83. Browser-version differences and mixer/renderer differences prevent attributing an observed difference to CEF or scheduling alone.
- CPU affinity 4 cores with 4-core host quota. Requested 8 GiB is a sampled sum-RSS termination guard, not an enforced 8 GiB kernel cap; host cap 16 GiB. No exposed hardware GPU: OBS OpenGL uses llvmpipe and CEF uses software rendering. These are software-only host results.
- During the long stream, three already-completed raw OBS streams were relocated to the separate /tmp filesystem to reserve overlay storage, with original paths preserved by symlinks. storage-relocation.json records exact host monotonic intervals and byte counts; the complete raw media is included in the archive. This brief host housekeeping remains inside the measured long-run interval and is not selectively excluded.
- Resource sampling every 500 ms; soak PSS/USS every 10 seconds. Product accounting includes OBS/CEF or SuiteCut/CEF plus the controller; local receiver, Xvfb and Pulse are separately sampled and excluded. Clean owned-file page cache is advised away between captures only. No audio taps or sanitizers during primary OBS captures.
- The long-run AAC packet timelines contain no gaps above the 2 ms rounding allowance in either product. Encoded audio continuity therefore does not imply continuous audible sample content.
- A post-capture H264 filler-NAL diagnostic explains the larger OBS stream: original average mux bitrate 4638 kbps, about 533 kbps after removing filler/remuxing, versus SuiteCut 458 kbps with no size change from that filter. The full original streams are retained. Matching nominal stream bitrate does not mean identical CBR padding policy.
- Encoded AAC packet timestamps are independently checked for both products in packet-controls, allowing 2 ms mux rounding. Packet holes and decoded quiet audio are distinct measurements.
- Generated audio-content correlation versus the encoded AudioContext marker is also saved. Its 10 ms marker plus nearest-frame quantization is an investigation aid, not a certification of lip sync.
- Setup attempts that failed effective-video/readiness assertions are retained under startup-attempts and excluded from benchmark rows. Any failed primary run appears in the CSV with harnessValid=false; no silent fallback to lower resolution.

## Evidence

- comparison.csv / comparison.json: all paired metrics and baseline analysis/fixture hashes.
- runs/<case>: MKV or FLV media, OBS log, copied runtime profile, result JSON, process samples, status history, decoded marker arrays, RMS arrays, ffprobe metadata, quiet-interval CSV and analysis JSON.
- source/: exact harness/fixture/analyzer sources used; provenance.json: binary hashes and platform.
- packet-controls/: gzip packet metadata and active-timeline-gap summaries for both products.
- The SuiteCut baseline is referenced without modification; baseline analysis/result/fixture copies are included. Full original media and traces remain in the separately sealed archive suitecut-shared-clock-2026-10-11.tar.gz (SHA256 c5602868e63866a6e2adb61c8c31a5cb98f5b2398604efc4aa0588d5e24d4871).
