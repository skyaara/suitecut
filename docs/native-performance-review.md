# Native engine and browser-control performance review

Reviewed on Linux, 17 October 2026. This review covers the owned C++ browser
engine, native capture and resource pipelines, and the standalone
`@suitecut/browser-control` SDK/viewer. The installed CEF/Chromium library remains
the pinned, prebuilt CEF 156 runtime. It was not rebuilt or instrumented.

The changes remove repeated work, fix resource retirement, and reuse existing
ownership boundaries. They introduce no new runtime dependency or service and
preserve frame geometry, native acknowledgements, input ownership, recording
cadence, and bounded queues. Browser Test/fixture/reporting compatibility and
other language bindings are outside this review.

## Implemented changes

| Area | Finding and resulting change |
| --- | --- |
| Paint retention | CEF supplies complete pixels plus damage rectangles. Unchanged-size paints now copy only validated dirty rows; initial, resized, empty, invalid or sufficiently large damage takes a full copy. Skipped paints invalidate damage history, forcing one complete snapshot after screenshot capture without discarding the retained media frame. Popup pixels follow the same rules. |
| Capture fitting/conversion | Resizing and I420 conversion read retained BGRA pixels directly when popup composition is unnecessary. This removes one source-sized temporary copy. Fitting initializes opaque alpha only in actual letterbox pixels. |
| Worker packet decoding | Worker events and worker-console predicates reuse their validators instead of constructing a validation graph for each packet. Validation and owned payload buffers remain intact. |
| Frame subscription cleanup | A listener whose synchronous cached-frame delivery throws is removed immediately; pre-existing subscribers remain registered. |
| Trace stacks | Each action stages only newly observed filenames rather than copying the accumulated map. IDs, capacity checks, failed-capture rollback and output remain unchanged. |
| Trace storage | Completed/replaced chunks release trace-string and stack-vector allocations. Full trace-session retirement also clears source-path deduplication storage while outstanding physical reads remain tracked until completion. |
| Trace ZIP output | A constexpr 1 KiB standard CRC32 table replaces eight rounds per byte. The archive format and bytes remain identical. |
| HAR replay | A sorted vector of entry indices narrows URL/method candidates without duplicating retained URLs or bodies. Request headers are normalized once; incoming URL, method and headers are read once per lookup. Original score ties and multipart handling are preserved. |
| Native network journal | Each immutable history record keeps its serialized byte count. Eviction no longer serializes it again. Existing 256-event and 4 MiB limits remain. |
| SDK body replay | Consumption no longer dereferences every weak replay entry. A bounded FIFO limits identities; payloads are dereferenced when requested. This avoids quadratic work and temporary GC retention. |
| SDK network projection | Metadata is charged after its final event update rather than twice per event. Wait-observed response growth is now charged correctly. Navigation lookup avoids three temporary arrays. |
| Closed context workers | Lazy adapters created after context closure immediately detach their subscription. Late receipt projection still works without retaining a transport listener. |
| Viewer encoding | JPEG jobs require current client demand. One encoded image serves concurrent clients, and idle/hidden viewers stop requesting encoding. The server retains one codec job, a latest raw frame and a published JPEG. |
| Viewer polling/UI | Optional authenticated `wait=1` frame requests wait at most 1,000 ms, with at most 32 pending waits. Frame, revision, timeout, disconnect and shutdown release waits. Toolbar/tab rebuilding is removed from the per-frame path; request/decode time is included in frontend pacing. Existing immediate polling remains supported. |

## Measured component results

These are scoped paired workloads on the same shared cloud host. A component
speedup is not a whole-browser speedup. Fixtures exercise the real changed
helper or emitted SDK path, with before/after samples and input hashes retained.

| Workload | Before → after | Result |
| --- | --- | --- |
| 4K BGRA to I420, without popup composition | 7,234 → 3,734 µs | 1.94× throughput; one 31.64 MiB temporary BGRA vector removed |
| 4K to 720p fitting | 5,394 → 1,111 µs | 4.86× throughput |
| 1080p to 4K fitting | 11,872 → 6,218 µs | 1.91× throughput |
| Worker event delivery, 2,000 packets | 243.65 → 12.01 ms | 20.28× throughput |
| Worker-console predicate delivery, 2,000 packets | 1,028.50 → 26.11 ms | 39.39× throughput |
| Populated trace-file index stack capture | 1,590.42 → 26.64 ms | 59.7× throughput |
| Eight 16 MiB ZIP exports | 1,374.20 → 438.99 ms | 3.13× throughput; exact ZIP bytes preserved |
| Duplicate-URL HAR lookup, 1,024 entries and 128 headers | 1,100.69 → 5.63 ms | 195× throughput; deliberately expensive legal archive |
| Distinct-URL HAR lookup, 4,096 entries | 1.379 → 0.0174 ms | 79× throughput; at most 32 KiB of index storage |
| Native network journal workload | 540.45 → 426.69 ms | 21% less elapsed time |
| SDK replay body consumption, 12,000 bodies | 1,856.41 → 47.54 ms | 39× throughput |
| SDK request projection, 1,000 three-stage lifecycles | 168.75 → 109.55 ms | 1.54× throughput |

Small damage rectangles substantially reduce paint-retention copying, while
published frames are still complete. The isolated 4K 128×64 damage copy measured
2,367 → 0.875 µs. This says nothing about the unchanged full-frame IPC cost or
overall delivered FPS. Portrait-to-1080p fitting varied slightly in the opposite
direction, so no gain is claimed for that workload.

