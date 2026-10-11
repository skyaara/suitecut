# Reproduce the OBS control without overwriting this run

Use the existing repository checkout and the pinned dependencies described in `../README.md` / `../2026-10-09-optimization/environment.json`. Run from the repository root. The host needs the same four-CPU quota, 16 GiB memory limit, software rendering and sandboxed browser setup for a comparable reproduction. Choose a fresh output, media and isolated profile directory.

1. Copy `.suitecut/obs-profile/obs-studio` into a new `<profile>/obs-studio` directory. In its `basic/profiles/Benchmark/recordEncoder.json` and `streamEncoder.json`, set only `x264opts` to `threads=4`. Keep the saved codec/preset/rate-control settings in `settings.json`.
2. In one terminal, start the isolated runtime under an exclusive lock:

   ```sh
   OBS_CONFIG_ROOT="<absolute profile directory>" RUNTIME_LOG_DIR="<absolute new log directory>" \
     flock -n .suitecut/thread-control.lock bash benchmarks/obs-cef/start-runtime.sh
   ```

3. In another terminal, export `DISPLAY=:99`, `PULSE_SERVER="unix:$PWD/.suitecut/runtime/pulse.sock"`, `PLAYWRIGHT_BROWSERS_PATH="$PWD/.suitecut/playwright-browsers"`, and the actual printed `OBS_PID`, `XVFB_PID`, `PULSE_PID` values. Then run:

   ```sh
   OBS_LONG_OUTPUT="<absolute fresh output directory>" \
   OBS_LONG_MEDIA="<absolute fresh media directory>" \
   OBS_LONG_DURATION_MS=1800000 node --expose-gc benchmarks/obs-cef/obs-long-soak.mjs
   ```

4. Shut down OBS first, let the runtime wrapper reap its display/audio children, and confirm the output files are finalized before decoding.
5. The analyzer/report/archive scripts explicitly target the saved output/reference directories. Adapt their `OUT` and archive path to the new reproduction before running them. Run full-file audio/content checks as well as all three sync windows; do not infer a pass from a completed workload or OBS counters alone.

The saved `run-obs-long-control.py` is the actual exclusive coordinator for this run and records its runtime PIDs/source hashes. It uses the saved directory name; do not run it unchanged over existing evidence. This control changes the isolated profile only; the original benchmark profile and prior evidence archives remain preserved.
