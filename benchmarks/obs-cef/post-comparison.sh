#!/usr/bin/env bash
set -euo pipefail
export DISPLAY=:99
export PULSE_SERVER="unix:$PWD/.suitecut/runtime/pulse.sock"
export SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser"
export PLAYWRIGHT_BROWSERS_PATH="$PWD/.suitecut/playwright-browsers"
export OBS_PID=28846 XVFB_PID=28837
optimization_logs=benchmarks/obs-cef/2026-10-09-optimization/logs
node benchmarks/obs-cef/verify-container.mjs > "$optimization_logs/container-check.log" 2>&1
FEATURE_MODE=record FEATURE_OUTPUT=benchmarks/obs-cef/2026-10-09-optimization/features-record FEATURE_MEDIA=.suitecut/obs-cef-optimization/features-record node benchmarks/obs-cef/features.mjs > "$optimization_logs/features-record.log" 2>&1
FEATURE_MODE=stream FEATURE_OUTPUT=benchmarks/obs-cef/2026-10-09-optimization/features-stream FEATURE_MEDIA=.suitecut/obs-cef-optimization/features-stream node benchmarks/obs-cef/features.mjs > "$optimization_logs/features-stream.log" 2>&1
PROFILE_MODE=record PROFILE_LABEL=latest-4k-record PROFILE_WIDTH=3840 PROFILE_HEIGHT=2160 PROFILE_FPS=60 PROFILE_DURATION_MS=10000 node --cpu-prof --cpu-prof-dir=benchmarks/obs-cef/2026-10-09-optimization/profiles benchmarks/obs-cef/profile-media.mjs > "$optimization_logs/latest-4k-record.log" 2>&1
node --expose-gc benchmarks/obs-cef/soak.mjs > "$optimization_logs/soak.log" 2>&1
