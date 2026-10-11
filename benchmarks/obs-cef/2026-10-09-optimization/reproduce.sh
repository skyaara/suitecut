#!/usr/bin/env bash
set -euo pipefail
# Run from the repository root after setup/build and starting the runtime.
# Install Python dependencies used by the analyzers: psutil, numpy, matplotlib.
# Saved runs remain immutable; each reproduction gets a new root/label.
export DISPLAY=:99
export PULSE_SERVER="unix:$PWD/.suitecut/runtime/pulse.sock"
export SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser"
export PLAYWRIGHT_BROWSERS_PATH="$PWD/.suitecut/playwright-browsers"
export OBS_PID="$(pgrep -x obs)"
export XVFB_PID="$(pgrep -x Xvfb)"
run_label="reproduced-$(date -u +%Y%m%dT%H%M%SZ)"
output_root="$PWD/benchmarks/obs-cef/$run_label"
media_root="$PWD/.suitecut/obs-cef-optimization/$run_label"
export BENCHMARK_OUTPUT="$output_root/comparison"
export BENCHMARK_MEDIA="$media_root/comparison"
export BENCHMARK_CASE=720p60-canvas,1080p60-canvas,4k30-canvas,4k60-scroll
export BENCHMARK_TRIALS=3
node benchmarks/obs-cef/run-optimized.mjs
for trial in 1 2 3; do
  FEATURE_MODE=stream FEATURE_BACKENDS=obs,native \
    FEATURE_OUTPUT="$output_root/features-stream-$trial" \
    FEATURE_MEDIA="$media_root/features-stream-$trial" \
    node benchmarks/obs-cef/features.mjs
done
FEATURE_MODE=record FEATURE_BACKENDS=obs,native \
  FEATURE_OUTPUT="$output_root/features-record" \
  FEATURE_MEDIA="$media_root/features-record" \
  node benchmarks/obs-cef/features.mjs
# Long verification can return nonzero for media failures; preserve its result.
soak_exit=0
SOAK_LABEL="$run_label" SOAK_FPS=30 SOAK_DURATION_MS=1800000 \
  PROFILE_PRIVATE_EVERY=0 node --expose-gc benchmarks/obs-cef/soak.mjs || soak_exit=$?
REUSE_LABEL="$run_label" REUSE_CYCLES=200 REUSE_HEAP_SNAPSHOTS=1 \
  node --expose-gc benchmarks/obs-cef/reuse-source.mjs
# Four-thread OBS control: stop OBS, back up recordEncoder.json and
# streamEncoder.json under .suitecut/obs-profile/obs-studio/basic/profiles/Benchmark,
# set only x264opts to "threads=4", restart OBS, refresh OBS_PID/XVFB_PID,
# then repeat comparison and stream commands with separate output/media roots.
# Restore the captured profile after stopping the control runtime.
# The saved thread-limited-checks.py is an audit coordinator with historical
# runtime PIDs; do not launch it unchanged against another runtime.
# Report/validation/archive helpers intentionally target the saved run directory;
# adapt their OUT paths before generating a reproduction report.

exit "$soak_exit"
