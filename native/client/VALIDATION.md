# Native client validation — Linux x86_64, 2026-10-19

The standalone C++20 SDK and C ABI build without Node.js or CEF headers. Release
static/shared builds, downstream C/C++ CMake consumers, and a C-only shared
consumer passed. Installed shared-library C and C++ examples controlled the real
CEF runtime, including a trusted Unicode form submission and a decoded PNG.
The original CEF-only client shared library was about 379 KiB and its static
archive about 625 KiB; these sizes predate the external Chromium/agent additions.
CEF and its resources remain a separate runtime distribution.

## SDK release candidate fuzz gates (2026-10-19)

The current standalone client passes 16 Release and 16 ASan/UBSan/LSan test
targets. Heavy sanitizer campaigns additionally pass 200,000 JSON/packet
iterations and 40,000 observation/diff forests (1,050,139 generated nodes).
The new CLI worker survives 10,000 malformed/truncated/oversized IPC requests:
its descriptor count remains 12 and thread count remains 2; sampled sanitizer
RSS rises from 76,988,416 to 80,138,240 bytes after warmup. The sanitizer
quarantine is explicitly bounded for that retention probe. This finite sample
does not establish leak freedom for arbitrary workloads.

Three CEF-independent engine sanitizer campaigns pass: 1,000,000 command
framing/credit cases, 1,000,000 scheduler iterations (including separate framing,
operation and pause properties), and 30,000 memory-boundary iterations. The
memory campaign covers codecs, multipart failure/release, UTF16 projection,
screenshot leases and 240,000 damage-model paints. Peak charged codec bytes
were 16,822,062 within that campaign's declared limits.

Both emitted TypeScript SDKs pass 200,000 lifecycle operations and 100,000 value
codec cases each, plus routing and packet campaigns with GC/heap checks.
All 1,604 JavaScript tests pass. A separate negative process-lifetime fixture
intentionally creates a SIGTERM-resistant orphan; its exact PID/start identity
and expected SIGKILL match the supervisor receipt. Ordinary test and runtime
checks retain clean, separate cleanup receipts. Normal Firefox utility-process
SIGTERM exits are recorded separately from client errors; all owned descendants
retire without supervisor-issued kills.

Fresh static/shared installed C++ consumers and C ABI consumers control real
CEF, Chrome and Firefox. The shared C ABI also builds from a C-only CMake
project. Native CLI workflows pass against all three browsers in Release and
with client sanitizers, including explicit worker leak checking before `_exit`.
The browser binaries themselves remain uninstrumented.

The npm tarballs pass fresh consumer imports and CLI checks, including all eight
audio package contracts. An installed browser-control tarball passes trusted
form input, resizing and decoded PNG checks on all three browsers; its installed
CEF viewer passes takeover/resolution checks. The installed root tarball records
video with page audio and retires its browser in a display-free smoke test.
Full lint, formatting, type checking and a frozen lockfile are release gates;
the updated production and development dependency audit reports zero advisories.

Artifacts and immutable hashes are under `.suitecut/release-readiness-20261019`.
The prebuilt native candidates target Linux x86_64 with glibc >=2.38 and
GLIBCXX >=3.4.32 for the CLI (>=3.4.31 for the shared library), OpenSSL 3 and ZLIB; they do not bundle those system libraries
or browsers. Source builds remain the option for other compatible Linux
toolchains. Windows/macOS binaries and a custom patched/codec CEF build are not
qualified by this run. No package was published or tagged.

## Native CLI and compact agent interface (2026-10-19)

The Release build and four focused test targets pass: `agent_test`,
`control_cli_test`, `observation_test`, and `simple_agent_test`. These cover
capability discovery and full-driver preservation, literal CLI arguments,
inert-node filtering, password redaction, output budgets, stale aliases,
acknowledgement-only actions, compact deltas, original-error preservation,
coordinate gestures and shared deadlines.

Ten scripted workflow groups pass against actual Chrome 156. They exercise
the legacy JSONL and restricted MCP catalogues, persistent named CLI sessions,
trusted form input/readback, current-ref replacement, search, subtree/frame
observations, explicit advanced-tool selection, double-click and interpolated
drag, private output files, isolated profiles, malformed IPC, concurrent opens,
close/reopen, idle retirement, failed startup and unsafe session paths. These
use rendered local HTTP fixtures without a model or video recording.

