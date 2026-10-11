# Publication benchmark harness

Serial 1920×1080 / 30 FPS browser capture comparison. See the dated `environment.json` and `report.md` for measured versions, limits, capabilities, and results. The native external-browser capture row is unavailable; `connectBrowser` is an automation adapter.

The default series contains five trials per mode for five implementations, followed by four 10-minute streaming memory profiles and one 30-minute native profile. Separate recording and streaming tests are intentional. Recording uses a silent fixture; SuiteCut outputs video only while this OBS profile adds silent AAC. Streaming captures a continuous deterministic page audio signal.

`runtime.py` owns a fresh display, Pulse server, browser and optional OBS profile. It applies four-core affinity and a sampled 8 GiB summed-RSS guard. The actual kernel quota/limit are saved for every case. It acquires `.suitecut/thread-control.lock` to prevent concurrent captures. The guard is not a kernel memory cap.

`capture.mjs` uses the actual selected implementation for capture and browser input. `ffmpeg-normalized.py` adapts historical output codec settings to H.264 and saves original/normalized arguments, without changing historical frame queues. It does not synthesize another audio input. The deterministic video marker and action bits must be visible in the encoded media, not merely in DOM snapshots.

`sample.py` records process CPU/RSS/thread/fd readings twice per second. It discovers descendants from one parent map per second and caches permanent zombie PIDs while they remain present. This kernel lacks `/proc/PID/task/TID/children`; the implementation does not depend on those files. Observer CPU and collection duration are saved. Complete PSS/USS require readable `smaps_rollup` for every included process; inaccessible readings are flagged. The local RTMP receiver and owned Xvfb/Pulse infrastructure are separately identified.

`analyze.py` runs only after capture stops. It checks dimensions and codec, decodes every marker frame, verifies captured input-state order, measures held images and real video timestamps, and analyzes decoded streaming audio in 5 ms RMS bins. Waveform correlation anchors compare audio sample content to the encoded AudioContext clock marker, with explicitly limited timing precision. Resource summaries require at least 75% of expected active samples and the browser/capture/encoder roles.

```sh
python benchmarks/publication/test_analyze.py
python benchmarks/publication/run.py
python benchmarks/publication/publish.py
```

Python dependencies: psutil, NumPy, SciPy, Matplotlib; optional `markdown` for rendered HTML. Node dependencies are the pinned workspace package dependencies. System dependencies and private runtime paths: matching FFmpeg, `.suitecut/obs-deps/root` (Xvfb/Pulse/runtime libraries), `.suitecut/obs-cef-profile/root` (OBS 32.2 and browser plugin), `.suitecut/obs-cef-profile/config` (template), `.suitecut/native-build` (current engine/CEF), `.suitecut/publication-old/native-build` and `.suitecut/publication-old/dist` (historical capture/shim). The script does not install these dependencies.

To reconstruct the historical code, extract `git archive 2d4158c95217b2b4099898a2508a40f3fa4f121a src native/browser package.json tsconfig.build.json tsconfig.json` into `.suitecut/publication-old`, apply the saved historical build compatibility patch, and build with the common codec-enabled CEF SDK. See the saved build logs and source snapshot for the exact build. Preserve the old capture implementation and isolate the rebuild from the current source.

`PUBLICATION_DIRECTORY` chooses a fresh destination; `PUBLICATION_TRIALS`, `PUBLICATION_TRIAL_MS`, `PUBLICATION_MEMORY_MS`, and `PUBLICATION_NATIVE_MEMORY_MS` override durations/counts. `BENCH_DIRECTORY`, `BENCH_BACKEND`, `BENCH_MODE`, `BENCH_PROFILE`, `BENCH_DURATION_MS` select an individual `runtime.py` case. Five backend names: `ts-cef`, `ts-external`, `obs-cef`, `obs-external`, `cpp-cef`.

The runner resumes completed IDs from `runs.json`, rejects unfinished existing case directories and stops on structural media/resource-validation failures. Quality failures and process-cleanup failures remain in the comparison rather than being silently discarded. Retain and label incomplete/calibration cases before resuming. No other capture, build, sanitizer, plot generation or offline video analysis should run during active measurements. Version/library changes, external browser UI crop, software graphics and encoder configuration must be revalidated on a new host.

Final artifacts include per-trial media and raw samples, decoded markers and audio RMS arrays, JSON/CSV summaries, source/SDK/binary snapshots, Markdown/HTML reports and PNG/SVG figures. Seal the completed directory with a relative-path SHA-256 manifest; verify the archive inventory and content against that manifest. No publishing or upload is performed by this harness.
