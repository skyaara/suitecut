# SuiteCut native codec benchmark

Codec selection and discovery are implemented in the C++ media engine, with typed controls in the TypeScript SDK. Eight software pairings passed real CEF recording and presentation export. Independent FFmpeg decoding passed for 42 files: eight captures, eight presentation exports, two simultaneous-output files and 24 synthetic capture/render files.

**Continuity remains imperfect on this host.** Captures sustained 26.76–29.63 distinct fps at a 30 fps target, with maximum image holds of 67–433 ms. The first capture had 520 ms of leading audio silence; interior silent intervals ranged up to 70–210 ms across the software pairings. All native queue-drop counters were zero. Those counters do not establish gap-free audio. The codec checks passed their documented acceptance thresholds; this report preserves the remaining continuity findings.

The evidence directory is labelled `2026-10-10`. The final matrix ran from `2026-10-09T19:54:34.150Z` to `2026-10-09T19:57:58.497Z` in UTC. Host: Linux x86-64, four CPU equivalents (`cpu.max = 400000 100000`), 16 GiB memory limit, private Xvfb/PulseAudio and software browser rendering. No `/dev/dri` or NVIDIA device was exposed. CEF 152.0.8+g1ce985c+chromium-152.0.7977.134, FFmpeg 7.1.5, SVT-AV1 2.3.0, AOM 3.12.1 and Playwright 1.62.1.

Each pairing captured 1920×1080 at 30 fps for approximately 20 seconds from the same animated browser fixture, with a 440 Hz tone, typing, a click, selection and scrolling. A four-second busy loop deliberately blocked Node during each capture. C++ continued encoding independently. Each pairing then exported the same 5.899-second presentation at 640×360/30 fps, including narration, captions, zoom/highlight, two checkpoints and selection between two pages.

| Video / audio | File MiB | Distinct fps | Max hold ms | Leading / max interior silence ms | Mean native CPU cores | Stop ms | Export ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `libx264` / `aac` | 0.88 | 29.14 | 267 | 520 / 140 | 0.687 | 40.5 | 1116.6 |
| `libx265` / `aac` | 1.59 | 29.63 | 167 | 10 / 90 | 1.168 | 66.4 | 2024.1 |
| `libvpx` / `libopus` | 2.07 | 27.91 | 167 | 0 / 130 | 1.165 | 76.9 | 1199.0 |
| `libvpx-vp9` / `libopus` | 1.26 | 28.93 | 200 | 0 / 170 | 0.991 | 178.9 | 2417.4 |
| `libsvtav1` / `aac` | 0.86 | 28.65 | 133 | 20 / 70 | 1.120 | 284.6 | 1943.4 |
| `libaom-av1` / `aac` | 1.24 | 29.48 | 67 | 10 / 90 | 0.936 | 62.9 | 3927.7 |
| `ffv1` / `flac` | 18.59 | 26.76 | 200 | 0 / 150 | 0.885 | 98.0 | 1097.1 |
| `ffv1` / `pcm_s16le` | 22.40 | 27.34 | 433 | 0 / 210 | 1.092 | 44.9 | 1052.0 |

![Codec CPU, size and distinct-frame measurements](codec-performance.svg)

These settings are not equivalent visual quality. Recording defaults are x264 veryfast/CRF18, x265 ultrafast/CRF23, VP8 realtime/speed8/quality18 with a 4500 kbps target, VP9 realtime/speed5/CRF18, SVT preset12 (mapped to11)/normal prediction/CRF32, AOM realtime/speed8/CRF32 and FFV1 level3. Exports use the existing standard quality profiles, which differ from recording. The chart compares these tested configurations; it is not a quality-normalized encoder ranking.

Native CPU includes the C++ helper and CEF renderer/GPU processes, excluding Node, the receiver and sampler. Per-case peak summed RSS was 1166–1926 MiB; those sums double-count shared libraries, and later cases include allocator caches retained from earlier encoders in the same engine. They are not isolated steady-state codec memory measurements. Node CPU includes the deliberate four-second busy loop; `decoded-validation.json` also reports CPU with that loop subtracted.

Full decoding used separate FFmpeg/ffprobe processes with fatal decode errors enabled. Animation markers established distinct frames and image holds; every capture exceeded 80 distinct frames during the four-second Node stall. Every export contained 177 decoded video frames, audible narration mixed with page audio, and the expected visual and audio page selection. Frame timestamp gaps stayed within one 30 fps interval plus container rounding. Silent intervals were measured with mono 8 kHz RMS below 0.001 in 10 ms windows. Full interval lists are saved, including startup and trailing intervals. The coarse acceptance check permits at most five consecutive silent 100 ms windows; passing that check does not mean silence is absent.

The remaining audio gaps occurred across several encoders, including PCM. This matrix does not isolate whether the gaps come from CEF/audio delivery, scheduling or the native timeline. Some coincide with the forced controller stall. Their cause remains unresolved; there is no new matched OBS run for these codec settings. The previous 30-minute H.264/OBS comparison remains separate and cannot establish superiority for the new encoders.