Seven additional workflow groups each pass against Firefox/BiDi and the
existing pinned CEF runtime. Both return 41 compact controls, preserve refs
across acknowledgement-only fill/readback, deliver one double-click and all
20 coordinate-drag movements with release, produce an unchanged semantic delta,
and retire the named session. The CEF checks use the installed executable.
Firefox previously ignored `mouse_move`'s step count; its BiDi action sequence
now preserves interpolation using bounded protocol chunks.

On this 41-control Chrome fixture, serialized result bodies used 7445 bytes
for full observation, 5738 bytes for compact, and 2347 bytes for an unchanged
delta with fresh refs (approximately 23% and 68% smaller than full). These are
fixture payload measurements, not AI task-success or general latency claims.
The earlier MiniWoB model-trial results predate this interface change; those
frozen trials were not rerun or modified.

Evidence is under `.suitecut/company-patterns/`: `workflow-results.json`,
`payload-sizes.json`, `firefox-results.json`, `cef-results.json`, and the build
and lifecycle supervisor receipts. Each successful supervisor receipt reports
normal child exit, no timeout, no cleanup failure, and all descendants retired.
The installed prefix is `.suitecut/company-patterns/sdk`, including the C++
catalogue header and optional CLI skill. This step did not rebuild Chromium/CEF
or extend the previous sanitizer/fuzz evidence to the new session code.

## Native browser-control gap closure (2026-10-18)

The native SDK now implements authenticated WS/WSS and HTTP(S) discovery,
Firefox WebDriver BiDi, an encrypted credential vault, static PNG pixel
comparison, and Chromium video recording, tracing and CPU profiling. C++ APIs,
the additive ABI-v1 C connection APIs, and the 53-tool native agent/MCP interface
are integrated without Node. Build dependencies are OpenSSL and ZLIB; recording
finalization additionally uses FFmpeg/ffprobe. CEF remains a separate runtime.

Current finite proofs under `.suitecut/native-gaps/`:

| Proof | Result | Evidence |
| --- | --- | --- |
| Native test groups | 12/12 release and ASan/UBSan with leak detection | `ctest.log`, `asan-ctest.log` |
| PNG decoder/comparison fuzzing | 12,000 mutations plus independent PNG/filter/Adam7 fixtures; sanitizer clean | `image/` |
| Vault encryption, tampering, paths, limits and concurrent writers | All six groups passed, including trusted host URL-origin parsing | `credentials/20261018T192242Z/` |
| TLS wire behavior | 15 cases: verified trust/hostname, mTLS, headers, discovery framing, DNS and raw BiDi | `tls/wire-test.log` |
| Actual Chrome over authenticated TLS | 11 native C++/C ABI checks and 6 caller-ownership checks; natural caller exit | `tls/chrome-run-cabi/` |
| Agent PNG comparison and credential login | 13 release and sanitizer checks; cross-origin redirect receives zero secret text or key events | `guarded-agent-workflows/`, `asan-agent-workflows/` |
| Firefox native browser/agent | 35 release and sanitizer checks, including actual public BiDi dispatch, trusted cross-origin frame input, files, dialogs and contexts | `firefox-public-api/`, `firefox-asan/` |
| Firefox attachment and C ABI | 21 checks; caller DOM/input/viewport preserved across sessions; SDK-owned contexts retired | `firefox-attachment/` |
| Chromium capture | 34 release and sanitizer checks; 1/24/60/120 FPS, MP4/WebM, trace/profile artifacts, bounded backpressure and page-close cleanup | `media-budget-final/`, `media-budget-final-asan/` |
| Installed native stdio MCP | 22 checks, 53 tools, zero protocol noise and sanitized master-key environment in actual Chrome children | `installed-control/` |
| Chromium workflow regression | 62 checks with actual HTTP pages, frames, files, native drag, storage, WebMCP and modal continuations | `chromium-regression/` |
| Pinned CEF regression | 18 checks and 3 context/page cycles | `cef-regression.json` |

Video output uses explicit sample-and-hold resampling. The verified release
120 FPS run encoded 39 frames using 19 source frames, with 20 repeated output
samples; 20 source frames were stored. `ffprobe` confirmed 120/1 cadence.
Sidecars preserve source timestamps and output sample runs. This verifies
output cadence, not 120 unique renders per second. Video is silent. Source work
is bounded by an eight-frame/16 MiB queue, a 256 MiB spool budget and 72,000
output frames. Five repeated recording/page-close cycles verify cleanup without
publishing partial videos. The media worker is created lazily and blocks while
idle; ordinary Chromium control retains its four fixed SDK threads.

