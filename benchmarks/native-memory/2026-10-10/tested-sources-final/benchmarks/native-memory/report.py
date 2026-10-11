"""Generate the saved memory-hardening evidence report from collected results."""
import datetime
import json
import pathlib
import re
import statistics
from zoneinfo import ZoneInfo

root = pathlib.Path(__file__).resolve().parents[2]
evidence = root / 'benchmarks/native-memory/2026-10-10'


def lines(path):
    return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []


def test_result(name):
    path = evidence / 'logs' / name
    text = path.read_text(errors='replace') if path.exists() else ''
    matches = re.findall(r'100% tests passed(?:, 0 tests failed)? out of (\d+)', text)
    return {'passed': bool(matches), 'count': int(matches[-1]) if matches else None, 'log': name}


def fuzz_result(name):
    text = (evidence / 'logs' / name).read_text(errors='replace')
    matches = re.findall(r'Done (\d+) runs in (\d+) second', text)
    return {'completed': bool(matches), 'runs': int(matches[-1][0]) if matches else 0,
            'seconds': int(matches[-1][1]) if matches else 0, 'log': name}


def summarize_memory(rows, field, roles):
    values = [sum(p.get(field, 0) for p in row['processes'] if p['role'] in roles) / 1048576 for row in rows]
    if not values:
        return None
    count = max(1, min(10, len(values) // 3))
    return {'samples': len(values), 'firstMiB': values[0], 'lastMiB': values[-1],
            'maxMiB': max(values), 'firstWindowMedianMiB': statistics.median(values[:count]),
            'lastWindowMedianMiB': statistics.median(values[-count:]),
            'medianWindowChangeMiB': statistics.median(values[-count:]) - statistics.median(values[:count])}


soak = json.loads((evidence / 'soak-release/result.json').read_text())
statuses = lines(evidence / 'soak-release/output-status.jsonl')
rss = lines(evidence / 'soak-release/processes.jsonl')
private = lines(evidence / 'soak-release/private-memory.jsonl')
active_private = [row for row in private if any(p['role'] == 'cef-browser/utility' for p in row['processes'])]
roles = ['node', 'cef-renderer', 'cef-gpu', 'cef-browser/utility']
memory = {role: summarize_memory(active_private, 'private', [role]) for role in roles}
memory['totalPrivate'] = summarize_memory(active_private, 'private', roles)
memory['totalPss'] = summarize_memory(active_private, 'pss', roles)
fuzz = [fuzz_result(name) for name in ['fuzz-bounds.log', 'fuzz-media.log', 'fuzz-media-final-static.log', 'fuzz-bounds-final.log', 'fuzz-media-verified-final.log'] if (evidence / 'logs' / name).exists()]
tests = [test_result(name) for name in ['asan-verified-final-tests.log', 'tsan-verified-final-tests.log', 'release-verified-final-tests.log']]
cef_log = (evidence / 'logs/cef-final-tests.log').read_text(errors='replace')
cef_matches = re.findall(r'(\d+) passed \(', cef_log)
cef_tests = {'passed': bool(cef_matches) and not re.search(r'\d+ failed', cef_log),
             'count': int(cef_matches[-1]) if cef_matches else None, 'log': 'cef-final-tests.log'}
validation_path = evidence / 'soak-release/media-validation.json'
media_validation = json.loads(validation_path.read_text()) if validation_path.exists() else []
summary = {'generatedAt': datetime.datetime.now(ZoneInfo('Asia/Calcutta')).isoformat(),
           'memorySafetyGuarantee': False, 'confirmedMemoryBugsFixed': ['codec-pointer data race', 'scaler replacement failure double-free'],
           'soak': soak, 'memory': memory, 'fuzz': fuzz, 'nativeTests': tests,
           'cefIntegration': cef_tests, 'mediaValidation': media_validation,
           'privateSamples': len(private), 'activePrivateSamples': len(active_private),
           'outputSamples': len(statuses), 'rssSamples': len(rss),
           'maxVideoQueuedBytes': max((o['queuedVideoBytes'] for row in statuses for o in row['outputs']), default=0),
           'maxAudioQueuedBytes': max((o['queuedAudioBytes'] for row in statuses for o in row['outputs']), default=0),
           'privateSamplerMaxMs': max((r['samplingMs'] for r in private), default=0),
           'cefSanitizerCoverage': 'Blocked: stock CEF 154 aborts during initialization; minimal C API reproduction has no SuiteCut Source or media code.'}
(evidence / 'summary.json').write_text(json.dumps(summary, indent=2))

rows = '\n'.join(f"| {role} | {data['firstWindowMedianMiB']:.1f} | {data['lastWindowMedianMiB']:.1f} | {data['maxMiB']:.1f} | {data['medianWindowChangeMiB']:+.1f} |" for role, data in memory.items() if data)
fuzz_rows = '\n'.join(f"| {item['log']} | {item['runs']:,} | {item['seconds']} | {'completed' if item['completed'] else 'not completed'} |" for item in fuzz)
test_rows = '\n'.join(f"| {item['log']} | {item['count'] or 'pending'} | {'passed' if item['passed'] else 'pending/failed'} |" for item in tests)
elapsed = soak.get('elapsedMs', statuses[-1]['atMs'] if statuses else 0) / 60000
report = f'''# SuiteCut native memory hardening and profiling

Generated: {summary['generatedAt']} (Asia/Calcutta). Repository: `/workspace/suitecut`.

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
{fuzz_rows}

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
{test_rows}

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
Final real-CEF integration: **{cef_tests['count'] or 'pending'} tests,
{'passed' if cef_tests['passed'] else 'pending/failed'}**. This includes scrolling,
input, recording/streaming, CDP sessions, popup/context closure, a session-cap
regression and engine close during a pending pause. SDK verification passed
309 tests in 37 files; typecheck, lint and SDK compilation passed.

## Sustained resource profile

Run status: **{soak['status']}**. Requested: 30 minutes. Observed: {elapsed:.2f}
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
{rows}

The total excludes the RTMP receiver and sampler. Browser/utility includes
zygotes and utility processes. Renderer counts vary briefly when contexts open.
Maximum private-sampling cost: {summary['privateSamplerMaxMs']:.1f} ms. Continuous
output status recorded maximum queued video {summary['maxVideoQueuedBytes']:,}
bytes and audio {summary['maxAudioQueuedBytes']:,} bytes per output. Final
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
'''
(evidence / 'report.md').write_text(report)
print(json.dumps({'soak': soak['status'], 'elapsedMinutes': elapsed, 'fuzzInputs': sum(f['runs'] for f in fuzz), 'summary': str(evidence / 'summary.json')}))
