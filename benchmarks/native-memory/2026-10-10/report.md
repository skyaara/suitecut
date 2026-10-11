# SuiteCut native memory hardening and profiling

Generated: 2026-10-10T16:28:49.735949+05:30 (Asia/Calcutta). Repository: `/workspace/suitecut`.

## Outcome

Two native memory defects were reproduced against the pre-change source and
fixed: a codec-pointer data race and a scaler replacement double-free. Additional
hardening covers media timestamps, PCM and popup bounds, partial trace opens,
worker shutdown, retained child/session ownership and long-lived command counters.
A reproducible pause hang was also fixed. The backend remains C++; these changes
improve safety but do not make every execution or dependency provably memory-safe.

**Full CEF AddressSanitizer coverage remains blocked.** The stock CEF 154 binary
aborts during initialization with sanitizer instrumentation. A minimal C API
program without SuiteCut Source, media, SDK or browser automation code reproduces
the abort. Normal sandboxed CEF integration and measured resource stability are
separate evidence; they must not be presented as clean full-browser sanitizer runs.

## Confirmed findings and fixes

| Finding | Evidence before the fix | Change and verification |
|---|---|---|
| Codec lifetime race | ThreadSanitizer reports `Impl::Codec` writing the codec pointer while `MediaOutput::Pause` reads it. The saved symbolized trace identifies both pre-change source locations. | Only encoder workers access codec pointers. Frame-size metadata is published under the media mutex and used by pause. Concurrent lifecycle and instrumented native tests pass. |
| Scaler double-free | A wrapped scaler replacement frees its old context and returns null, matching FFmpeg's failure ownership. AddressSanitizer reports the old `Cleanup` freeing that context again. | Store `sws_getCachedContext`'s return value before checking failure, leaving null ownership on failure. The identical injected failure now returns a controlled error and passes sanitizers. |
| Pause boundary hang | The real CEF blocked-Node/pause/page-switch test timed out reproducibly, while teardown reported an unknown output after the test timeout. | Fractional clock origins can round an aligned endpoint above its completed sample count. A one-nanosecond completion tolerance avoids an impossible wait. Native tests cover four nonzero fractional origins; the real CEF test subsequently passed. |
| Startup/offline queue edge | Audit found pause can clear the offline video queue while a retained first frame still satisfies readiness. | Origin selection handles an empty queue and retains the first frame safely. The media fuzzer exercises early pause and offline output. |
| Unsafe timestamp arithmetic | Packet offsets used unchecked rounding; clip seek used an unchecked cast/add; decoded timestamp subtraction could overflow. | Native timestamps are finite and bounded before conversions, PCM size is bounded, seeks use checked rescaling/addition, and decoded subtraction is guarded. NaN render durations and null-byte local paths are rejected. |
| Pending shutdown work | Closing cleared output ownership before joining pause workers that could still wait on encoders; render cancellation was not requested first. | Move output ownership out under the Source mutex, stop outside it, cancel render jobs, then join workers. Native producers/pauses/two simultaneous stops are exercised in 160 cycles, including weak-owner release checks. |
| Retention and late callbacks | Failed target/popup creation and moving an output during pending lifecycle work could retain stale ownership. | Failed synchronous creation and aborted/opener-closed popups release child ownership; child callbacks retain the root owner. Selection rejects pending lifecycle work. Worker, context/target and session retention are bounded. |
| Diagnostic file ownership | If CSV creation failed but PCM creation succeeded, repeated Open calls could replace the PCM handle without closing it. | Trace owners are noncopyable, serialize access and attempt opening once. Diagnostics still use synchronous writes; keep them disabled for timing benchmarks. |
| Long-lived counter overflow | Signed C++ DevTools/context counters had no wrap boundary. | DevTools IDs rotate within positive int range and skip live requests; context string counters use unsigned 64-bit storage. Boundary regressions and the bounds fuzzer cover the helper. |

Only the first two rows have before/after sanitizer defect reproductions. The
remaining rows are audit hardening or functional/concurrency regressions, not
claims that every issue was independently found by fuzzing.

## Fuzzing and instrumentation

| Campaign | Inputs | Seconds | Result |
|---|---:|---:|---|
| fuzz-bounds.log | 78,924,359 | 181 | completed |
| fuzz-media.log | 122,774 | 181 | completed |
| fuzz-media-final-static.log | 123,118 | 181 | completed |
| fuzz-bounds-final.log | 28,375,940 | 61 | completed |
| fuzz-media-verified-final.log | 34,723 | 61 | completed |

The bounds target invokes production audio overlap copying, popup compositing,
packet-clock normalization and, in the final campaign, positive command-ID
rotation. Inputs include arbitrary double/int bit patterns and clipped/offscreen
popup coordinates. The media target opens real FFmpeg encoder/muxer outputs and
mutates packet timing, audio state, pause/resume/failure, offline/realtime mode,
30/60 FPS and fractional origins. Outputs are intentionally bounded to 40 ms.

These are coverage-guided libFuzzer campaigns with AddressSanitizer,
UndefinedBehaviorSanitizer and LeakSanitizer enabled. Corpus files, logs and
tested binaries are saved. A passing campaign means no sanitizer finding was
observed in those inputs; it is not complete state-space or codec/Chromium coverage.

| Native check | Tests | Result |
|---|---:|---|
| asan-verified-final-tests.log | 7 | passed |
| tsan-verified-final-tests.log | 7 | passed |
| release-verified-final-tests.log | 7 | passed |