The recording flood proof configured only 16 KiB for public event retention,
polled no events during four recordings, and measured 7,037 queued bytes with
84 counted telemetry drops while recording and evaluation remained usable.
The lifecycle and raw-wire rings share that single configured byte budget.
Recorder-owned frames are consumed/acknowledged before optional public retention;
raw user-owned screencasts and critical control events retain explicit overflow
semantics. The fixture drains critical events between later lifecycle scenarios,
rather than expecting unbounded retention in the tiny public buffer.

Firefox proof executables opt into Linux subreaping only within their own test
process. The SDK does not change the embedding application's reaper policy;
it drains only its owned process group when adopted helpers are its children.
Firefox normally terminates some utility helpers with SIGTERM: tests record and
accept normal exit 0 or expected SIGTERM 15, and reject unexpected helper exits.
The caller parent exits normally. BiDi context UUIDs are session-scoped, so
attachment proofs rediscover the caller's actual page and verify its unchanged
DOM token, input and viewport after reconnecting.

Firefox uses content contexts and a controlled blank tab for owned launch.
The earlier 35-check proof establishes same-context drag and trusted frame
input. Directory upload and HTML5 drag data transfer remain unverified by that
receipt. Chromium capture helpers remain backend-specific.

### Shared feature implementation and current verification

Brave and Helium were additionally verified on Linux x86_64 through the native
Chromium adapter using official temporary installs and fresh headless profiles:
Brave 1.97.59 (Chromium 155.0.8059.85) and Helium 0.19.4.1 (Chromium
155.0.8059.84). Each passed 75 shared browser-control checks, 9 installed C/C++
SDK/agent checks, and 60 broader HTTP workflows (144 total). The workflows cover
real frame input, uploads, native HTML5 drag, PDF, cookies/storage, routing,
authentication, console errors, init scripts and downloads. Neither browser
exposed the optional WebMCP API; those assertions were not counted.

The broader workflow caught a shared HAR regression: HTTP status values were
serialized as floating-point JSON, breaking integer access/comparison in the
typed C++ API. CDP and BiDi HAR now retain integer HTTP status codes; the common
workflow asserts that type. The older workflow's media calls were also updated
to the shared `colorScheme` / JSON-null API. Release SDK tests passed 13/13;
the maintained HAR sanitizer test passed 47 checks, and the Firefox sanitizer
regression passed all 75 shared checks after the fix. Evidence and cleanup
receipts are retained in `.suitecut/browser-compat-brave-helium-20261018/`,
including `verification-final.json` and `cleanup.json`.

The new shared SDK work targets **CEF, external Chromium and Firefox**. It does
not add Safari/WebKit or establish universal browser/command compatibility.
The original proofs above predate the following changes, so they are not
evidence that these new paths passed. Current receipts are retained under
`.suitecut/cross-browser-complete/`.

| New proof | Result | Evidence |
| --- | --- | --- |
| CEF expanded common downloads/emulation/HAR | 9 scenarios and 75 checks passed in Release and ASan/UBSan with leak detection | `cef-final/results.json`, `cef-final-asan/results.json`, matching `cef-final-proof.json` / `cef-final-asan-proof.json` |
| Chromium expanded common downloads/emulation/HAR | 9 scenarios and 75 checks passed in Release and ASan/UBSan with leak detection | `chromium-final/results.json`, `chromium-asan-final/results.json`, matching `chromium-final-proof.json` / `chromium-asan-final-proof.json` |
| Firefox expanded common downloads/emulation/HAR | 75 checks across 9 scenarios passed; real cancellation disconnected the slow HTTP transfer; binary/download/HAR bytes matched | `firefox-final/results.json`, adjacent `network.har.json` |
| Firefox expanded workflow lifecycle | Supervisor exit 0, cleanup successful, no timeout/interruption, all descendants retired | `firefox-final-proof.json` |
| Firefox expanded ASan/UBSan workflow | Same 9 scenarios and 75 checks passed with leak detection; no diagnostics, successful cleanup and all descendants retired | `firefox-final-asan-nine/results.json`, `firefox-final-asan-nine-proof.json` |
| Firefox attached native media restoration | 14 checks passed; real CSS rendering, partial/null semantics, exact nondefault caller restore and unchanged caller without system permission | `privileged/restore-results.json`, `privileged/restore-run.json` |
| Maintained BiDi HAR conversion/collector tests | 46 checks passed; includes retirement/restart/recovery, response-only omit and failed POST-body handling | `bidi-har/final-failure-unit-proof.json` |
| SDK test groups | 13/13 passed in Release and ASan/UBSan with leak detection | `ctest-final-release-proof.json`, `ctest-final-asan-proof.json` |
| Installed downstream CMake consumer | 9 actual browser checks each on Chrome and Firefox; successful cleanup | `consumer/chromium/results.json`, `consumer/firefox/results.json`, adjacent named proof files |
| Firefox cross-context/HTML5 drag | Unsupported; positive native-routing attempts failed, so these operations reject before input | `firefox-drag-widget-pointer/results.json`, `firefox-drag-html5-first/results.json`, `firefox-native-widget-pointer/results.json` |

