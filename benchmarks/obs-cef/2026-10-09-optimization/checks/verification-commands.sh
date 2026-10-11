#!/usr/bin/env bash
set -euo pipefail
# Run from the repository root after SDK/runtime setup described in README.md.
export DISPLAY=:99
export PULSE_SERVER="unix:$PWD/.suitecut/runtime/pulse.sock"
export SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser"
pnpm build
pnpm exec vitest run --maxWorkers 1 --testTimeout 30000 \
  tests/native-browser.test.ts tests/native-protocol.test.ts tests/native-page.test.ts \
  tests/native-recorder.test.ts tests/native-session.test.ts tests/native-recording.test.ts \
  tests/native-broadcast.test.ts tests/media.test.ts tests/live-stream.test.ts tests/live-audio.test.ts
pnpm typecheck
pnpm exec eslint src/live-audio.ts src/live-stream.ts src/media.ts src/native-browser.ts \
  src/native-page.ts src/native-protocol.ts src/native-recorder.ts src/native-session.ts \
  src/native-video-container.ts tests/live-audio.test.ts tests/live-stream.test.ts \
  tests/media.test.ts tests/native-browser.test.ts tests/native-page.test.ts \
  tests/native-recorder.test.ts tests/native-session.test.ts --max-warnings 0
node scripts/verify-native-browser.mjs
node scripts/verify-native-recording.mjs
node scripts/verify-native-stream.mjs
node benchmarks/obs-cef/verify-container.mjs
python3 -m cmake --build .suitecut/native-build --target test
# Scope: helper source is instrumented; prebuilt CEF/libyuv is not.
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  native/browser/audio_clock_test.cc -o .suitecut/native-build/suitecut-audio-clock-asan
ASAN_OPTIONS=detect_leaks=1 .suitecut/native-build/suitecut-audio-clock-asan
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I .suitecut/libyuv/include native/browser/pixel_test.cc \
  .suitecut/native-build/libyuv/libyuv.a -o .suitecut/native-build/suitecut-pixel-asan
ASAN_OPTIONS=detect_leaks=1 .suitecut/native-build/suitecut-pixel-asan