The thread build uses instrumented FFmpeg 7.1.5 for the exercised formats and
filters. The first run with prebuilt FFmpeg flagged an ambiguous decoder buffer
handoff; rebuilding FFmpeg made its atomic operations visible and the tested
native paths passed without suppressions. x264/vpx binaries and assembly remain
outside full compiler instrumentation. Address/undefined/leak tests use the
normal linked codec libraries, so heap interceptors cover allocations but do not
instrument every internal read/write in those prebuilt libraries.

Clang static analysis of the three native media translation units also produced
no diagnostic. That does not analyze CEF's implementation or prove thread safety.
SDK tests, typecheck/lint and real CEF integration logs are saved separately.
Final real-CEF integration: **15 tests,
passed**. This includes scrolling,
input, recording/streaming, CDP sessions, popup/context closure, a session-cap
regression and engine close during a pending pause. SDK verification passed
309 tests in 37 files; typecheck, lint and SDK compilation passed.

## Sustained resource profile

Run status: **passed**. Requested: 30 minutes. Observed: 30.00
minutes. The workload records H.264/AAC and streams H.264/AAC to a local RTMP
receiver simultaneously, at 640×360 and 30 FPS. It continuously draws a canvas,
plays an oscillator, creates/closes browser contexts and explicit CDP sessions,
pauses/resumes recording, reloads the active page and starts/stops short
FFV1/PCM recordings. No raw media subscription is used.

Final workload counters, output counters and errors are in
`soak-release/result.json`. The source deliberately reloads its AudioContext;
this is a memory/lifecycle workload, not the continuous-noise OBS audio-quality
comparison. Nonzero missing-audio counters must not be mislabeled clean audio.

Private-memory/PSS sampling begins later than the continuous RSS sampling and
uses 17-second intervals. First/last windows below each use up to ten samples;
they cover the available private-memory samples, not the first ten seconds of
the whole run. Trend calculations exclude samples after CEF has exited; raw
shutdown samples remain saved. Shared pages make summed process RSS an overcount of physical
memory, so PSS and private pages are reported separately.

| Group | First window median MiB | Last window median MiB | Maximum MiB | Change MiB |
|---|---:|---:|---:|---:|
| node | 113.7 | 116.0 | 116.0 | +2.3 |
| cef-renderer | 54.1 | 53.9 | 55.9 | -0.2 |
| cef-gpu | 29.9 | 30.0 | 31.0 | +0.0 |
| cef-browser/utility | 197.6 | 198.7 | 198.7 | +1.1 |
| totalPrivate | 395.5 | 398.5 | 400.5 | +2.9 |
| totalPss | 567.0 | 571.3 | 580.4 | +4.3 |

The total excludes the RTMP receiver and sampler. Browser/utility includes
zygotes and utility processes. Renderer counts vary briefly when contexts open.
Maximum private-sampling cost: 99.5 ms. Continuous
output status recorded maximum queued video 5,184,000
bytes and audio 61,440 bytes per output. Final
successful shutdown must report zero retained queue bytes.
All three saved outputs (recording MP4, received FLV and short MKV) were fully
decoded for both video and audio with FFmpeg error checking; exit codes and
stream/container probes are saved in `soak-release/media-validation.json`.

This short host-specific profile can show bounded resource use for the tested
workload. It does not establish 24-hour leak freedom, all-platform safety, 4K60
resource behavior or GPU encoding safety. Early portions overlap diagnostic
builds/fuzzing; a brief independent CEF initialization probe also used this host.
The workload does not support an OBS memory-efficiency comparison.

## Full CEF sanitizer blockage

Static and shared ASan-runtime attempts abort with glibc invalid-free checks.
The debugger captures calls from libcef during initialization. The minimal
`cef_probe.cc` uses CEF's C API and no SuiteCut client/encoder code; its debugger
trace reproduces the failure. This narrows the issue to the stock CEF/sanitizer
combination but does not identify its internal allocation cause. Leak detection
was disabled only for debugger isolation; those debugger runs are not passing
leak checks. A compatible instrumented CEF/Chromium distribution remains needed.

No sandbox bypass or broad sanitizer suppression was used to obtain a pass.
Failed launch/link/probe attempts are retained. The initial CEF run happened
before the X11 link path was corrected and found no executable; later attempts
reproduced the CEF sanitizer abort. An intermediate shared-runtime fuzz launch
failed sanitizer link-order validation before fuzzing; the final fuzz run uses
the ordinary static sanitizer linkage. An initial FFmpeg instrumentation build
omitted VP9/FLAC, and its missing-codec failures are retained before the complete
configuration. A Make regeneration initially left one new test unbuilt; final
verification builds and runs that target explicitly.

## Evidence and scope

`tested-sources-before/` preserves the code used for the two defect reproductions.
`tested-sources-final/` preserves the final native code and collectors.
`tested-binaries/`, `provenance.json`, compiler/package SHA metadata and FFmpeg
configuration record the instrumentation. `soak-tested-sources/suitecut-browser`
is the exact executable used for the sustained run. It includes the codec,
scaler, timestamp, trace and shutdown fixes, but predates the final popup/root
ownership and command-counter follow-up; final focused CEF tests cover that
follow-up. Do not describe the sustained run as testing a byte-identical final
executable.

The report, raw status/process samples, recordings, generated fuzz corpus and
all failed/passing logs remain in this directory. SHA seals and the external
archive make that snapshot reviewable. Historical OBS/native benchmark seals
are verified separately and must remain unchanged.

The earlier audio-generation stalls are still an open issue. Fuzzing is useful
for malformed buffers and lifecycle combinations; timing traces and matched
stress fixtures remain necessary to investigate missing audio. This work does
not claim that those interruptions are fixed or that SuiteCut outperforms OBS.

Reproduction instructions: `native/browser/MEMORY_SAFETY.md`.