* The shared download surface configures a context directory and provides
  page-owned IDs, native status, cancellation, completed paths and byte-preserving
  save. Firefox uses `browser.setDownloadBehavior` plus real
  `browsingContext.downloadWillBegin`/`downloadEnd` events; privileged cancellation
  selects a unique owning Gecko download and calls its native cancellation API.
* Media options share CEF's partial-update contract: omitted fields retain
  existing overrides, and JSON `null` clears the specified override. Firefox
  media uses actual native browsing-context properties with saved originals for
  restoration. Geolocation, timezone and user-agent string overrides use native
  BiDi; unsupported user-agent platform/Accept-Language options fail explicitly.
  Firefox158 has no per-context contrast override. Its print medium uses light
  color matching; the proof checks print CSS separately from screen/dark CSS.
* HAR 1.2 capture uses backend-native request/response events and bounded body
  collectors. `content:"embed"` and `content:"omit"` are the common options.
  Actual body, redirect and timing coverage must be assessed from the new
  workflow and conversion-test receipts, not the older metadata journals.
  `content:"omit"` omits response bodies while retaining available POST data.
  CEF captures are filtered to the owning page and admit one active archive
  capture per context.
* Context permission grants use native `permissions.setPermission` scoped to
  the user context and HTTP(S) origin. Firefox's store persists grants, so the
  SDK first snapshots the exact original entry/expiry and restores that entry
  on clear or close. Descriptor batches are prevalidated, and retained restore
  data is capped at 32 KiB. An attached browser must permit the privileged
  snapshot; the SDK does not silently leave profile permission changes behind.

Owned Firefox launches enable `-remote-allow-system-access` for privileged
native media/download controls. An attached browser must already have that
permission; the SDK does not restart it or change its profile. Missing permission
is an explicit capability error. Ordinary BiDi control does not require that
permission. Completed local download sources are constrained to their captured
configured directory. Remote browser-host download paths are not SDK-host files.
The implementation uses bounded event observation without an additional thread
or a competing event consumer. The 75-check Firefox proof verifies native
download cancellation, partial/null media semantics, geolocation with a native
context permission grant, timezone and actual user-agent HTTP headers. Its HAR
contains both redirect hops and actual text/binary/POST bytes. Firefox's shared
redirect request ID exposes the final response body; earlier redirect-body
omission is explicit. The separate 14-check attached-media proof verifies exact
caller restoration and a permission failure that leaves the caller unchanged.
All three expanded workflows pass in both Release and ASan/UBSan builds with
leak detection. Each matching supervisor reports successful cleanup, no
timeout/interruption and all descendants retired. Sanitizers cover the SDK and
fixtures, not the external browser binaries. The installed consumer uses only
the exported CMake package and public headers; its separate Chrome and Firefox
runs each pass nine checks with clean supervisor receipts.

Firefox158 retains same-context pointer dragging. Positive frame-to-parent
pointer tests reproduced native input pinned to the source child, including
negative child coordinates rather than a target-parent outcome. Its headless
native drag service was unavailable, and the alternative native
`HeadlessWidget::SynthesizeNativeMouseEvent` route did not provide held
`buttons` state. The exact installed Gecko revision's source and investigation
receipts are retained under `gecko-capture/`; these are failed-positive
experiments, not passing drag coverage. The final adapter rejects cross-context
drag and draggable HTML5 drag-and-drop before dispatching input, preserves
same-context pointer behavior, and does not fabricate DOM drag events.

Credentials use AES-256-GCM, fresh salt/nonce per write and PBKDF2-HMAC-SHA256
with 600,000 iterations. Master passwords come from the host; CLI configuration
removes the password environment variable before launching browser children.
Guarded login fills validate their saved origin inside each private DOM action,
including delayed locator resolution; these fills use native DOM value setters
with JavaScript input/change notifications. Form submission remains trusted
browser input. No saved credential values appear in tool responses.

