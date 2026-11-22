# suitecut embedded browser

The default Chromium Embedded Framework (CEF) capture backend for SuiteCut 2.0. Each launch owns a
sandboxed CEF helper that can host isolated contexts and multiple native pages. Video comes from `CefRenderHandler::OnPaint`;
audio comes from `CefAudioHandler::OnAudioStreamPacket`. No screenshot loop, browser extension, or exposed remote-debugging port participates in capture.

The root `suitecut` API uses this backend.

## Runtime modules and local qualification

Production code is organized into compiled `protocol/`, `runtime/`, `page/` and
`process/` modules, with private controller fragments grouped by domain. See
[architecture and ownership](ARCHITECTURE.md) and the
[component build requirements](../../docs/building.md).

The October 21 macOS ARM64 qualification passed the full 90-case
[CEF regression package](../../tests/cef/README.md) with one worker, plus 30
frame handoff repetitions under the original three-second deadline. The local
checks cover navigation-close shutdown, frame metadata ordering and libc++
completion ownership. All 23 module translation units also passed compiler
checks without forced precompiled headers. These results are scoped to macOS
27.0.1; the historical Linux checks below are separate evidence. No CEF CI job
is enabled. See [macOS packaging and signing](../../docs/native-full-runtime-macos.md).

## Historical callback retry build a238fec

Native callback retries are integrated at production `a238fec`, with Runner
`343b915d`, Source `d29461c1` and merge tool `c3889a7e`. All native targets and
strict frontend compilation pass. Eight affected registered checks and four
affected public scenarios pass; the earlier first three public scenarios retain
their separate `14b8e08` positives. Current Source acceptance passes two genuine
browser tests with three ordered retirement/resume fences. This is composed
incremental evidence among 517 registered checks, not a fresh full-suite run.

`expectNative(callback).toPass()` uses native invocation, interval, deadline and
physical-retirement control. Global cancellation now holds cleanup through both
managed invocation and parent callback retirement under the original budget.
Twelve charged/clockless native lifecycle compositions pass optimized and executed
sanitizers. Using toPass in fixtures/all-hooks, nested toPass, callback modifiers and
`expect.poll` remain unsupported; Source fencing remains conservative over the
whole Engine, without concurrent Source isolation or browser-child reuse.

The unchanged SDK has 1,276 passing tests in 163 files, full type checking, and
composed lint evidence (prior full lint plus typed lint of the only changed file).
All 692 emitted outputs and 1,269 SDK quality inputs are unchanged in the final
native-only repair; independent review checks 2,801 unique recorded paths and 230
runtime assets. Network eviction avoids repeated body-metadata serialization in
one admission. No end-to-end latency speedup is inferred from test wall times.

Original failures remain recorded, including timeout315 and historical cleanup
negatives. Automatic snapshot baseline updates and broader cookie-date parsing
remain blocked by automatic review; the custom CEF build remains user-deferred.
Full feature parity is unfinished. Compact evidence:
`.suitecut/diagnostics/native-to-pass-a238fec-result-v1.json`.

Current Source `d29461c1` also passes one authored, unfiltered network attribution
diagnostic: child-frame and main-frame fetch requests each reach the real origin
once with exact frame/context identity and one successful terminal event. A
separate favicon attempt fails with `net::ERR_ABORTED` before reaching the server.
All extra traffic is retained. The five-check scope preserves 2,045 input hashes
and retires all 15 observed process identities. Generic zero command counters
coexist with retained network metadata; actual helper exit supplies the separate
physical evidence. Historical 57-case results and broader redirect/popup coverage
remain unchanged. Evidence: `.suitecut/diagnostics/native-network-attribution-current-a238fec-v1.json`.

## Previous native build 64cc681