An additional approximately 20-second test recorded VP9/Opus while simultaneously publishing H.264/AAC to a local RTMP receiver, including a four-second Node stall and scrolling. Both finalized with empty audio/video queues and zero dropped-audio counters.

| Concurrent output | Decoded distinct fps | Max hold ms | Leading / max interior silence ms |
| --- | ---: | ---: | ---: |
| VP9 / Opus recording | 29.56 | 167 | 0 / 90 |
| H.264 / AAC RTMP receiver | 29.85 | 67 | 20 / 20 |

FFV1 video and FLAC/PCM audio were independently byte-compared with deterministic native input: 30 frames of 320×180 I420 (2,592,000 bytes) and exactly 48,000 stereo S16 samples (192,000 bytes). Both lossless pairings matched exactly, with no silent padding appended to the synthetic input. This is lossless relative to 8-bit limited-range I420 and 48 kHz stereo S16; browser RGB has already been converted and subsampled.

Twelve synthetic pairings additionally cover HEVC/MOV, AV1/Opus/WebM, VP9/FLAC/MKV and H.264/PCM/MOV. Invalid codec/container combinations, out-of-range and null CRF, lossless CRF, unknown containers and HEVC RTMP were rejected. All three linked NVIDIA encoders rejected startup on this GPU-less host, and the same engine remained usable afterward. NVIDIA throughput, quality and successful-device cleanup are unverified. RTMP remains conventional H.264/AAC/FLV.

Two media issues were fixed during this work. The native renderer now supplies decoder packet time bases and accounts for Opus priming without shifting decoded samples twice; its WebM test allows one millisecond of timestamp rounding. Lossless recording now writes a partial final audio block rather than padding to a complete FLAC block. FFV1 export also uses level3/slice checksums, and HEVC MP4/MOV uses the hvc1 sample entry.

SVT-AV1 low-delay prediction crashed inside `libSvtAv1Enc.so.2` under ASAN at 320×180. The final recording configuration uses normal prediction with encoder lookahead. The complete codec matrix and sanitizer stress were rerun after this change; the failing pre-fix log is retained as `logs/asan-first.log`. AOM is the available AV1 path with realtime/zero-lookahead configuration.

ASAN/UBSAN with leak detection passed 20 cycles ×12 pairings, totaling **480 capture/render jobs**, without reported sanitizer errors or leaks and with no suppressions. Threads returned to one after every cycle. End-of-cycle RSS ranged from 213.5 to 256.9 MiB (first 239.0, last 253.7); ASAN quarantine was limited to64 MiB. SuiteCut's media core/test was instrumented; CEF and third-party codec libraries were not rebuilt with sanitizers. This is repeated cleanup evidence, not an indefinite leak-free guarantee or a 30-minute soak of every new codec.

Other checks passed: four native CTest targets, 306 unit tests, TypeScript build/typecheck, ESLint, changed-file formatting, six real CEF Playwright tests and the detailed native presentation regression. The latter covers narration mixing, page ownership, callback failure, cancellation, default audio-disabled behavior, captions, checkpoints, visual/audio selection, typing, click, zoom/highlight and scrolling. Playwright snapshots are excluded from test discovery and new output directories prevent older evidence from being replaced.

`matrix.json` contains commands/options, output status samples, Node memory/CPU, startup/stop/export timings, ffprobe metadata, hardware errors and negative checks. `processes.jsonl` contains independent raw process/pressure samples. `decoded-validation.json` contains complete file probes, hashes, continuity intervals, source selection/mixing checks and lossless comparisons. `sanitizer-summary.json`, `logs/`, `tested-binaries/`, `tested-sources-sdk/`, `tested-sources/`, `source-sha256.json`, `build-metadata.json` and `linkage.txt` preserve the tested implementation. `render-fixture/` includes the exact source media plus a manifest using relative artifact paths for portable presentation replay. That manifest was also replayed through the native engine and its H.264/AAC output decoded successfully.

All earlier attempts are retained under `setup-attempts/` and `logs/`, including the NVENC harness correction and the pre-fix low-delay/FLAC captures. Their paths inside their saved result JSON describe the original locations; their media was moved into the corresponding attempt folder. The previous native-engine evidence checksums passed in full after restoring two outputs touched by an accidental archived-test discovery. Its archive remains `/workspace/shared/suitecut-native-engine-evidence-2026-10-09.tar.gz` with SHA256 `695e8d5a952fdc1ff8b9196ae9987aed661996e2333865fcc41856c14de7cf84`.

Build and replay commands are in [the collector guide](../README.md); API examples and restrictions are in [the engine guide](../../../native/browser/ENGINE.md#codec-selection). All changes remain in the working tree; no commit, push or deployment was performed.

`codec-changes.diff` compares the native core and thin SDK against the saved state before this codec work. The complete evidence archive is `/workspace/shared/suitecut-native-codec-evidence-2026-10-10.tar.gz`; its checksum is saved beside it.