Sanitizers instrument the native SDK and test executables, not the external
Chrome, Firefox, CEF or FFmpeg binaries. These finite workloads do not establish
universal leak freedom. Supervisor receipts record natural or requested child
retirement and no live descendants after each completed proof. Linux x86_64 is
the validated platform. POSIX process transport and encrypted-vault file handling
still have explicit Windows limitations; bounded hostname DNS is Linux-specific.

## Correctness and review

All seven test groups passed in both release and AddressSanitizer /
UndefinedBehaviorSanitizer builds, with leak detection enabled. Tests cover
strict JSON, fragmented/corrupt packets, bounded allocations, C ABI ownership,
concurrent waits, fake-engine lifecycle, and facade protocol envelopes.

Real browser checks cover navigation, evaluation, Unicode form input, input and
DOM getters, trusted pointer events, checkbox/select/keyboard controls, shadow
DOM, semantic observations and native references, moving-target clicks,
overlay rejection before pressing, screenshots, and isolated contexts/pages.
The matched runs completed 300 context/page cycles in total. The tested native
client's late PSS and FD deltas were zero in both unprofiled repetitions.

Review fixes include final wire-size validation, independent concurrent wait
deadlines, raw engine-error preservation, exception-safe process/request
admission, descriptor cleanup, readiness-cache retirement, screenshot defaults,
and a 4K-compatible media queue. Direct JSON submission removes a redundant
serialization/parsing pass. One I/O thread handles pipes and deadlines; idle
polling is adaptive. No application callback runs on that thread.

## Matched client measurements

Same pinned CEF binary, 1280×720, 60 FPS, on-demand frame transport, deterministic
fixture, sequential C++/Node/Node/C++ execution. Each execution used 30 samples
per small-operation family, six 256 KiB evaluations, and 50 context/page cycles.
These two repetitions establish scoped observations, not universal speedups.

| Measurement | Native C++ | Current Node SDK |
| --- | ---: | ---: |
| Executable launch to ready, median | 187 ms | 483 ms |
| Small evaluation, median | 0.50 ms | 2.02 ms |
| 256 KiB evaluation, median | 7.43 ms | 10.72 ms |
| Locator count, median | 11.92 ms | 12.54 ms |
| Locator click, median | 49.99 ms | 66.72 ms |
| Locator fill, median | 11.33 ms | 19.46 ms |
| Peak client RSS | 3.63 MiB | 140.05 MiB |

Memory is for the application client only. CEF/Chromium memory is excluded.
Matching inputs and observable outcomes do not prove every ancillary completion
guarantee of the wrappers is equivalent. Node's broader convenience API and
callback integration are not all represented by these five operation families.
Total in-application CPU excludes Node SDK import; executable-to-ready includes
that import. A separate resource-profiling run passed both clients, but its
browser PSS sampler omitted failed reads. Browser-total PSS is **unknown**;
zero values in that raw profile must not be interpreted as actual browser use.

## Fuzzing

250,000 deterministic mutation iterations passed under ASan/UBSan/LSan, followed
by 2,000,000 release iterations. Each iteration mutates JSON and a fragmented
binary packet stream; seeds include invalid Unicode, duplicate keys, nesting
limits and occasional 1 MiB payloads. Sanitizer stderr was empty. This is seeded
mutation fuzzing, not a coverage-guided libFuzzer campaign or proof of no leaks.
The separately compiled CEF library was not sanitizer-instrumented.

The 250,000-iteration sanitizer run peaked at 389 MiB versus 10 MiB for the
release fuzz run. A controlled pair of identical 50,000-iteration sanitizer
runs reduced peak RSS from 359 MiB to 50 MiB when the global freed-allocation
quarantine was lowered from 256 MiB to 16 MiB. Both produced identical result
counts, empty sanitizer stderr and three final FDs. This demonstrates a large
sanitizer-retention contribution, rather than equivalent production live
memory. The control pair used the final rebuilt sanitizer binary; it does not
provide an exact subtraction from the earlier heavy run.

Additional actual-pipe tests exercise 600 randomized payloads, 800 concurrent
requests, 1,098 readiness/page-close cycles, abandoned futures, deadlines,
backpressure, critical-event overflow, disposable media eviction, a full 4K
frame, broken stdin, truncated output, profile ownership and process shutdown.
Supervised real-browser workloads and installed examples retired naturally,
without forced cleanup.

## Reproduction and limits

See [build/API instructions](README.md) and [benchmark reproduction](tools/README.md).
Local evidence is under `.suitecut/native-client/`: `verified-summary.json`,
`comparison/`, `resource-profile/assessment.json`, `fuzz/`,
`installed-sdk-results.json`, and supervisor receipts. Exact measured workload
sources were archived before the optional sampler was corrected.

