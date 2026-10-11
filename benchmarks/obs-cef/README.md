# SuiteCut / OBS evidence

The saved run is [9 October 2026](2026-10-09/report.html), using the client timezone Asia/Calcutta. Raw ISO timestamps use UTC.

This measures functionality that can be observed through OBS capture: real browser scrolling, moving content, input, navigation, source changes, recording, audio on/off, and local RTMP streaming. OBS input is driven by an external browser controller. SuiteCut-specific model synthesis, test-runner semantics, remote services, and hardware paths are outside this comparison.

## What is saved

- `2026-10-09/report.html` and `report.md`: detailed findings, charts, scope, and limitations.
- `trials.json`, `trials.csv`, `aggregates.json`: every trial, failures, missing observations, descriptive medians and p05/p95 ranges.
- `trials/<id>/`: exact settings, result, media probe, decoded binary marker IDs, process resource samples, representative decoded frame, and SuiteCut manifest.
- `features/`: operation results, screenshots, decoded source identities, PCM tone analysis, receiver files/probes/status, sync evidence, and sustained-run samples.
- `logs/`: setup/build/runtime logs, successful verification, failed smoke/pilot diagnostics, and startup trace.
- Environment, browser/SDK versions, Linux CEF distribution metadata/checksums, package checksums, encoder/profile settings, and the exact native setup patch.
- Recordings and received streams under `.suitecut/obs-cef-evidence/2026-10-09`; paths are recorded in JSON. The full evidence archive includes these media files.

Failed smoke attempts and the interrupted pilot are retained separately from repeated-trial aggregates. Media in the evidence archive uses its original workspace-relative path. JSON preserves the original absolute path for audit; after extracting elsewhere, substitute the extraction root.

## Reproduction

Use a Linux x86-64 source checkout, Node 22+, pnpm, Python, GCC, FFmpeg/FFprobe, and Debian-compatible runtime libraries. The run used four CPU equivalents and a 16 GiB cgroup limit. Keep the Chromium sandbox enabled. `native/browser/browser.cc` includes the saved callback qualification and first-run fix.

```sh
bash benchmarks/obs-cef/setup.sh
bash benchmarks/obs-cef/start-runtime.sh
```

Keep the runtime running in one terminal. In another:

```sh
export DISPLAY=:99
export PLAYWRIGHT_BROWSERS_PATH="$PWD/.suitecut/playwright-browsers"
export SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser"
export LD_LIBRARY_PATH="$PWD/.suitecut/obs-deps/root/usr/lib/x86_64-linux-gnu"
export PULSE_SERVER="unix:$PWD/.suitecut/runtime/pulse.sock"
export OBS_PID="$(pgrep -x obs)"
export XVFB_PID="$(pgrep -x Xvfb)"
node benchmarks/obs-cef/run.mjs
node benchmarks/obs-cef/features.mjs
python3 benchmarks/obs-cef/inspect-failures.py
python3 benchmarks/obs-cef/report.py
python3 benchmarks/obs-cef/validate-evidence.py
python3 benchmarks/obs-cef/save-evidence.py
```

`run.mjs` defaults to five trials, ten-second workloads, nine cases, and both backends. Override `BENCHMARK_TRIALS`, `BENCHMARK_DURATION_MS`, `BENCHMARK_CASE`, `BENCHMARK_BACKENDS`, `BENCHMARK_OUTPUT`, or `BENCHMARK_MEDIA` for a diagnostic run. Use a separate output directory to preserve original measurements. Each trial starts a fresh browser profile, warms the page for one second and the encoder for half a second, and alternates pipeline order. OBS capture is disabled during native trials.

`BENCHMARK_RESUME=1` preserves saved completed trial IDs and runs the remaining IDs. Archive an interrupted attempt separately before resuming; do not count that incomplete attempt as one of the five completed trials. This run resumed its driver after the first 4K30 failures to add recursive exception capture, preserving completed measurements and the interrupted attempt. The capture pipeline and trial settings stayed the same. The earlier errors retain their top-level exception; later failures also retain nested causes.

`features.mjs` runs recorded browser operations with audio disabled/enabled, a 20-second 1080p60 RTMP test, and a five-minute 1080p60 RTMP soak for each backend. Use the actual saved status to distinguish passed, failed, and interrupted runs. Never infer success from requested duration.

For an isolated diagnostic, set `FEATURE_MODE` to `record`, `stream`, or `soak`, `FEATURE_BACKENDS` to `obs` or `native`, and `FEATURE_OUTPUT` / `FEATURE_MEDIA` to separate directories. The `finish-after-recordings.py` utility waits for an existing recording-driver PID, reloads OBS to load the 4500 kbps streaming configuration, then runs the feature checks. It records the reloaded OBS PID for sampling and closes that OBS instance afterward.