The preceding incremental Runner `1a7dafcc` and merge tool `8ac59db7` pass all native
build targets, strict frontend compilation and 29 affected native checks. All 1,229
SDK tests in 159 files, types and lint pass. Native fail-on-flaky policy preserves
actual retry and scheduling outcomes while controlling final run status. Prior
poll/report performance improvements remain integrated. Source `4f8fefac` is
unchanged. Earlier timeout and cleanup negatives remain open; see the active
workboard for exact scopes, measured limits and pending features.

The preceding common Source `4f8fefac` passes matching HAR11 and clock11 browser
checks. Its common native run is 490 pass / 2 fail, with the corrected directory
checker scope passing separately and one output-preparation timeout unresolved.
SDK1,196/type/lint and all native builds pass. This is unsealed evidence; see
the [validation ledger](AUDIT.md) for exact limits and current feature work.

The latest sealed Linux candidate is `0d116d39`: all eight checks pass afresh,
with 423 native checks and 1,133 SDK tests in 150 files. Independent audit verifies
3,096 sealed files and 230 assets, complete before-gate inputs and unchanged SDK
outputs. Matching process-registration5 passes; deadline body/hook cases and a
declared navigation-promise fixture variant pass on exact sealed native/SDK bytes.
Unchanged original soft6 also passes, including its original 40 ms cleanup
mismatch case. Listing29 and attachment mutation8 pass in full CTest. Browser child reuse and
full parity remain unfinished. See [the validation ledger](AUDIT.md) for the
reader/fixture corrections, original negatives and exact runtime boundaries.
Previously sealed `b873192a` retains its scoped URL matcher6/HAR chronology6 and
mutable-array worker reuse4 evidence; these are separate earlier invocations.
Previously sealed `f01357a7`:395/395 native checks pass, with
1,048 SDK tests in 139 files. Strict native frontend/build, diff validation and full CTest are fresh;
four SDK gates/API reuse original passing evidence after exact SDK inputs and all 620
emitted artifacts match. The immutable manifest contains 2,156 hashes plus 230 assets.
The original global-timeout cleanup regressions and two new cleanup scopes pass;
real soft/reuse/fixture layers and HAR MIME/ZIP changes are integrated. Matching
browser proofs remain separately scoped. See [the validation ledger](AUDIT.md).
Previously sealed Linux candidate `4ba1c9d9`: all eight checks pass, with
373 native checks and 986 SDK tests in 133 files. Full native CTest and lint are
fresh; successful SDK and native compilation evidence is inherited after complete
input/artifact equality. The immutable inventory contains 2,075 hashes and 230
runtime assets. Native HAR aggregation/cookies, document/frame debugger binding,
and cached assertion deadline publication are compiled; matching browser checks
are pending. See [the validation ledger](AUDIT.md) for exact proof boundaries.
Previously sealed Linux candidate `9cffe672`: all eight checks pass, with
361 native checks and 984 SDK tests in 133 files. All SDK gates and full native
CTest are fresh; strict native compilation is explicitly reused after exact native
input equality. Its immutable inventory contains 2,046 hashes and 230 assets.
Combined checks include anonymous-file parallel groups and native attachment
append/read declarations. Exact changing-screenshot2 passes; unchanged HAR10 is
6 pass / 4 fail with actual archive ordering and remaining negatives preserved.
See [the validation ledger](AUDIT.md) for proof scopes and pending work.
Previously sealed Linux candidate `b074f3e2`: all346 fresh native checks pass;
SDK964/130 and strict native compilation are transparently inherited from exact
unchanged R2 inputs/artifacts. The manifest records2002 hashes plus 230 runtime
assets. Exact drag5, Page/Context route removal9, script inventory6 and evaluation8
pass cleanly. Declared held-Drop3 proves real callback retention through cancellation
and genuine late-ACK retirement of all16 fields. Original drag19+2skips preserves
its66-byte Fontconfig warning; request387/popup478 and five HAR failures remain
visible. See [the validation ledger](AUDIT.md) for exact evidence and pending work.
The previously sealed Linux candidate is `ff9863a9`: all331 native checks and946 SDK
tests in128 files pass, with1951 recorded hashes plus 230 runtime assets. Matching
recursive JSON9/YAML7, unchanged accessibility79, checkbox/select50, chooser3,
context route removal4 and paused evaluation8 pass with empty native stderr and
complete immutable audits. Two reviewed checker-only description/wait variants
pass5 each; original checker failures remain recorded. See [the validation
ledger](AUDIT.md) for native compile evidence reuse and remaining gaps, including
coordinator emergency cleanup and deferred vendor capabilities.
Previously sealed `fb2068e8` passed320 native and903 SDK tests/121files, with its
own trusted drag5, original drag19+2skips, route8 and breakpoint8 scopes retained.
Previously sealed `97916950` passed 307 native and 877 SDK tests in 117 files;
its matching shared-snapshot publication passed 8/8 and prior ARIA negatives remain
recorded independently.
The previous sealed Linux candidate is `e38ddd29`:294 native checks and845 SDK tests
in114 files pass with all eight fresh gates and1804 hashes plus 230 runtime assets.
Matching locator predicates12, bounding boxes12 and unchanged predicate10 pass,
with empty native stderr, exact immutable audits and confirmed physical retirement.
Matching root debugger10 also passes; post-open page-owner abort needs a
following SDK cleanup fix.
The previous sealed Linux candidate is `31a44e69`:280 native checks and810 SDK tests
in110 files pass, all eight gates are fresh, with1761 recorded hashes plus 230
pinned runtime assets. Exact sealed highlighting12, bounding-box12 and actual
worker coverage9, fullquery110 and unchanged original bounding-box9 pass. The new
request-header wire6 remains1/6; raw receipt diagnostics identify absent post-redirect
ExtraInfo from pinned CEF. A separate held-selection group-transfer followup remains
uncompiled; same-process opaque child dispatch is still under repair.
The previous sealed Linux candidate is `c6fbefa8`:270 native checks and798 SDK tests
in107 files pass, with all eight fresh gates successful and1731 recorded hashes
plus 230 pinned runtime assets. Matching sibling bootstrap4 and unchanged
ownerFrame10 pass. The new highlight checker exposes a real loading-document
selector error after its first9 groups; follow-up repair remains pending.
Previous d9b5f817 has its own separate passing runtime scopes. Matching pristine10, dispatch12, unchanged
original dispatch17 and worker ownership/reattachment/history12 pass. Native
runner attachments, pending-attachment skip and EOF/kill retirement also have
matching sealed process evidence. Matching browser proofs are scope-specific;
see [the validation ledger](AUDIT.md) before treating a capability as verified.
The implementation transfers CPU BGRA or I420 buffers. It does **not** implement GPU
texture sharing, hardware encoding, a desktop UI, or a complete browser-automation API.

