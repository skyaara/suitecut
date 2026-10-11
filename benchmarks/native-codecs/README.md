# Native codec validation

The C++ media core supports H.264, HEVC, VP8, VP9, AV1 (SVT and AOM), FFV1,
AAC, Opus, FLAC and PCM. Discovery reports what is linked and compatible with
each container. NVIDIA encoder entries require separate device verification.

The [`2026-10-10/report.md`](2026-10-10/report.md) report saves codec-specific
capture/render results, decoded continuity, resource samples, lossless comparisons
and limitations. The existing 30-minute H.264/OBS evidence remains separate under
`benchmarks/native-engine/2026-10-09`; it is not a 30-minute validation of every
new codec.

Codec/decode checks pass with continuity findings: the stress captures contain
short audio gaps (up to 210 ms internally, plus 520 ms at first audio startup).
The report separates these measurements from native queue-drop counters and
does not claim gap-free operation or superiority over OBS for the new codecs.

After building the native helper and SDK on a host with matching CEF, FFmpeg,
Xvfb and PulseAudio dependencies:

```sh
cmake --build .suitecut/native-build --target suitecut-browser suitecut-media-test suitecut-codec-test -j2
ctest --test-dir .suitecut/native-build --output-on-failure
SUITECUT_CODEC_TEST_OUTPUT="$PWD/benchmarks/native-codecs/2026-10-10/core" .suitecut/native-build/suitecut-codec-test
python3 benchmarks/native-engine/runtime.py benchmarks/native-codecs/matrix.mjs
python3 benchmarks/native-codecs/analyze.py benchmarks/native-codecs/2026-10-10
```

`matrix.mjs` uses an actual CEF page with animation, tone audio, typing, clicks,
selection and scrolling. Each software pairing records 1080p30 for 20 seconds,
including a four-second stall in Node. Each also renders an existing SuiteCut
presentation containing narration, captions, zoom/highlight, checkpoints and page
selection. An additional test simultaneously records VP9/Opus and publishes
H.264/AAC to a local RTMP receiver. Hardware startup is expected to fail on this
GPU-less host; successful-device benchmarks are not inferred from encoder discovery.

`analyze.py` uses separate FFmpeg/ffprobe processes for full decode, frame
timestamps, animation markers and audio continuity. It independently compares
synthetic FFV1 output pixels and FLAC/PCM samples byte for byte with the native
input. Resource sampling uses the existing independent `/proc` collector.

The bundle includes source media and `render-fixture/manifest-portable.json`,
whose artifact paths are relative to the manifest. Replay that presentation in
the native engine with:

```sh
python3 benchmarks/native-engine/runtime.py benchmarks/native-codecs/replay.mjs
```

The sanitizer stress builds the C++ media core separately from CEF:

```sh
c++ -std=c++20 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -pthread \
  -I .suitecut/native-media-deps/root/usr/include/x86_64-linux-gnu \
  native/browser/media_engine.cc native/browser/media_file.cc \
  native/browser/media_render.cc native/browser/media_codec_test.cc \
  -L .suitecut/native-media-deps/root/usr/lib/x86_64-linux-gnu \
  -lavformat -lavcodec -lavutil -lswscale -lswresample -lavfilter \
  -o .suitecut/native-codecs/codecs-asan
SUITECUT_CODEC_TEST_REPETITIONS=20 \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:quarantine_size_mb=64 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  .suitecut/native-codecs/codecs-asan
```