In a controlled body-replay/GC workload, ArrayBuffer usage during replay fell
from 67,270,907 to 16,414,971 bytes, while the same bounded replay archive remained
available. Four completed trace chunks released 32 MiB of allocation capacity
that previously survived while their logical byte count was zero. These are
specific retained-allocation findings, not estimates of whole-browser savings.

The paired 1080p Sharp viewer fixture measured the 1.5 seconds after its last
frame demand: 22 → 0 extra JPEG jobs, 1,296 → 57.37 ms CPU, and peak RSS
222.2 → 147.6 MB. The encoded JPEG bytes were identical. This fixture measures
viewer encoding, not Chromium or complete native-process memory.

The matched 1,966-operation live browser comparison completed in 126.03 →
102.98 seconds with identical operation counts and passing assertions. This is
an observed 18.3% reduction on the shared host, not a portable throughput
guarantee. Its late native PSS was approximately 397 → 399 MiB and its post-GC
Node heap approximately 57.8 → 58.0 MiB; no overall memory saving is claimed for
this mixed workload. The specific retained-allocation fixes above remain useful
for workloads that exercise those resources.

The three-sample real 4K capture probe measured median total Node plus native
CPU per delivered frame of 120.38 → 103.33 ms for BGRA and 88.37 → 44.19 ms
for I420. These sum CPU consumed on multiple cores; they are not frame latency.
I420 native CPU alone fell from 20.17 → 10.66 ms per delivered frame. BGRA native
CPU alone increased in these noisy samples despite improved total CPU and
delivered FPS, so no isolated native BGRA saving is claimed. The real low-demand
viewer delivered 14 → 15 JPEGs while producing 91 → 15 encodes; its idle window
produced 46 → 0 additional encodes. Native/raw frame work continues while JPEG
encoding is idle.

## Verification and evidence

The integrated browser-control checks cover 808 SDK tests across 113 files.
Native capture tests compare every fitted byte against the previous algorithm
for 10,000 deterministic randomized cases, including alpha, popup/damage
geometry, overlapping rectangles, resize stride changes and invalid inputs.
Changed trace/HAR helpers have sanitizer coverage; network metadata tests use
real pinned CEF values and check byte/count eviction, replay, cursor rejection,
consumer mutation isolation and clearing. ZIP output is checked byte-for-byte
and decoded with independent CRC validation.

The matched real-browser workload uses seed 123456789, 1,966 operations and 128
context/page creation-navigation-close cycles per variant. It records post-GC
Node memory, renderer heap, aggregate native PSS and actual diagnostics. Separate
real 4K BGRA/I420 capture and low-demand viewer probes record CPU ticks, delivered
frames and memory. Human/AI handover, resolution changes and recording checks
cover integration boundaries. The final machine-readable receipt records which
commands completed and their actual helper/descendant shutdown results.

The final skipped-paint recovery guard was added after the throughput comparison.
Its dedicated sanitizer case proves stale pixels would survive an unguarded
partial update. The final guarded binary passed full-page screenshot-to-live-paint
restoration, three tracing sessions on one live context, all eight handover and
seven resolution groups, and complete decoding of short 60 FPS (99 frames) and
120 FPS (205 frames) I420 recordings across an odd viewport resize. These output
rates describe CFR resampling; they do not claim every encoded image is distinct.

Local evidence locations:

- Combined build, unit tests, live comparisons, package and final handoff:
  `/workspace/suitecut-performance-review-20261017/`.
- Capture correctness and paired timings:
  `/workspace/suitecut-native-performance-20261017/frame/review.json`.
- Tracing and network journal:
  `/workspace/suitecut-native-performance-review-20261017/resources/REVIEW.json`.
- HAR replay and sanitizer results:
  `/workspace/suitecut-browser-performance-proof-20261017/har-replay/review.json`.
- Transport validator samples:
  `/workspace/suitecut-perf-review-20261017/transport/paired-final.json`.
- SDK lifecycle/GC findings:
  `.suitecut/diagnostics/sdk-lifecycle-review/REVIEW.json`.
- Viewer codec and HTTP lifecycle checks:
  `/workspace/suitecut-viewer-performance-20261017/`.

## Architecture and remaining limits

The existing division remains appropriate: C++ owns browser state, actual pixel
capture, media workers and physical operation lifetime; the SDK owns command and
event projection; the viewer shares one bounded encoder. Small helpers and
metadata indices address the measured costs without a new framework or a
replacement transport. The standalone package retains only Sharp, YAML and Zod
as external runtime dependencies.

Painted raw frames still cross the native pipe at capture cadence, including
DOM-only pages without frame subscribers. A 4K BGRA frame is 31.64 MiB; native paint
storage and SDK last-frame caches scale with page count. The 128-page admission
limit is not a promise that 128 simultaneous 4K pages fit comfortably in RAM.
An explicit demand-driven capture subscription would address this remaining
bandwidth/memory cost, but needs defined cache, recording, late-subscriber,
ownership and backpressure behavior. This review preserves those contracts.

The native `browser.cc` coordinator still has many textual includes and is
expensive to compile. Splitting it would primarily improve build times and
requires careful separation of private ownership; no runtime speedup is claimed
from file reorganization. A broad rewrite would add risk without addressing
the costs measured here.

Whole Chromium/V8/GPU heap instrumentation, hardware encoders, production
network conditions and multi-hour multi-machine soaks remain separate work.
Finite unit/sanitizer/stress checks do not prove global leak freedom. The
existing custom-CEF capability limits are unchanged; see the active browser
backlog and the current installed build receipt.