For the root API, migration from the beta, and native API usage,
see [the native-default guide](BETA.md).

## Build

For browser media playback, see [codec-enabled CEF builds and actual playback acceptance](CODECS.md).
The official Linux SDK below does not provide H.264/AAC decoding, and no Linux
codec build is planned; use macOS arm64, whose pinned SDK is a codec-enabled build
of the same CEF tag, for H.264, AAC and HEVC playback. Output FFmpeg
encoders and browser input decoders are separate.

Use CEF **156.0.30+g6028375+chromium-156.0.8078.96 stable**, CMake 3.21+, Ninja, and a C++20
compiler. [`cef-build.json`](cef-build.json) pins two minimal binary SDKs by URL,
byte length and SHA-256; verify the archive against that checksum before extracting it:

- Linux x64: the official archive from [CEF's build distribution](https://cef-builds.spotifycdn.com/index.html).
- macOS arm64 (`macosarm64`): the third-party
  [OpenArch codec build](https://github.com/dpfaffenbauer/OpenArch/releases/tag/cef-156.0.30-codecs)
  of the same tag, with H.264, AAC and HEVC playback. It is not an official CEF
  build and carries no codec patent licence; see [CODECS.md](CODECS.md).

Keep SDKs and outputs outside source control (for example under `.suitecut/`).
CMake rejects an SDK whose CEF version differs from this pin.
Builds do not silently fetch or update Chromium. Both 156.0.30 archives are
checksum-verified, and the engine builds and runs against the macOS arm64 SDK.
The Linux build and runtime evidence in [the validation ledger](AUDIT.md) was
measured on the earlier 156.0.7 beta pin. See the ledger for exact scope, upstream failures, capture
measurements, and the blocked whole-browser sanitizer experiment.
Fetch the exact [libyuv Git commit](libyuv-build.json) into `.suitecut/libyuv`.
Gitiles-generated source archives vary in metadata between requests, so their
compressed SHA-256 is not a stable pin. Git verifies the fetched objects; check
that `rev-parse HEAD` matches the pinned commit. libyuv supplies SIMD color
conversion for I420 and is linked statically.

The original CEF parser and string tests have a separate Linux harness:

```sh
python3 scripts/verify-cef-upstream.py \
  --cef-root .suitecut/cef-sdk \
  --cef-wrapper .suitecut/native-build/libcef_dll_wrapper/libcef_dll_wrapper.a
```

It requires Python 3.12+ and CMake, checks pinned source checksums, retains the
original assertions and upstream licenses, and records exact inputs and GoogleTest
results under `.suitecut/cef-upstream/runs/`. This validates CEF URL, JSON and
string behavior; browser-action conformance needs the separate automation suites.

```sh
git init .suitecut/libyuv
git -C .suitecut/libyuv fetch --depth=1 https://chromium.googlesource.com/libyuv/libyuv 7c85a3a0820fab29abb502c207d8b9a394e352cc
git -C .suitecut/libyuv checkout --detach FETCH_HEAD
git -C .suitecut/libyuv rev-parse HEAD
```

The pinned SDK targets this Linux x64 workspace. Build with:

```sh
cmake -S native/browser -B .suitecut/native-build -G Ninja \
  -DCEF_ROOT="$PWD/.suitecut/cef-sdk" \
  -DLIBYUV_ROOT="$PWD/.suitecut/libyuv" \
  -DPROJECT_ARCH=x86_64 -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/native-build --target suitecut-browser --parallel 2
export SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser"
```

On macOS ARM64, use the corresponding macOS SDK of the same CEF version:

```sh
cmake -S native/browser -B .suitecut/native-build -G Ninja \
  -DCEF_ROOT="$PWD/.suitecut/cef-sdk" \
  -DLIBYUV_ROOT="$PWD/.suitecut/libyuv" \
  -DPROJECT_ARCH=arm64 -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/native-build --target suitecut-browser --parallel 1
export SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser.app/Contents/MacOS/suitecut-browser"
```

The CMake bundle includes the CEF framework, sandbox helper apps, CEF license, and
Chromium credits. Keep the complete `.app` together. Local builds use the shared
signing policy, which defaults to ad-hoc signing on an unconfigured host. Pin a
fixed certificate for repeated certificate-backed builds. Distribution requires
separate notarization and platform qualification; local compilation alone does
not establish those gates.

Linux also needs Chromium's runtime dependencies. Set `headless: true` in native
launch options for display-free software rendering; the sandbox stays enabled.
Native HTTP additionally requires libcurl 7.88 or newer and its development
package (`libcurl4-openssl-dev` on Debian), plus libpsl headers and library
(`libpsl-dev` on Debian; `libpsl` on macOS). CMake checks these dependencies;
the independent API cookie policy uses curl's native URL punycode API and the
actual public-suffix list. Associated/page request contexts use the genuine CEF
cookie store. The current source gives independent API contexts a bounded native
cookie jar; matching combined runtime verification is recorded separately in the
validation ledger. Their backing CEF contexts still own actual origin storage.
Native credential generation/import additionally requires OpenSSL 3 Crypto and
its development package (`libssl-dev` on Debian; `openssl@3` on macOS). CMake
checks the version and links the native credential controller to Crypto.

Native HTTP content validation requires zlib and Brotli development libraries
(`zlib1g-dev` and `libbrotli-dev` on Debian; `zlib` and `brotli` on macOS).
CMake links these explicitly. Brotli's encoder is used only by the content
validation component test; the browser uses its decoder. Native validation
sets no memory, decoded-size or encoding-chain limit of its own, and checks the
owning HTTP operation's original deadline. Incomplete HTTP framing retains the actual
libcurl/OS result; incomplete compressed representations produce a native
validation diagnostic without inventing an observed curl error code. A genuinely
successful HTTP transfer carrying zero representation octets remains empty when
supported encoding metadata is present. Nonempty compressed data must reach its
codec's terminal state; a zero Content-Length alone cannot bypass HTTP framing,
header validation, cancellation or the original deadline.

The macOS example requires the
corresponding macOS ARM64 SDK of exactly the same CEF version, with its own
archive checksum; the Linux archive cannot be used on macOS.
Windows configuration deliberately fails until its sandbox and packaging are
implemented. Do not work around platform problems by disabling the sandbox.

## Memory and backpressure

CEF callbacks never wait for pipe writes. The native writer retains one pending
video frame per page, which a newer frame replaces, and queues audio and control
packets whole; a stalled consumer causes video replacement but no audio or event
eviction, so memory grows until the host reads. Commands, results and packet
headers have no size limit of suitecut's own. Intake only waits: while 64
commands are queued or 128 replies await the pipe, and while unread events exceed
65,536 or 64 MiB (an empty backlog accepts any size).

The native publisher retains a queue of genuine timestamped pictures for
causal selection, including held and in-flight images. Picture payloads and
queued PCM have no size limit of suitecut's own. Audio-enabled output defaults to a
120 ms capture buffer and independent 20 ms PCM scheduling; complete video slots
add their interval closure delay. Original startup/stop deadlines and the
`maxWriteLagMs` pipe lag limit stalled encoders. suitecut does not limit the
number of active publishers. CEF
surfaces, FFmpeg memory and separately retained SDK frames are additional.
GPU texture sharing remains unimplemented.

## Native screenshot comparison slice

The ed097480 candidate adds `expectNative(page).toHaveScreenshot(pathOrPNG,
options)` and the locator equivalent. Native C++ owns accepted-paint gates,
consecutive decoded-RGBA stability, comparison retries, the original deadline,
and image/job retirement. Expected baselines must be explicit PNG paths or
Buffers; named runner baselines and update policies remain unfinished.

Comparison uses SuiteCut's normalized Oklab distance with alpha composited over
white, not an equivalence claim for another comparator's threshold or antialias
policy. Both dimensions must agree. When both pixel and ratio limits are given,
the stricter limit applies. Native image admission sets no PNG size, dimension,
pixel or memory limit of suitecut's own; byte sizes must not overflow.
Negation uses the native loop; timeout0 disables ordinary logical expiration.

A changing page can time out without two equal captures. Its diagnostics retain
the last fully admitted pre-deadline actual PNG and the difference from the
explicit baseline, while reporting `stable:false`. Difference generation shares
that capture's bounded native pixel job; timeout starts no additional capture or
diagnostic job. Late CPU/UI completion cannot replace the published pair, and
cancellation retains physical image ownership until the actual UI release. A
difference canvas whose byte size would overflow is skipped.

The d0def75c candidate accepts PNG Buffers through 24KiB chunks and
readonly native file paths. Native preparation owns the original assertion
deadline across configuration, chunks, actual file I/O, capture and comparison;
client nonces permit cleanup after a lost preparation reply. Logical staging,
file/image work and retained input have no count or byte limit of suitecut's own.
Path workers check a genuine regular file, size and identity before/after reading, using
O_NONBLOCK and CLOEXEC. Caller cancellation retires logical work while any real
file/capture callback retains physical ownership until it settles.

Actual public checks pass7 input and8 screenshot groups, including232989-byte
PNG Buffer/path input and130KiB style configuration. Test-only pass-through syscall
gates establish4 public held-file cases and shared2-worker queue/deadline behavior.
The original authored wrong-transport counter check and its corrected checker are
retained separately. See the hashed d0 scope index in AUDIT.md. A following SDK fix
separates public failure publication from queued cleanup while retaining its
canonical staging slot; controlled sender proofs are distinct from actual CEF
admission. Ordinary screenshot/PDF large inline configuration remains separate
work. Named baselines/update policies and offscreen OOPIF full-page capture retain
their documented gaps.

## IPC protocol v1

The executable reads newline-delimited UTF-8 JSON on stdin, with no per-command size limit.
Every command has `version: 1`, a positive integer `id`, and `command`:

```json
{"version":1,"id":1,"command":"navigate","url":"https://example.com"}
{"version":1,"id":2,"command":"devtools","method":"Runtime.evaluate","params":{"expression":"document.title","returnByValue":true}}
```

EOF requests graceful browser shutdown. Stdout is exclusively binary IPC:
four-byte little-endian JSON-header length, header bytes, then exactly `bytes`
payload bytes. All headers include `version`, `type`, and `bytes`.

| Type       | Header fields                                              | Payload                                        |
| ---------- | ---------------------------------------------------------- | ---------------------------------------------- |
| `ready`    | `timestampMs`, `cefVersion`                                | none                                           |
| `frame`    | `timestampMs`, `width`, `height`, `pixelFormat`, `dropped` | tightly packed BGRA8 or contiguous I420 planes |
| `audio`    | `timestampMs`, `frames`                                    | interleaved stereo PCM s16le                   |
| `response` | `id`, `ok`, optional `error`                               | optional JSON DevTools result                  |
| `error`    | `error`                                                    | none; source is no longer reliable             |

Native timestamps are milliseconds from process start. Audio presentation time
is anchored against callback receipt time. Unknown versions and malformed
lengths are fatal; consumers must not attempt to resynchronize a corrupt stream.
Stderr is drained without forwarding page URLs or browser diagnostics into reports.

## Open-source distribution

SuiteCut's source is MIT licensed. CEF and Chromium retain their own licenses and
third-party notices. SDK binaries are not committed.
libyuv retains its BSD-3-Clause license. Keep `CEF-LICENSE.txt`, `CEF-CREDITS.html`,
and `libyuv-LICENSE.txt` in any native binary distribution.
See [AUDIT.md](AUDIT.md) for validation evidence and release limitations.

The HTTP/1 surplus parser examines only raw incoming bytes actually observed
past an agreed Content-Length, with no transfer encoding or bodyless method or
status. The native libcurl framing length/version must agree; folded/ambiguous
headers disable that parser. A real parser failure aborts through curl's native
write/progress callbacks so its connection cannot enter the idle pool. It does
not wait for unseen bytes, log raw messages, or relabel orderly EOF as reset.
Header continuations extend only their actual preceding field; derived cookie
metadata commits complete field values after the real header section ends.

The browser-control integration at `bf480072` built against pinned CEF156 and
passed a bounded live scope for activation, mobile capture restoration, locator
handlers, idle emulation, before-unload and native context video/file lifecycle.
Ordinary MIDI overrides now reject explicitly: that content-setting type is not
registered in this CEF build. The backlog that listed the remaining implementation and verification
boundaries was a Node SDK document; it is on the `archive/node` branch.

Default native builds leave component-check executables out of `all`. Explicit
check targets still work; `-DSUITECUT_BUILD_BROWSER_CHECKS=ON` includes them in
the default build.

The current stock-CEF pass adds audited native handler callback I/O and native
snapshot-reference registrations, permission-scope fixes and native-reported
`browser.capabilities()` metadata. Custom-library candidates remain separately
[isolated](cef-patches/README.md); ordinary builds do not apply or activate them.
The current consolidated build/installation result remains
`.suitecut/diagnostics/native-browser-build.json`; broad E2E/fuzz work is deferred.

The current implementation pass adds version-probed root-target WebSocket
control, native file-mutation receipts/serialization, precise credential-import
identity and retained-coverage interval/source helpers. Backend-dependent
activation remains explicit; see
`docs/internals/cef-issues/README.md`. Compilation is not full runtime parity.

The controller completion adds page/context WebSocket URL callbacks and target
inheritance, WebSocket message staging, production retained-coverage lifecycle
coordination, explicit original-widget mouse receipts, exact clock-document
ownership, deduplicated acknowledged credential synchronization, and handler-driven
recovery of another failed child page. Each optional backend is enabled only by
its actual protocol contract. The stock library remains unchanged. See the current
completion section in the active backlog for backend dependencies and the build
receipt for the installed source revision.

## Installed controller completion build

The native browser and matching browser-only SDK compiled and installed from
`9e7ad4180a259d722c2fd1c0b89ab1e3d4985ffa`. All 976 selected source inputs stayed
unchanged during compilation. The emitted SDK contains 226 files; its 43 runtime
exports and page/context WebSocket APIs import successfully. Dynamic libraries
resolve. Native and SDK build supervisors retired their descendants normally.

The installed binary SHA-256 is
`1bd2b22c7f16656f92ac923b1433aa2f7d96357962476b54aca52553c43653f1`.
The authoritative receipt is `.suitecut/diagnostics/native-browser-build.json`.
This supersedes the earlier installed-build entries above. Custom CEF compilation
and broad E2E/fuzz remain deferred; prepared upstream reports remain unpublished
because the GitHub integration returned HTTP 403.

## Automatic background workers

The engine sizes its shared background worker pool from usable CPU capacity
and effective memory limits. The default CPU share is half the usable capacity,
with at least one worker. A stack-reservation allowance of 1/64 of detected
memory can reduce the worker target further, using the platform's default
thread stack size. At least one worker remains available on tight limits.
These are explicit default policies, not a guarantee of optimal throughput.
There is no fixed four-worker ceiling. No SDK thread-count setting is required.

Linux queries affinity and ancestor cgroup v1/v2 CPU and memory limits. macOS
queries active CPUs, RAM, and pthread defaults. The Windows policy queries CPU
groups/affinity, queryable job CPU/memory limits, physical RAM, and the executable
stack reservation. Unavailable queries fall back to available platform data.
Configured limits are sampled at pool construction; this is not a live resource
pressure controller.

Workers start only when jobs arrive and are reused across subsequent jobs.
Accepted work is never refused for being busy; it queues until a worker is free.
Thus a four-CPU, 16-GiB environment uses two workers, while larger environments
can scale beyond that. Thread creation
failure preserves accepted work on existing workers; failure to create even the
first worker rejects admission. Diagnostics count thread-start failures.
Shutdown drains accepted work and releases captured owners before retiring
physical reservations. Screenshot processing, HAR files, trace previews and
sources, and context video preparation already submit through this shared pool.
Dedicated transport, media, and Chromium threads retain their own lifetimes;
the shared pool limit is not a cap on the complete Chromium process tree.
The stack allowance does not bound task buffers or total browser RSS, and the
queue of accepted jobs has no length limit.

Diagnostics expose `nativeTraceBackgroundWorkerLimit` and
`nativeTraceBackgroundWorkers`, plus `nativeTraceBackgroundCapacity` and
`nativeTraceBackgroundWorkerStartFailures`. The independent-read window example
is optional application-level concurrency and does not control these threads.

The standalone `native/resource-policy` CMake project checks this component
without CEF and can be run locally on Linux, Windows, and macOS. GitHub Actions
are intentionally disabled for this repository; do not add CI workflows.
Resource-policy portability does not implement missing browser backend support: Windows CEF
packaging/sandbox and the native client's Windows transport remain unimplemented.
Linux runtime checks do not establish complete macOS application support.