The native client targets POSIX/Linux and also contains external
Chrome/Chromium CDP and Firefox/BiDi launch/attach backends. The original
measurements above use the
SuiteCut CEF engine, not external Chrome. Typed C++ methods cover core agent
controls; the CEF raw command/event API exposes other native engine families.
Advanced CEF callbacks require the corresponding
event polling and continuation commands. Completed response buffers belong to
their caller. Cleanup owns the launched process group and direct child; it does
not claim ownership over processes deliberately escaping that group.

## External Chrome/Chromium integration

On Linux x86_64 with **Chrome/156.0.8078.12**, the final release build passed all
58 supervised core external-browser assertions, including actual modal
continuation, mask pixels/cleanup, animation restoration and long-input-delay
shutdown. The queued 70 ms deadline settled in 70.23 ms; shutdown during a
120-second input delay took 113.11 ms. Captures had the expected
1280×720 viewport, 1280×1840 full page,
120×90 clip and 800×600 resized viewport. These are local workload results for
this runtime/build, not universal Chrome compatibility or performance claims.

All ten client CTest groups passed with the final build. The earlier pipe-launch
revision also passed 24 shared-DOM JavaScript regressions. A consumer compiled as
C11 and linked with the C++ library launched
real Chrome through `sc_client_launch_chromium` and evaluated
`Promise.resolve(6*7)` to `42`.
The final SDK's existing CEF backend passed its 18 real-browser checks and three
context/page cycles against the production CEF binary. Seven additional genuine
CEF JSON-agent checks passed, including shared-context tabs. These runs check both backend paths;
they do not make the earlier CEF benchmark numbers applicable to Chrome.

The earlier pipe-launch build completed 32/32 MiniWoB web-task episodes: a configured
16-family panel at seeds 101 and 202, with a fresh Chrome/profile per episode
and no video recording. These replay recorded UI plans through the native C++
SDK; they are not a fresh AI policy evaluation or the complete MiniWoB suite.
Both repetitions retired all owned processes. Reports, commands, screenshots
and per-task outcomes are under `.suitecut/native-chrome/runs/`.

The Chrome report records zero pending requests and zero dropped events before
the final close regression. Evidence is under
`.suitecut/native-chrome/native-agent-controls-final/results.json`,
`.suitecut/native-chrome/native-agent-cef-final/results.json` and
`.suitecut/native-chrome/native-agent-cef-tools/results.json`, with supervisor receipts
and build/source provenance retained alongside the validation artifacts.

`tests/chromium_real_browser.cpp` is a separate public-API integration executable
for a locally installed Chrome/Chromium. It uses a self-contained data URL,
does not start Node or a fixture server, and writes a JSON assertion report and
an actual viewport PNG. It is not part of the browser-independent CTest suite.
Build and launch instructions are in [the SDK README](README.md).

