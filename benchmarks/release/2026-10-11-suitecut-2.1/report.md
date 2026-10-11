# SuiteCut 2.1.0 final release verification

Prepared locally; **not published**. Final saved checks: **passed**.

## Fresh sanitizer and fuzz results

Eight CTest groups passed in the release build and in the Clang 19
ASan/UBSan/LSan build, including 160 concurrent output-lifecycle cycles,
codec cleanup, scaler-allocation failure, and injected 250 ms encoder delays
at 30/60 FPS with decoded PCM retention assertions. ThreadSanitizer checks use
matching instrumented FFmpeg 7.1.5; see the saved configure/build/test logs.

| Fuzz target | Executed inputs | Seconds | Peak RSS MiB | Failure artifacts | Exit |
| --- | ---: | ---: | ---: | ---: | ---: |
| bounds | 64,096,607 | 601.6 | 492 | 0 | 0 |
| media | 18,568 | 601.7 | 233 | 0 | 0 |

Each campaign ran separately for a requested 600 seconds, with leak detection
and fatal sanitizer errors enabled, no suppressions, 512-byte maximum input,
5-second per-input timeout, 1024 MiB RSS limit and 128 MiB allocation limit.
Affinity is four CPU cores; host CPU quota is four cores. These are serial safety
checks, not performance measurements. Fuzzer RSS includes sanitizer overhead.

The bounds target mutates audio overlaps, popup composition, timestamps and command
IDs. The media target executes bounded FFV1/PCM output/queue/pause/failure/stop paths.
Executed-input counts include short rejected inputs and do not count complete
browser recordings or establish all-codec coverage. Corpus files, random seeds,
logs, binary hashes and source identity are retained. This is a fresh campaign;
the older sealed campaign remains separate.

## Release checks

| Check | Exit | Seconds |
| --- | ---: | ---: |
| build-asan | 0 | 0.7 |
| ctest-asan | 0 | 12.4 |
| build-release | 0 | 0.1 |
| ctest-release | 0 | 9.0 |
| fuzz | 0 | 1203.3 |
| configure-tsan | 0 | 3.1 |
| build-tsan | 0 | 28.7 |
| ctest-tsan | 0 | 15.8 |
| frozen-lock | 0 | 8.2 |
| prepublish | 0 | 155.3 |
| pack-release | 0 | 37.2 |
| installed-native | 0 | 11.5 |
| engine-e2e | 0 | 26.6 |
| external-browser | 0 | 9.6 |

The full unit suite passed **328 tests in 38 files**; site checks reported zero
errors and zero warnings. Package checks inspect all nine tarballs and public imports, install SDK/VITS/core
in an isolated consumer, and exercise installed CLI help. Native installed-package
verification records through real CEF with an audio track and checks owned-browser
cleanup. The CEF Playwright integration suite additionally exercises capture,
readiness, input/scroll, storage/network, pause, popup and shutdown paths: **16 passed**.
External browser automation passed **27 checks**, including 25 attach/disconnect cycles,
on sandboxed Chromium 151.0.7922.34. No RTC claim is made by that suite.

One initial prepublish attempt stopped because pnpm tried a noninteractive reinstall
after the workspace version bump. The frozen lockfile passed; final commands kept
the existing workspace symlinks with `pnpm_config_verify_deps_before_run=warn`.
The isolated npm tarball installs passed. Initial attempts are preserved in
`release-attempts.json` and the original logs; this was not a sanitizer failure.

## Artifacts

| Package | Compressed bytes | SHA-256 |
| --- | ---: | --- |
| suitecut | 62,368,603 | `97ae12e28ba2c153eb20077b0def2c2645e534a4b9773ab37dafacd2f8579b9d` |
| @suitecut/audio-sherpa-core | 3,563 | `5aef0d9cdd78611e3c8692a82f212fadba363d447fd52f72b49d35f26c0c6279` |
| @suitecut/audio-vits | 1,764 | `6227b0bb90ec874eca11a7e4bc00b5bd80adf63352282d66da7311678ce2d451` |
| @suitecut/audio-matcha | 1,754 | `5137713354215419d6cdce1f2cf2b18a8d82048021bdf1cf1b8dd925bb748580` |
| @suitecut/audio-kokoro-sherpa | 1,857 | `5595bff68df93f5d9b80dc32310bcdd9f79792f2cc36a6155fcfb6a067298dee` |
| @suitecut/audio-kitten | 1,684 | `a07129eb612a554a776ad153acc0f1c190f7464520a19f87f6092f0a962421a3` |
| @suitecut/audio-zipvoice | 1,813 | `b54d6cebcc4efb5b7c71735027de50c7af75c8d6a78d682e3f1375bcf2fe822c` |
| @suitecut/audio-pocket | 1,811 | `78112a2647d786a3e920aa0d932be26cc6ecaec3c048046a25d2a0576b9afac8` |
| @suitecut/audio-supertonic | 1,698 | `f61a6f8d8f525d09ba5bc9eb7dac7e4d891a5c9250a3d16aa326b967c823d1d5` |

The npm artifact contains native source and pins, not a compiled browser or CEF
SDK. All packages are version 2.1.0. Publication, tagging and pushing were not performed.

## Earlier long-run evidence and remaining issues

The unchanged native implementation and executable match the earlier sealed
resolution matrix and 30-minute memory profile; `sealed-baseline-reuse.json`
records the comparison. Those captures were not re-run as part of this final fuzz pass.

- SuiteCut 30-minute 1080p30 stream: peak summed RSS 1408.0 MiB, PSS 722.54 MiB,
  USS 497.98 MiB. Post-120-second slopes: RSS +1.04, PSS +0.94, USS +0.92 MiB/min.
  Queue retention peaked at 32.63 MiB video and 0.06 MiB audio. The memory trend's
  cause is unresolved; this profile does not establish indefinite leak freedom.
  Per-PID analysis attributes about +0.61 MiB/min to the CEF/native host and
  +0.22 MiB/min to the benchmark controller. See `earlier-memory-attribution.json`;
  process location does not distinguish caching/buffering from leaked allocations.
- Quiet audio: SuiteCut 15.03 seconds total / 275 ms longest interval; matched OBS
  embedded CEF 15.02 seconds / 280 ms. Both AAC packet timelines were continuous.
  These are quiet output-content measurements, not proof that packets were dropped.
  Their source stage is unresolved and the release does not claim this issue fixed.
- CPU/resource comparisons are fixture/host-specific. The earlier runs used four
  cores and a sampled 8 GiB summed-RSS guard; the host kernel memory cap was 16 GiB.
- Full Chromium/prebuilt library sanitizer coverage, GPU encoding, remote RTC
  transport and the new engine's macOS packaging remain unverified. Windows native
  packaging is not implemented. Stock CEF source-audio startup can still lose sound.

![Earlier SuiteCut memory profile](suitecut-memory-profile.png)

![Earlier OBS memory profile](obs-soak-resources.png)

Detailed sealed data: `benchmarks/native-sync/2026-10-11-shared-clock/` and
`benchmarks/obs-cef/2026-10-11-quietness-control/`. Copies of their memory charts
above are provided for convenience. Their raw media and checksummed archives
remain unchanged. A bounded clean sanitizer/fuzz run cannot prove universal memory safety.