The installation script fetches Debian packages from the current trixie repository. Compare them to the saved package checksums before claiming an exact software reproduction; mirror updates can change packages. The CEF Linux SDK SHA-256 and libyuv commit are fixed. The saved OBS version is 30.2.3.1-3; saved Chromium is 151.0.7922.34; CEF Chromium is 152.0.7977.134.

The user-owned OBS package requires relocation of its absolute muxer path. `setup-obs.py` modifies one known path in the extracted plugin and creates a wrapper supplying its runtime libraries. It checks the path count and length. SDKs and dependencies remain in ignored `.suitecut/`; benchmark scripts and reports are saved in the repository.

## Reading the results

A valid recording means dimensions, readable media, decoded animation, and scrolling correctness where applicable. It does not establish the configured frame rate. Distinct FPS comes from transitions in a fixed 20-bit animation marker, decoded from the output. Repeated frames and longest held markers are retained. Static scenes have no motion target.

SuiteCut's production recorder uses VP9 CRF 18/realtime/cpu-used 5/four threads. OBS uses x264 veryfast/CRF 18/H.264 MKV with a silent AAC track during performance tests. This is an end-to-end available-pipeline comparison; it is not a codec-normalized encoder comparison. CPU covers capture and finalization, and summed RSS can count shared pages more than once. Half-second sampling can miss short-lived children.

`startupMs` includes launching the browser, navigating to the fixture, and the one-second page warmup; it is not isolated process launch latency. OBS finalization waits for inactive output and a readable muxed file, polling at 250 ms. Native finalization includes encoder draining and manifest/media probing. Operation-check duration includes assertions, settle delays, and evidence capture; it is not an input-to-photon latency measurement. Derived `attemptWallMs` includes the complete trial lifecycle, including cleanup and decoding, and is not isolated encoder time.

Streaming uses the production SuiteCut H.264 publisher or OBS H.264, and a local FFmpeg receiver. Decode actual packet timestamps and marker motion; container FPS alone is insufficient. Decode timestamps must increase and presentation timestamps must be unique; normal H.264 B-frame presentation reordering is permitted. Audio and video are padded to the same media start before sync matching. A/V evidence compares periodic flash and tone onsets, excludes the first two seconds, and can resolve offsets only within half a one-second period. It includes browser scheduling error and does not measure network-to-display latency.

The installed OBS package has no browser-source plugin, hardware encoder, or GPU capture path. It captures external Chromium via XSHM on Xvfb. Its 87-pixel crop removes browser UI and is verified from saved source/decoded images. Different desktop themes, DPI, browser UI, or window managers require rechecking that crop. No browser-plugin comparison is implied.

The streaming playback metrics in the report use received media duration, including publisher drain. This run initially calculated them using active wall duration; `features/results.json` retains the original evidence, and `features/results-enriched.json` retains both calculations. `features-executed.mjs` is an audit snapshot of the loaded harness; the reproduction harness now uses media duration directly. This correction changes reporting, not captured media or pass/fail assertions.

The final evidence validation also checks the encoded input-state sequence 0 → 1 → 3 → 7 → 15 in primary-source frames only, excluding blank/other-source frames. Each functional recording has a `recorded-states-validation.json` audit. The reproduction harness includes this stronger assertion directly.

## Optimization follow-up

[Optimization report](2026-10-09-optimization/report.md) preserves the later
profiles and fixes separately from the original measurements. `run-optimized.mjs`
uses decoded presentation timestamps for VFR-safe motion and hold calculations;
it accepts comma-separated `BENCHMARK_CASE` names. `soak.mjs` runs simultaneous
1080p60 recording and RTMP streaming for 30 minutes with natural GC, periodic
wheel scrolling, and beginning/middle/end A/V checks. `reuse-source.mjs` isolates
session retention across 25 recordings with post-recording forced GC.

Raw samples, CPU profiles, rejected diagnostic settings, exact source snapshots,
clips and receiver files are preserved. Read the completed result and validation
before inferring that a requested soak finished. The follow-up archive is saved
separately as `/workspace/shared/suitecut-optimization-evidence-2026-10-09.tar.gz`.
The earlier OBS/CEF archive is retained unchanged.

## Matched OBS control and dropout investigation

The [30-minute OBS control](2026-10-09-obs-long-control/report.md) checks the same
1080p30 scrolling and flash/tone workload with simultaneous recording and
streaming. OBS avoided SuiteCut's long interruptions, while its ordinary skipped
frames remain reported. Browser, capture architecture and codecs differ.

The [dropout investigation](2026-10-09-dropout-investigation/report.md) adds native
callback/queue/write counters, a labeled four-second Node pause, and a separate
clean 30-minute SuiteCut control. Source-counter recovery distinguishes a
reader-only pause from the original browser/page stall. The new evidence is saved
separately; read the actual decoded-output verdict and limitations in the report.