The executable checks navigation/title, awaited Promises and Unicode, surfaced
JavaScript exceptions and raw CDP metadata, trusted form clicks, labels/roles,
open shadow roots, CSS/nth locators, checkbox/select controls, keyboard chords,
Unicode insertion, observed references and invalidation after replacement or
navigation. It also checks viewport/full-page/clipped PNG dimensions, JPEG/WebP
headers, viewport changes, DevTools frame metadata, separate context globals
and cookie stores, page/context close, ten pending queries, independent queued
deadlines, explicit cancellation, and recovery without replaying retired input.
An actual JavaScript `confirm` remains pending while the application polls its
`Page.javascriptDialogOpening` event and accepts it through a raw DevTools
continuation; the original evaluation must then resolve `true`. The final close
check starts a Shift key press with a 120-second input delay, allows it to
dispatch, and requires shutdown within the configured budget plus one second.
Its pending future must report close/cancellation. The JSON report records
actual PNG dimensions, transport counters before close, and delayed-input close
time; the viewport PNG is saved before its dimension assertion.
Moving-target and hover-created-overlay fixtures check final target validation
and absence of background pointer downs. The expanded source now also checks a
real masked PNG pixel (decoded through the browser's Image/canvas API), mask and
style cleanup, and finite/infinite animation state restoration. Unsupported
generic families and invalid screenshot options fail explicitly. All are
included in the final 58-check report above.

The JSON report and supervisor receipt establish the finite integration result
for the exact browser/build used.
Existing CEF timing, sanitizer, memory and lifecycle results do not establish
external Chrome performance or leak freedom. Most image checks inspect
signatures and PNG dimensions; the mask assertion checks one decoded fixture
pixel and is not a general screenshot comparison. The data URL does
not test HTTP routing, cross-origin frames, downloads, localStorage, service
workers or arbitrary production sites. The cookie check uses isolated browser
context stores without making a network request.

The Chromium adapter uses four fixed SDK background threads: pipe I/O, ordinary
commands, a narrow DevTools continuation lane, and deadlines. Both command lanes
share bounded queue and pending-request budgets. The continuation lane handles
dialog replies, Fetch/Debugger commands, debugger startup and execution
termination while ordinary evaluation/navigation waits; it adds no per-request
threads. Applications use manual event polling and raw CDP continuation;
typed JavaScript-SDK callback parity is not provided. The C ABI adds
`sc_client_launch_chromium` with the unchanged ABI-v1 `sc_launch_options` layout,
and the installed `suitecut-native-chromium` example accepts a browser path and
optional URL.

## Expanded native HTTP and agent workflows

The new `tests/chromium_workflows.cpp` executable embeds a bounded C++ loopback
HTTP server and writes its own result report, PDF, HAR, upload files and saved
download. Its source exercises the native `AgentDriver` and `Agent::run` against
actual browser state, including snapshot/ref actions, confirmed form outcomes,
semantic diffs, async modal tool interleaving, shared-context tabs, isolated
contexts and state transfer, navigation/history/waits, same/cross-origin frame
input, file contents, downloads, PDF, static route fulfillment/abort, request and
response journals/body, headers, Basic authentication, offline mode, console
errors, init scripts and media emulation. Actual HTML5 drag preserved the native
data-transfer token with trusted dragstart/drop events. Single/multiple files,+nested Unicode directory paths and downloaded bytes all matched the fixture.
The extra server thread belongs to the
proof executable, not the SDK's four-thread scheduler.

WebMCP checks run only when actual browser registration, discovery and execution
APIs exist. On the tested Chrome 156 runtime, a separate direct API probe
confirmed `document.modelContext.getTools()` returns real `RegisteredTool`
records, and `executeTool(record, parametersObject)` invokes the selected tool.
The native driver projects metadata to JSON without the cyclic Window property
and reacquires the matching real record on each explicit invocation. Legacy
`listTools`/testing APIs remain supported. The workflow registers an actual page
tool and checks that the driver labels its metadata and result as untrusted.
If the APIs are absent, the report records
`webmcp.available:false` and no WebMCP assertion is counted as passing. No
replacement API or guessed tool list is injected. Runtime diagnostics record
secure-context state and exposed navigator/document/window/prototype APIs.
The direct API probe is `.suitecut/native-chrome/webmcp-registered-tool.jsonl`.
The final integrated HTTP workflow reported `webmcp.available:true` and passed
its actual page-tool discovery/invocation assertions.

The release HTTP workflow passed **62/62 checks** in
`.suitecut/native-chrome/native-agent-http-final/results.json`. The same 62
checks passed under ASan/UBSan with leak detection enabled in
`native-agent-http-asan/results.json`, with no sanitizer diagnostics. Both runs
admitted and received 142 commands, had zero pending requests and dropped events
before close, and retained zero queued event bytes after close while driver and
transport views remained alive. This finite workload does not establish leak
freedom in arbitrary applications or instrument the Chrome binary.

The real native stdio MCP proof passed **39 checks** and discovered **47 tools**,
including trusted Unicode input, asynchronous modal continuation, screenshot
image content, PDF embedded resources, argument validation and malformed-input
recovery. A 5,000-message telemetry flood produced 905 counted drops while
browser control remained usable. Its report is
`.suitecut/native-chrome/native-agent-mcp-final/results.json`; drops are explicit
bounded telemetry behavior, not a claim that every console message is retained.

The real attach proof passed **19 checks** in
`.suitecut/native-chrome/agent-attach-natural/results.json`: C ABI attachment with
default/null options, existing-page/viewport/input preservation, SDK-owned
context cleanup, repeated detachment/reconnection, and caller-owned Chrome's
natural exit with code zero. The SDK refuses new targets in the caller's default
context. Release held keys/buttons before detaching; these checks do not prove
automatic restoration of inputs held by SDK commands at detach time.

The baseline README capability table used agent-browser **0.39.1** as a public
browser-control inventory reference. At that baseline, Firefox/BiDi, remote TLS,
a credential vault and typed Chromium trace/video/pixel helpers were pending.
The gap-closure section above supersedes those items. IndexedDB/service-worker
state, deferred-origin state restoration and framed screenshot masks/animation
freezing remain outside the documented surface; universal command/browser
parity and a bundled AI model/provider ecosystem are not claimed.

## Agent interface improvements (2026-10-18)

The current client adds ordered `Agent::run` actions, compact observations,
stateless observation differences, and locator-scoped observations. Release
verification passes 9/9 client checks; the two new behavioral checks also pass
under ASan/UBSan with leak detection. A bounded seeded observation mutation run
completed 3,000 iterations and 19,857 generated nodes without diagnostics. Its
initial 20,000-iteration attempt timed out and is retained as incomplete.

The matching native engine adds password-safe AI control discovery and stable
same-document element references. Six focused native reference checks and three
live JavaScript SDK reference/frame scripts pass. Large full semantic captures
still have existing bounds; scoped capture can inspect a selected subtree.

Detailed scripted browser, AI web-task, source-hash, and memory evidence lives
in `.suitecut/agent-improvements-bench/` in the development workspace. These are
finite workloads, not an exhaustive browser compatibility or leak-freedom proof.
The C ABI remains raw-command based; the new agent convenience APIs are C++.

## Cua Driver review adaptations

The current release build passes 10/10 client checks. The action and visual
checks also pass ASan/UBSan with leak detection. An actual CEF proof passes
66 assertions, including Unicode readbacks, acknowledged versus confirmed
effects, full-list prevalidation, whole-batch deadlines, cropped-image input
at DPR 1/1.5/2, one-use/cross-controller tokens, navigation, scrolling and
viewport changes. The engine/CEF library is unchanged and not sanitizer
instrumented by this client build.

The native fixture's 12-sample medians were approximately 12.5 ms for an
unverified fill and 16.0 ms with a readback. Screenshot capture measured
128.6 ms, or 138.7 ms with capture identity/geometry checks. A blocked action
under a 100 ms batch budget returned in 100.5 ms. These are local fixture
measurements, not cross-platform Cua performance comparisons or AI task scores.
Evidence and source provenance are in `.suitecut/cua-adaptation/`.

Visual freshness is a client-side check. It cannot detect arbitrary DOM-only
layout shifts or prevent navigation between the final check and input. PNG
dimensions are read from a validated IHDR header without a full image decoder.
Only viewport captures at normal visual zoom are supported by this helper;
the existing raw screenshot API remains available for its other options.

## Bounded performance improvements

The native client now moves evaluated JSON values instead of deep-copying them,
parses and writes ordinary ASCII string spans in batches, and filters unrelated
process groups before opening their `/proc` identity records. The engine drops
the redundant initial screenshot paint wait while keeping font readiness,
root-before-child preparation, final paint, PDF and hook synchronization.

On this four-CPU Linux workspace, the final build measured approximately
0.82 ms to parse a 1 MiB ASCII JSON string (previous runs 2.38–2.50 ms),
100 ms for a plain screenshot (118 ms previously), and 103 ms for a capture
with freshness checks (134 ms previously). Large evaluation results allocated
4.18 MB per operation versus 8.15 MB previously; these are cumulative requested
C++ allocation bytes, not peak browser memory. Instrumented benchmark counters
include the client I/O thread and exclude the CEF processes.

The bounded read example overlaps independent requests without adding threads:
sixteen reads with a 25 ms asynchronous page wait took 106 ms at a window of
four, versus 422 ms serially. This workload demonstrates overlap; it does not
predict CPU-bound JavaScript speedups or permit reordering dependent actions.
Five same-engine close measurements had medians of 77 ms versus 167 ms on a
host with many unrelated process entries, so that gain depends on host state.

The release suite passes 10/10 checks. Four changed-path checks pass ASan/UBSan
with leak detection, including 100,000 seeded JSON/decoder fuzz iterations.
After 100 additional large reads, the final client process retained 24 KiB
more RSS, with unchanged file descriptor and thread counts; this finite check
does not establish leak freedom. CEF is not sanitizer-instrumented here.

Screenshot/PDF pixel checks pass. Child style/font/caret/mask/animation and
cancellation checks pass in a 360×1000 fixture where all children are visible.
The original 360×220 fixture reproduces the documented offscreen OOPIF raster
limitation in both the baseline and updated engines. That pending CEF work is
not fixed by these changes. Tighter FFmpeg filter pools were measured and
rejected after throughput fell; production media thread settings are retained.

Evidence, exact binary hashes, rejected candidates and the final SDK archive
are in `.suitecut/bounded-performance/`. Earlier candidate timing runs used a
binary with experimental media flags; the final confirmation uses the shipped
binary with those flags removed. No FFmpeg subprocess runs in these browser
control timing workloads.
