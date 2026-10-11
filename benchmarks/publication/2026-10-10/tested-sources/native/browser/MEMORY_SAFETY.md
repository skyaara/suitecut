# Native memory safety checks

The native backend uses owned CEF references, immutable shared media buffers,
bounded queues, joined workers, and checked audio/video boundaries. C++ and the
prebuilt CEF/codec dependencies do not provide a universal memory-safety guarantee.

## Instrumented native tests

Use separate build directories. Supply the pinned CEF/libyuv distributions and
FFmpeg development libraries as described in README.md. Diagnostic builds are
not release binaries.

```sh
cmake -S native/browser -B build-memory-asan \
  -DCEF_ROOT=/absolute/cef -DLIBYUV_ROOT=/absolute/libyuv \
  -DSUITECUT_FFMPEG_ROOT=/absolute/ffmpeg-dev \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSUITECUT_SANITIZERS=address,undefined
cmake --build build-memory-asan --target suitecut-media-test \
  suitecut-audio-startup-test suitecut-codec-test suitecut-audio-clock-test \
  suitecut-pixel-test suitecut-memory-test suitecut-scaler-failure-test
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-memory-asan --output-on-failure
```

The scaler fault-injection target uses GNU linker wrapping and is available on
Linux. The lifecycle regression runs 160 concurrent start/pause/produce/stop
cycles, checks released weak buffer owners, exercises fractional pause origins,
and rejects non-finite clocks and malformed buffers. It is an ownership and
concurrency check, not a duration soak.

For races, use `-DSUITECUT_SANITIZERS=thread` in another directory. Run the media,
audio-startup, and memory-lifecycle tests with `TSAN_OPTIONS=halt_on_error=1`.
FFmpeg must also be built with ThreadSanitizer to make its internal atomic
handoffs visible. The stock FFmpeg decoder emitted an ambiguous warning here;
the matching 7.1.5 instrumented build passed all seven native test groups without
suppressions. External x264/vpx code and assembly remain outside that coverage.

## Coverage-guided fuzzing

Use Clang and add `-DSUITECUT_FUZZ=ON` to the address/undefined build. Build
`suitecut-fuzz-bounds` and `suitecut-fuzz-media`. Supply a corpus directory and
save generated inputs, failure artifacts, logs, toolchain identity and binary
hashes:

```sh
mkdir -p corpus-bounds corpus-media failures
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  build-memory-asan/suitecut-fuzz-bounds corpus-bounds \
  -max_total_time=600 -max_len=256 -timeout=10 -artifact_prefix=failures/
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  build-memory-asan/suitecut-fuzz-media corpus-media \
  -max_total_time=600 -max_len=128 -timeout=10 -artifact_prefix=failures/
```

The bounds target calls the production audio-overlap copy, clipped popup
compositor, packet clock and command-ID rotation with arbitrary bit patterns. The media target
runs actual FFmpeg encoders with mutated packet timing and lifecycle operations,
bounded output duration, fractional origins, offline/realtime modes and 30/60 FPS.
Neither target is a fuzz test of all Chromium, all media formats or the whole CDP
protocol. Timing stalls need tracing and stress tests in addition to fuzzing.

## Full CEF instrumentation limit

On this host, stock CEF 154 aborts inside `cef_initialize` when combined with
AddressSanitizer. Both static and shared ASan runtime attempts failed. A minimal
C API program with no SuiteCut Source/encoder code reproduces the abort; GDB
records glibc invalid-free checks called from libcef. This isolates the problem
to the stock-CEF/sanitizer combination but does not identify its internal cause.
Do not suppress it, disable the Chromium sandbox, or count those runs as passed
browser checks. A compatible instrumented CEF/Chromium distribution is still
required for complete browser-process coverage.

Linux's default subsequently changed to codec-enabled CEF 152 for H.264/AAC
playback. The stock-154 sanitizer failure above is historical evidence, not a
result for that replacement. Normal browser/playback checks passed on 152;
complete sanitizer coverage of its prebuilt Chromium dependency remains unverified.

The Linux subprocess dispatcher now follows CEF's `NO_STACK_PROTECTOR` entry
pattern. Chromium resets its stack canary after a zygote fork; a protected caller
around `CefExecuteProcess` can fail its old-canary check on subprocess return.
Only the small Linux `main` dispatcher is exempted. `RunSuiteCutBrowser`, media
code, Chromium's canary reset and the process sandbox retain their protections.
Saved disassembly verifies this boundary. This correction is separate from
sanitizer coverage and does not prove an absence of memory bugs in dependencies.

Real CEF runs use the normal sandboxed executable and save ownership/lifecycle
checks, queue sizes, process RSS, and proportional/private memory. Bounded RSS or
PSS supports resource stability for the measured workload; it does not prove
absence of leaks or invalid access in an uninstrumented dependency.

## Behavior and ownership contracts

- Only encoder workers access FFmpeg codec pointers. Pause uses frame-size
  metadata published under the media mutex, and accepts a one-nanosecond
  tolerance when checking a completed sample boundary.
- Scaler replacement assigns the returned pointer before checking failure:
  FFmpeg may already have freed the previous context when it returns null.
- Native packets reject non-finite/out-of-range timestamps, malformed sizes and
  oversized PCM. Producers must supply immutable buffers and a valid clock.
- Shutdown removes outputs under the Source mutex, stops them outside that
  mutex, cancels render jobs, then joins lifecycle workers.
- Outputs cannot move between sources while pause/stop work is pending. Native
  file/lifecycle workers are capped at eight per Source; contexts and child
  targets are capped at 64 and explicit session maps at 256.
- Child callbacks retain their protocol owner. Closed and failed child creation,
  aborted popups, and opener shutdown remove the corresponding child ownership.
- DevTools command IDs wrap within the positive integer range and skip pending
  requests. Context name counters use unsigned 64-bit storage.
- Diagnostic audio traces are noncopyable, serialize file access, and attempt
  file opening only once, including partial-open failures. They still perform
  synchronous diagnostic writes and should be disabled for performance captures.

The detailed evidence is saved under `benchmarks/native-memory/2026-10-10/`.
The earlier OBS comparison evidence is separate and remains sealed. These fixes
do not establish that Chromium audio-generation gaps are resolved.
