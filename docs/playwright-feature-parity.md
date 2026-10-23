# Playwright feature matching specification

This is the behavior reference and feature-matching checklist for SuiteCut. It
records supported operations, observable results, and important failure cases;
it does not describe Playwright's implementation. It is not a claim that SuiteCut
already supports these features or a commitment to implement every platform.

## Active scope: browser control

Current implementation work covers the native CEF browser-control capabilities in
**PW-01–PW-31**. **PW-32–PW-42 are outside this task**: assertion/test-runner parity,
fixtures, scheduling, reporting/blob archives, generated tests and UI, component
testing, optional platforms, and agent tooling are not active deliverables.
Native browser debugger sessions and locator suggestion/point-picking APIs remain
in scope even though the reference groups them under PW-37; this exception does
not include a test generator, editor integration, or interactive test UI.

Use the [one-page browser backlog](internals/native-browser-active-backlog.md) for the next
browser work. Existing evidence, unchecked reference items, historical failures,
and deferred vendor decisions below remain unchanged. An unchecked item or an
unrun private handoff is not, by itself, missing implementation; historical runner
results below do not expand the active task.

## Current integrated evidence

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

### Previous native build 64cc681

Native candidate `64cc681`, Runner `1a7dafcc` and merge tool `8ac59db7`, passes
all native build targets, strict frontend compilation, 1,229 SDK tests in 159 files,
type checking and lint. All 29 affected native checks pass once with 3,136 input
hashes unchanged. Independent review separates 124 genuine Runner report artifacts
from 33 controlled DTO fixtures and verifies 612 recorded processes absent,
including zombies. Source `4f8fefac`, all 1,116 compiled dependencies and 230 runtime
assets are unchanged; 680 SDK outputs and 1,954 unique compiled/SDK hashes are pinned.

`--fail-on-flaky-tests` and the strict SDK boolean `failOnFlakyTests` now apply
native final-run policy after actual execution and retirement. Five genuine CLI
scenarios and two merges preserve retries, original expected/flaky statuses,
maxFailures, later/dependent/teardown execution and serial/repeat identities.
Typed JSON policy is derived and validated natively; JUnit uses properties
without synthetic cases, and HTML/console explain the final policy outcome.
The merge tool preserves source policy and supports explicit aggregate policy.

Polling capacity reuse and indexed directory test-row serialization remain
integrated. The preceding paired component workload measures 15.30→0.80ms per
two full report writes at 4,096 tests, with identical bytes; warm descriptor
collection uses zero allocations in the measured actor workloads. These are
scoped observations, not whole-suite latency guarantees.

This is unsealed scoped evidence among 504 registered checks, not a full rerun
or full parity. Original 315 remains unresolved and was not retried. Historical
sanitizer/interrupt cleanup negatives remain, including poll assertions passing
with outer cleanup 125 from two adopted/reaped SIGKILL children of unknown lineage.
Automatic snapshot baseline updates and broader cookie-date parsing remain
pending after automatic review rejection; their inactive foundations are
preserved. The custom CEF build remains explicitly deferred.

Compact evidence: `.suitecut/diagnostics/native-fail-flaky-64cc681-result-v1.json`.

### Previous performance candidate 1c1f769

The preceding performance candidate `1c1f769` passed 27 affected checks. Its
report comparison, descriptor allocation controls and qualified sanitizer result
remain in `.suitecut/diagnostics/native-runner-perf-1c1f769-result-v1.json`.

### Previous incremental 2062c50

The current native build `4ea60ac` with checker `2062c50` passes all native
build targets, 1,213 SDK tests in 158 files, type checking, lint and all 17 affected
native checks. Runner `5d63fbd2` passes 14 repetition and nine parallel-group
process scenarios. Same-repeat tests genuinely overlap within one file using the
original global worker pool. Default/parallel fixture lifetimes, serial-group
retries, cross-file/project barriers, stable public identities and declared
project counts preserve the original deadlines and failure limits.

Indexed repeat planning is integrated: its component measures 98,827 comparisons
for 4,096 declarations; the prior 41,945,088 count is source-derived. This is not
an end-to-end latency measurement. Four existing snapshot callbacks and two
mutable-suffix scenarios also pass. Source `4f8fefac` and all 1,116 compiled
input hashes remain unchanged. Root rechecks 1,947 unique compiled/SDK hashes;
the process supplement verifies 243 known PIDs absent, including zombies.

These are scoped results among 502 registered checks, not a full-suite rerun or
full parity. The earlier output-preparation timeout, unrelated sanitizer cleanup
negatives and interrupted compiler cleanup records remain unresolved. No authored
deadline, original assertion or physical retirement requirement was relaxed.

Compact evidence: `.suitecut/diagnostics/native-parallel-repeat-2062c50-result-v1.json`.

### Previous incremental d48b6b8

The current native build `3f85de7` with checker `d48b6b8` passes all native
build targets, 1,211 SDK tests in 158 files, type checking, lint and 18 affected
native checks. Runner `22721d90` passes all 14 genuine repeatEach scenarios,
including original global budgets, retries, raw zero, project roles and shard
membership. Each repeated actor currently executes its selected tests serially;
parallel test-group activation remains separate private work.

Mutable bounded snapshot suffixes pass two real callback scenarios, including
beforeEach/body/afterEach state; four existing snapshot callbacks also pass.
Source `4f8fefac` and its 1,116 compiled input hashes remain unchanged. The 18
checks are scoped evidence, not a rerun of all 499 registered native checks.
The preceding original timeout and separate sanitizer-cleanup negative remain
unresolved. The first O3 compiler-warning failure is retained alongside its
reviewed explicit-initialization fix; no warning or authored deadline was relaxed.

Compact evidence: `.suitecut/diagnostics/native-repeat-snapshot-d48b6b8-result-v1.json`.

### Previous incremental 9373a3e

The incremental `9373a3e` build passes all native targets, 1,200 SDK tests in
157 files, type checking, lint and seven affected native checks. Runner
`cb3b01bc` includes shared identical snapshot plans, corrected default ARIA paths
and direct descriptor aggregation. Source `4f8fefac` is byte-identical to the
preceding build. Actual emergency fault checks retire cleanly; 705 driver pins
stay unchanged and a subsequent review rechecks 1,934 compiled/SDK hashes.
This is scoped incremental evidence, not a second full 492-check run.

The preceding full run and unresolved timeout below remain unchanged. A separate
unchanged output-directory sanitizer control has an unresolved cleanup negative;
its assertions passed, but three adopted SIGKILL children lacked exact lineage.
That raw result is retained separately from the passing descriptor actor proofs.
Two static-tool attempts also hit custom JavaScript heap caps; those resource
failures remain recorded alongside the successful type/lint invocations.

Compact evidence: `.suitecut/diagnostics/native-snapshot-descriptor-9373a3e-result-v1.json`.

The common Linux build at integration `6ebe3ca` builds all native targets and
passes 1,196 SDK tests in 157 files, type checking and lint. Source `4f8fefac`
and Runner `e909be10` completed one full 492-check native run: **490 passed,
two failed, none skipped**. All 1,204 frozen input hashes stayed unchanged.
The raw supervisor reports cleanup failure for two deliberately killed children;
their exact PID/start identities and SIGKILL wait statuses match the existing
runner-death fault. All recorded processes retired. This is an unsealed build,
not a new all-gates pass or full feature-parity claim.

The directory composition failure expected the wrong diagnostic. Its earlier
native catalog guard correctly rejected retry-time configuration drift. The
checker now requires that exact error; its four-case affected scope passes once
on the same production executables and SDK. Original 490 positives plus this
following result leave **one unresolved timeout scope**, not a fresh 492-pass run.

The timeout occurred during output preparation before fixtures or the author's
`beforeEach` timeout amendment. The original 100 ms deadline remains unchanged.
A single affected-case syscall trace did not reproduce it; that trace used a
different filesystem as well as tracing overhead, so it does not establish a
cause or a fix. The original failure and successful rollback remain recorded.

Matching common Source/SDK HAR11 and clock11 checks pass with unchanged input
hashes, empty stderr and actual process retirement. HAR covers strict legacy
cookie expiry forms, not the full cookie-date grammar. Clock includes a genuine
25-hour runFor jump, not broader page/context clock parity.

See the [active workboard](internals/native-feature-workboard.md) for integrated features,
next implementation owners, measured performance and remaining scope. Earlier
sealed evidence remains in the [native audit](../native/browser/AUDIT.md).

## Reference baseline

| Setting                 | Value                                                                                                                  |
| ----------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| Playwright reference    | [1.64.0](https://github.com/microsoft/playwright/releases/tag/v1.64.0)                                                 |
| Reference commit        | `606ecd8b34bac100db32561b6278ffd76e5eb3cd`                                                                             |
| Evidence reviewed       | 81 public API documentation files; test-title inventory from 513 upstream files                                        |
| Upstream test execution | Selected native adapter runs, including assertions, bindings, dialogs, screenshots, uploads and console; results below |
| SuiteCut browser target | CEF `156.0.7+g47dbc1d+chromium-156.0.8078.17` beta                                                                     |
| Chromium target         | `156.0.8078.17`                                                                                                        |
| Current platform        | Linux x64, minimal binary SDK                                                                                          |
| CEF pin evidence        | Archive size/checksums verified; exact-version header accepted; previous 152 header rejected                           |
| CEF runtime evidence    | Linux build/runtime and decoded 120/1 output verified within the native audit scope; whole-browser sanitizers blocked  |

See the [CEF manifest](../native/browser/cef-build.json),
[native build instructions](../native/browser/README.md), and
[native validation status](../native/browser/AUDIT.md) for current evidence.

Browser, operating-system, and language-binding restrictions apply. Some features
are Chromium-only, experimental, or part of the test runner or developer tools.
Chromium/CEF support does not imply Firefox, WebKit, or platform-integration parity.
Language SDKs can be addressed after the binary contract; this document describes
behavior independently of the eventual SDK syntax.

## How to track a match

The `PW-01` through `PW-42` identifiers are stable feature-family references.
Individual capability checkboxes represent verified matching behavior, not merely
the existence of similarly named methods. Leave a checkbox unchecked until the
behavior and applicable failure cases have evidence on the claimed platform.
Capability checkboxes remain unchecked until individually mapped to evidence.
The matching register below records assessed families and their current gaps;
an unchecked item alone does not mean its behavior is missing from SuiteCut.

Use these statuses in the matching register:

- **Unassessed:** SuiteCut implementation and evidence have not been mapped.
- **Missing:** Assessed; the capability is absent.
- **Partial:** Some behavior exists, with specific gaps recorded.
- **Implemented, unverified:** Behavior is implemented but required runtime evidence is missing.
- **Verified:** Claimed behavior and relevant failure cases pass on the recorded platform.
- **Deferred:** A deliberate scope decision, with a reason recorded.
- **Not applicable:** Outside the selected platform/product contract, with a reason recorded.

For each match, record the capability, public entry point, implementation file,
verification command/result, platform and pinned browser revision, and remaining
gaps. Split a family into separate register rows when capabilities have different
statuses. Raw command access or an inherited browser feature alone does not prove
a supported automation contract. Synthetic capture verification does not prove
live browser throughput.

## PW-01 — Browser selection and lifecycle

- [ ] Chromium, Firefox, and WebKit selection, with explicit engine coverage.
- [ ] Headed and headless execution.
- [ ] Supported installed Chrome and Edge channels.
- [ ] Selecting a browser executable.
- [ ] Launch options for environment, arguments, proxy, downloads, timeout, and slower action playback.
- [ ] Connection to an existing compatible remote browser.
- [ ] Browser-server launch and connection endpoint access.
- [ ] Supported debugging-endpoint attachment to Chromium.
- [ ] Browser identity, version, connection state, and context enumeration.
- [ ] Graceful shutdown, browser-server termination, and disconnection events.
- [ ] Browser sharing through bind/unbind with connection metadata.

**Acceptance behavior:** launch and connection failures report errors; shutdown
terminates affected work; disconnects are observable; processes and temporary
resources receive lifecycle cleanup. CEF engine coverage must be distinguished
from the broader Playwright engine inventory.

## PW-02 — Browser contexts and profiles

- [ ] Multiple isolated contexts within a browser.
- [ ] Page creation and enumeration within a context.
- [ ] Persistent context with a selected profile directory.
- [ ] Context defaults configured before page creation.
- [ ] Context settings applied to existing/future pages where supported.
- [ ] Context closure, closed-state queries, and closure events.
- [ ] Context-wide events, scripts, callbacks, and request handlers.

**Acceptance behavior:** isolated contexts do not share ordinary cookies or origin
storage; pages in one context share applicable state; persistent profiles retain
supported state between launches.

## PW-03 — Page, tab, and popup management

- [ ] Create, enumerate, activate, and close pages.
- [ ] Identify a page's context and popup opener.
- [ ] Observe new pages and popups, including action-triggered popup waits.
- [ ] Track closure and renderer crashes.
- [ ] Read URL, title, viewport, and HTML content.
- [ ] Replace document content.
- [ ] Close with optional before-unload handling.

**Acceptance behavior:** immediate popup navigation remains observable; blocked
popups and closed pages have distinct outcomes; crash/closure terminates pending
operations with meaningful errors.

## PW-04 — Navigation and document readiness

- [ ] URL navigation and relative URLs resolved against a base URL.
- [ ] Reload, backward history, and forward history.
- [ ] Individual frame navigation.
- [ ] URL waits using supported patterns or predicates.
- [ ] Navigation and document-lifecycle waits.
- [ ] Readiness milestones: commit, DOM content loaded, load, and network idle.
- [ ] Navigation-response access where a response exists.
- [ ] Navigation timeout configuration.

**Acceptance behavior:** redirects, same-document navigation, hash changes, and
history API changes are supported. Completed HTTP 404/500 responses are not
automatically navigation exceptions. Invalid URLs, connection/certificate errors,
closure, and timeout can fail navigation. Same-document transitions can return no
new response. Load completion does not guarantee application readiness. Network
idle is available; application assertions provide the stronger readiness contract.

## PW-05 — Frames and iframe navigation

- [ ] Main frame and hierarchy inspection.
- [ ] Frame lookup by name or URL.
- [ ] Attachment, navigation, and detachment events.
- [ ] Parent, children, owning page, and iframe-element access.
- [ ] Page operations scoped to a selected frame.
- [ ] Nested frame locators.
- [ ] Conversion between iframe locator and content-frame locator.
- [ ] Same-origin and cross-origin frame interaction.

**Acceptance behavior:** await late-created frames; fail pending work on detach;
reject ambiguous iframe targets. Upstream tests also cover cross-frame searching,
multi-frame ambiguity, nested frames, stalled siblings, and hidden iframes. Record
explicit compatibility decisions for these cases rather than assuming all
cross-frame operations have the same public contract.

## PW-06 — Element discovery and locators

- [ ] Accessible role, name, and description locators.
- [ ] Text, label, placeholder, alternative text, and title locators.
- [ ] Test IDs and configurable test-ID attribute.
- [ ] CSS, XPath, and registered custom selector engines.
- [ ] Descendant scoping and positive/negative text or descendant filters.
- [ ] Visibility filtering.
- [ ] Locator intersection and union.
- [ ] First, last, indexed selection, counting, and enumeration.
- [ ] Readable representations and human-readable descriptions.
- [ ] Normalization into a resilient user-facing locator.
- [ ] Highlighting and removal of highlights.
- [ ] `within(parentLocator)` with relative selection evaluated per parent.

**Acceptance behavior:** locators resolve against the current document when used;
single-element operations normally require one match and reject ambiguity.
Supported locators traverse open shadow roots; closed roots are not generally
traversable, and XPath does not pierce shadow roots. Immediate collection methods
do not all wait for dynamic content. Selecting the third cell within every row
must produce third-column cells, not the third cell in the whole table.

## PW-07 — Automatic waiting and actionability

- [ ] Required actionability conditions retried until success or timeout.
- [ ] Element resolution retried after document changes where appropriate.
- [ ] Operation timeout configuration.
- [ ] Trial actionability without executing the action.
- [ ] Forced actions with documented checks bypassed.
- [ ] Locator handlers for unexpected overlays.

| Operation                                             | Relevant checks                           |
| ----------------------------------------------------- | ----------------------------------------- |
| Click, double-click, check, uncheck, set checked, tap | Visible, stable, receives events, enabled |
| Hover, drag to another element                        | Visible, stable, receives events          |
| Fill, clear                                           | Visible, enabled, editable                |
| Select option                                         | Visible, enabled                          |
| Element screenshot                                    | Visible, stable                           |
| Select text                                           | Visible                                   |
| Scroll into view                                      | Stable                                    |

**Acceptance behavior:** visibility depends on rendered bounds and CSS visibility;
`opacity: 0` can still count as visible. Stability requires sufficiently unchanged
geometry across animation frames. Receiving events requires an unblocked action
point. Editable and enabled are distinct. Moving, covered, disabled, detached,
and replaced elements receive consistent behavior and useful failure diagnostics.
Do not apply every table check to every input method.

## PW-08 — Mouse, pointer, touch, and scrolling

- [ ] Pointer movement and intermediate positions.
- [ ] Button down/up, click, and double-click.
- [ ] Button, modifiers, click count, position, and delay options.
- [ ] Element hover.
- [ ] Wheel scrolling and scroll-into-view.
- [ ] Touch tapping in an appropriately configured context.

**Acceptance behavior:** coordinates, modifiers, button state, click counts, and
event order are observable. Wheel input does not promise scrolling has finished.
The basic desktop touchscreen API exposes tapping, not arbitrary multi-touch
gestures.

## PW-09 — Keyboard and text input

- [ ] Key down/up, named keys, shortcuts, and modifier combinations.
- [ ] Platform-aware modifier aliases.
- [ ] Typing with optional per-character delay and sequential locator key presses.
- [ ] Direct text insertion.
- [ ] Focus, blur, and input-value inspection.
- [ ] Filling, clearing, and selecting editable text.

**Acceptance behavior:** fill, typing, and direct insertion have different event
sequences. Modifiers persist until released. Repeated key down can produce repeat
behavior. Supported Unicode/non-Latin input works; unsupported/non-editable
targets fail appropriately.

## PW-10 — Form controls

- [ ] Fill text inputs, textareas, and contenteditable elements.
- [ ] Clear editable values.
- [ ] Check/uncheck and set checkbox or radio state.
- [ ] Select single/multiple options by value, label, index, or element.
- [ ] Read selected values and control state.
- [ ] Interact through associated labels.
- [ ] Observe submission and resulting navigation.

**Acceptance behavior:** an already-correct checkbox need not be toggled;
unsupported input types, disabled controls, and missing options produce the
appropriate failure or wait.

## PW-11 — Uploads, file choosers, and drag-and-drop

- [ ] Assign input files from paths or in-memory contents.
- [ ] Multiple-file and supported directory upload.
- [ ] Clear selected files.
- [ ] Observe/control file choosers and inspect multiple-file capability.
- [ ] Drag a page element onto another.
- [ ] Drop external files, text, URLs, or other MIME data onto a locator.

**Acceptance behavior:** names, MIME types, contents, input/change events,
multiple-file restrictions, cancellation, and rejected drops are observable. An
external drop target that does not accept the drag reports failure.

## PW-12 — DOM inspection and element state

- [ ] Text content, rendered text, inner HTML, attributes, and values.
- [ ] Bounding boxes.
- [ ] Checked, enabled, disabled, editable, visible, and hidden state.
- [ ] Count and inspect matches.
- [ ] Evaluation against one/all matched elements.
- [ ] Attachment, detachment, visible, and hidden waits.
- [ ] Fixed element references through handles.

**Acceptance behavior:** missing elements, detached handles, hidden geometry, and
frame-relative coordinates have defined outcomes. Immediate state queries differ
from retrying assertions.

## PW-13 — JavaScript evaluation and handles

- [ ] Evaluate expressions/functions in pages, frames, workers, and handles.
- [ ] Pass supported serialized values and object handles.
- [ ] Await promises and return serialized results.
- [ ] Retain JavaScript handles, read properties, convert to element handles, and dispose.
- [ ] Expose application functions and bindings.
- [ ] Initialization scripts before page scripts.
- [ ] Add scripts and stylesheets.
- [ ] Explicitly expose callbacks in supported JavaScript evaluation/init-script arguments.

**Acceptance behavior:** page errors, rejected promises, unsupported serialization,
disposed handles, destroyed contexts, and navigation during evaluation have
defined outcomes. Preserve the documented lifetime of exposed callbacks.

## PW-14 — Events, waits, cancellation, and errors

- [ ] Browser disconnect; context/page creation, closure, and page crash events.
- [ ] Frame lifecycle, console, page errors, and dialog events.
- [ ] Download, file chooser, request/response, request completion/failure events.
- [ ] WebSocket, worker, service-worker, and document-readiness events.
- [ ] Persistent/one-shot listeners and removal.
- [ ] Predicate waits and action/event coordination.
- [ ] Default/per-operation timeouts, including supported zero timeout.
- [ ] JavaScript AbortSignal cancellation on supported operations.
- [ ] Listener-removal policies for active asynchronous callbacks.

**Acceptance behavior:** timeout, cancellation, closure, crash, and operation
failure remain distinguishable. Pending work must not hang after its target is
gone.

## PW-15 — JavaScript dialogs

- [ ] Alert, confirm, prompt, and before-unload dialogs.
- [ ] Type, message, default value, and owning page inspection.
- [ ] Accept, dismiss, and prompt-text submission.
- [ ] Dialog-closure observation.

**Acceptance behavior:** dialogs auto-dismiss without a handler; a registered
handler must resolve the dialog to let affected activity continue. Before-unload
has additional page-close behavior.

## PW-16 — Downloads

- [ ] Download observation, URL, and suggested filename.
- [ ] Completion waits and save to chosen destination.
- [ ] Local completed path where available.
- [ ] Read stream where supported by the binding.
- [ ] Cancel, delete, and failure inspection.
- [ ] Accept-download configuration.

**Acceptance behavior:** interrupted/canceled downloads, failed responses, page
closure, context cleanup, and remote-path restrictions have defined outcomes.

## PW-17 — Network observation

- [ ] Request URL, method, resource type, and navigation status.
- [ ] Headers, including repeated-header-preserving APIs.
- [ ] Text, binary, and supported structured request bodies.
- [ ] Redirect relationships and frame/service-worker attribution.
- [ ] Request timing, transfer sizes, and failure details.
- [ ] Response status/text, URL, success, headers, body/text/JSON, and completion.
- [ ] Service-worker origin and available server address, HTTP version, and security details.

**Acceptance behavior:** redirects, cache, compressed content, failed requests,
repeated headers, and body availability are consistent. HTTP error responses are
not transport failures.

## PW-18 — Request interception and mocking

- [ ] URL-pattern or predicate matching.
- [ ] Page/context routes and invocation limits.
- [ ] Continue unchanged or override supported URL/method/headers/body.
- [ ] Abort with a selected failure reason.
- [ ] Fulfill with a synthetic response.
- [ ] Fetch and modify a response before fulfillment.
- [ ] Fall back to another handler.
- [ ] Remove individual/all routes with active-callback policies.

**Acceptance behavior:** page routes take precedence over context routes;
matching-handler order is defined. Service workers affect visibility to ordinary
routes. Routing affects HTTP caching. A popup's first request can require a
context route.

## PW-19 — HAR recording and replay

- [ ] Record/filter HTTP archives.
- [ ] Include, attach, or omit content; save ordinary/zipped archives.
- [ ] Full/reduced detail and explicit start/stop.
- [ ] Replay and update archives.
- [ ] Abort or use live networking for absent entries.

**Acceptance behavior:** matching considers methods, bodies, redirects, and
relevant headers. Cover multipart matching, repeated cookies, archive variants,
and route removal.

## PW-20 — WebSockets

- [ ] URL, lifecycle, text/binary traffic, errors, and closure observation.
- [ ] Socket-event/frame waits.
- [ ] Intercept matching connections and mock a server.
- [ ] Connect to the real server.
- [ ] Forward, modify, suppress, and inject messages.
- [ ] Supported connection-protocol inspection.

**Acceptance behavior:** direction, binary payloads, close codes/reasons, and
cleanup are preserved through supported operations.

## PW-21 — Standalone HTTP/API requests

- [ ] Browser-associated and independent request contexts.
- [ ] GET, HEAD, POST, PUT, PATCH, DELETE, and general fetch.
- [ ] Query parameters and headers.
- [ ] Binary/text/JSON/form/multipart bodies and form-data construction.
- [ ] Authentication, proxies, TLS settings, and supported client certificates.
- [ ] Redirect limits and supported transport retries.
- [ ] Timeout/cancellation and optional failure on unsuccessful HTTP status.
- [ ] Cookie inspection/insertion/clearing and storage-state export.
- [ ] Response body/text/JSON, headers, status, timing, and connection metadata.
- [ ] Response/context disposal and API-request tracing.

**Acceptance behavior:** browser-associated requests share applicable context
cookies; independent contexts have independent cookie state. Retry options do not
imply retrying every HTTP error.

## PW-22 — Cookies, storage, and saved authentication

- [ ] Add, inspect, and clear cookies, including URL and clearing filters.
- [ ] Expiry, secure, HTTP-only, same-site, and supported partition attributes.
- [ ] Current-origin local/session storage inspection and modification.
- [ ] Authentication-state export and restore.
- [ ] Optional IndexedDB, origin-private filesystem state, and virtual credentials.
- [ ] Context state replacement through setStorageState.

**Acceptance behavior:** cookies follow domain/path/security/expiry/partition
rules; local storage is origin-scoped; session storage has different sharing and
lifetime rules and is not in ordinary storage-state export. Replacement clears
covered existing state before applying the new state. Saved authentication does
not represent every browser-profile data type.

## PW-23 — Authentication, proxies, certificates, and permissions

- [ ] HTTP authentication credentials.
- [ ] Per-origin credential entries, fallback, matching, and precedence.
- [ ] Configurable API-request credential sending.
- [ ] Authenticated proxies and bypass rules.
- [ ] Supported client certificates and HTTPS-error handling.
- [ ] Grant/clear permissions, including origin scoping.
- [ ] Configurable service-worker allowance.

**Acceptance behavior:** proxy and website authentication are distinct;
unsupported permissions fail appropriately; certificate/credential scope is
respected. Permission availability varies by engine and platform.

## PW-24 — Device and environment emulation

- [ ] Viewport, screen dimensions, and device scale factor.
- [ ] Device presets, supported mobile behavior, and touch capability.
- [ ] User agent, locale, and timezone.
- [ ] Geolocation and online/offline state.
- [ ] Extra HTTP headers.
- [ ] Light/dark color scheme, reduced motion, forced colors, and contrast preference.
- [ ] Screen/print CSS media.

**Acceptance behavior:** layout, browser-visible values, media queries,
permissions, and events reflect supported settings. Emulation does not reproduce
all physical-device behavior.

## PW-25 — Clock and deterministic time control

- [ ] Install a controlled clock.
- [ ] Fixed date/time and system-time settings.
- [ ] Pause at a selected time and resume.
- [ ] Advance time while executing scheduled callbacks.
- [ ] Fast-forward with defined repeated-timer behavior.
- [ ] Supported timers, animation frames, idle callbacks, date, and performance control.

**Acceptance behavior:** fixed date does not necessarily stop timers; changing
system time does not execute all pending timers. Running time and fast-forwarding
have different callback behavior. Control is shared across the relevant context.
Cover timer order, intervals, callback errors, popups, and supported newer
time-related browser APIs.

## PW-26 — Workers and service workers

- [ ] Dedicated-worker discovery, creation/closure events, and URLs.
- [ ] Worker evaluation and handles.
- [ ] Supported worker-console observation.
- [ ] Supported service-worker discovery.
- [ ] Allow/block service workers.
- [ ] Supported service-worker network attribution.

**Acceptance behavior:** termination invalidates affected work; navigation can
replace workers; service-worker networking differs from ordinary page requests.
Record engine-specific restrictions.

## PW-27 — Accessibility inspection

- [ ] Role, accessible-name, and accessible-description locators.
- [ ] Accessible names, descriptions, and error-message inspection.
- [ ] Page/locator accessibility snapshots and structural comparison.
- [ ] Supported YAML and JavaScript JSON snapshots.
- [ ] Snapshot depth limits and supported bounding boxes.
- [ ] AI snapshots with element references.
- [ ] getByRef resolution and reference normalization into durable locators.

**Acceptance behavior:** references correspond to the latest snapshot in the
relevant frame and are not permanent IDs. AI snapshots can include iframe
content. Role/name/state/hidden-element handling follows supported accessibility
rules. These operations are not a complete accessibility compliance audit.

## PW-28 — Screenshots and visual comparison

- [ ] Page/element, viewport/full-page, and clipped capture.
- [ ] PNG, JPEG, and WebP with applicable quality options.
- [ ] Supported transparency and CSS/device-pixel scaling.
- [ ] Element masks and mask color.
- [ ] Animation handling, caret hiding, and temporary styles.
- [ ] File output or returned image data.
- [ ] Named/automatically named screenshot baselines.
- [ ] Page/locator screenshot assertions.
- [ ] Pixel thresholds and maximum difference count/ratio.
- [ ] Actual/expected/difference artifacts and baseline updates.

**Acceptance behavior:** cover fonts, changing layout, scrolling, high DPI, masks,
frame content, and stabilization. Baselines can vary by browser, OS, fonts, and
rendering environment.

## PW-29 — PDF output

- [ ] Supported Chromium PDF data and file output.
- [ ] Paper format/custom dimensions, orientation, margins, and scale.
- [ ] Page ranges and background printing.
- [ ] Header/footer templates and CSS page-size preference.
- [ ] Supported tagging and document-outline options.
- [ ] Print media by default, with screen media selectable before capture.

**Acceptance behavior:** pagination, sizes, templates, backgrounds, and printing
colors follow documented options. PDF output is not universal across engines.

## PW-30 — Video recording and screencast

- [ ] Page video recording, dimensions, and 1.64 recording FPS configuration.
- [ ] Video access, save, and deletion.
- [ ] Screencast start/stop and disposal.
- [ ] Image callbacks with timestamps and viewport dimensions.
- [ ] Simultaneous file recording and frame callbacks.
- [ ] Screencast image quality and restart after stopping.
- [ ] HTML overlays, chapters, titles, and descriptions.
- [ ] Action highlights, pointer decorations, duration, placement, and styling.
- [ ] Show/hide overlays/actions and runner test-information overlays.

**Acceptance behavior:** recording defaults to 25 FPS. Output FPS does not
guarantee distinct browser frames. Firefox/WebKit currently capture up to 25
distinct frames per second. Existing capture can affect requested dimensions.
Invalid FPS fails. Saving/completion follows recording lifecycle. Cover closure,
restart, active-recording conflicts, callback backpressure, and finalization.

SuiteCut's page audio, independent capture/output cadence, 120 FPS contract,
mixing, and RTMP publication require separate native acceptance criteria below;
they are not established by Playwright video matching.

## PW-31 — Tracing, diagnostics, and coverage

- [ ] Trace start/stop and artifact saving.
- [ ] Supported action/network/screenshot/document capture.
- [ ] Trace chunks and operation groups.
- [ ] Standalone API-request tracing and trace inspection.
- [ ] Runner-managed assertion tracing.
- [ ] Console type/text/arguments/location/timestamp/attribution.
- [ ] Page errors and context-wide web errors.
- [ ] Bounded console/error/request histories and supported clearing.
- [ ] Supported garbage-collection requests.
- [ ] Chromium JavaScript and CSS coverage start/stop/results.

**Acceptance behavior:** context tracing does not automatically capture test
assertions; runner-managed tracing can. Coverage is not uniform across engines.
Bounded histories are not guaranteed full-session logs.

## PW-32 — Assertions

### Retrying element assertions

- [ ] Attached/detached; checked/unchecked/indeterminate.
- [ ] Enabled/disabled, editable, empty, focused, visible/hidden, and in viewport.
- [ ] Element count and exact/contained text.
- [ ] Class membership/value, attribute presence/value, CSS, and ID.
- [ ] JavaScript properties, input values, and selected values.
- [ ] Role, accessible name/description/error message.
- [ ] Accessibility snapshot and screenshot assertions.

### Page and response assertions

- [ ] Page title, URL, screenshot, and accessibility snapshot.
- [ ] Successful API response.

### General assertion facilities

- [ ] Equality, strict equality, numeric comparisons, and approximate equality.
- [ ] Truthiness, null, undefined, defined, type, and instance checks.
- [ ] String/regular-expression matching, collection membership, and length.
- [ ] Object properties and partial matching.
- [ ] Thrown errors and promise resolution/rejection.
- [ ] Negation and asymmetric matchers.
- [ ] Soft assertions, custom messages/matchers, and configured instances.
- [ ] Polling and retrying a block with toPass.
- [ ] Supported text/binary/image snapshots.

**Acceptance behavior:** retrying assertions reread state until success/timeout;
generic assertions evaluate the supplied value. Soft failures accumulate while
execution continues. Preserve negation, diagnostics, and matcher-specific
timeout semantics.

## PW-33 — Test definition, fixtures, and hooks

- [ ] Named tests, nested groups, focused tests/groups, and parameterization.
- [ ] Skip, fixme, expected-failure, slow, and conditional annotations.
- [ ] Tags and custom metadata.
- [ ] Before/after test and group hooks.
- [ ] Test/worker-scoped and automatic fixtures.
- [ ] Fixture dependencies, extension, overrides, and option fixtures.
- [ ] Full fixture-timeout parity. Native setup/teardown clocks, inherited and explicit-zero declarations, and acknowledged fixture-local amendments are integrated; remaining upstream contracts are tracked in the register.
- [ ] Built-in browser, context, page, request, and component-mount fixtures.

**Acceptance behavior:** dependencies determine setup/cleanup; teardown runs after
applicable failures; fixture errors identify the fixture. Default browser fixtures
provide test isolation.

## PW-34 — Scheduling, projects, retries, and timeouts

- [ ] Named projects with browsers/devices/configuration/metadata.
- [ ] Project dependencies and teardown projects.
- [ ] Non-default projects selectable explicitly or through dependencies.
- [ ] Parallel workers, fully parallel execution, ordered/serial groups.
- [ ] Sharding, repetition, and failed-test retries.
- [ ] Immediate/isolated retry scheduling.
- [ ] Seeded shuffle and named resource locks.
- [ ] Test/action/navigation/assertion/fixture/hook/global timeouts.
- [ ] Failure limits and failure on focused/flaky tests.
- [ ] Path/title/tag/project/location/test-list filtering and supported changed-file selection.
- [ ] Optional context reuse.
- [ ] Immediate current-test failure and stop through test.abort.

**Acceptance behavior:** serial groups have group-level failure/retry semantics;
isolated retries run after ordinary tests, one at a time. Shuffle preserves
required group order. Shared locks prevent concurrency across files/projects in
one run; lock waiting does not count toward test timeout. Locks do not promise
cross-machine synchronization. Context reuse reduces isolation. test.abort stops
the current test, not necessarily the entire run.

## PW-35 — Steps, results, and artifacts

- [ ] Named/nested steps, return values, timeout, and skipping.
- [ ] Boxed error attribution and custom source locations.
- [ ] Step metadata and attachments.
- [ ] File/in-memory test attachments.
- [ ] Unique output paths and snapshot naming templates.
- [ ] Status/expected status/errors/duration/retry/worker/project/tag/annotation access.
- [ ] Screenshot/video/trace retention policies.

**Acceptance behavior:** parallel tests have distinct artifact locations; retries
have identifiable results; timeout/failure retains configured diagnostics.

## PW-36 — Reporting and runner integrations

- [ ] List, line, dot, HTML, JSON, JUnit, GitHub, blob, and Perfetto reporters.
- [ ] Custom run/test/step lifecycle callbacks.
- [ ] Standard output/error, global errors, and final exit handling.
- [ ] Result metadata and attachments.
- [ ] Run preprocessing and supported selection/sharding adjustments.
- [ ] Web-server startup/readiness/reuse and global setup/teardown.
- [ ] Report merging and artifact preservation.
- [ ] Watch/interactive execution and test listing without execution.
- [ ] Snapshot updates and CI exit behavior.

**Acceptance behavior:** reporter lifecycle, error propagation, attachment
fidelity, merged results, readiness failures, and exit statuses remain observable.

## PW-37 — Debugging, inspection, and generated tests

- [ ] Interactive browser inspection and action recording into tests.
- [ ] Locator picking/generation and supported language-specific code generation.
- [ ] Pause/resume/step/run-to-location and paused-state/source details.
- [ ] Interactive test UI, trace inspection, and editor integrations.
- [ ] Failed-test debugging with retained artifacts.
- [ ] Agent-assisted exploration/test planning.
- [ ] Scenario-based test generation and failed-test investigation/repair.
- [ ] Supported natural-language action-generation workflows.

**Acceptance behavior:** these are developer tools with additional integration
requirements; they are not automatically available as ordinary page operations.
Generated tests and repairs still require review and verification.

## PW-38 — Component testing

- [ ] Individual component-story testing in a real browser.
- [ ] Story mounting, supported props, and updates.
- [ ] Component-scoped locators.
- [ ] Real layout/input/accessibility/network and visual assertions.
- [ ] Normal runner retries/parallelism/fixtures/tracing.
- [ ] Framework-independent testing with a compatible application story gallery.

**Acceptance behavior:** unknown stories/render failures reject mounting; updates
can preserve component state; teardown removes the component. Older experimental
React/Vue component packages are not the current 1.64 model.

## PW-39 — Extensions and specialized browser attachment

- [ ] Appropriate persistent Chromium context for extension testing.
- [ ] Extension loading and supported pages/service-worker discovery.
- [ ] Extension-page testing and worker startup/suspension behavior.
- [ ] Documented WebView2 attachment.
- [ ] Supported Selenium Grid connection.

**Acceptance behavior:** record product/browser/environment restrictions.
These capabilities do not imply universal desktop application automation.

## PW-40 — Experimental Electron automation

- [ ] Application launch with arguments/environment and process metadata.
- [ ] Supported main-process evaluation and handles.
- [ ] Window enumeration and first/new-window waits.
- [ ] Renderer-window page automation and corresponding browser-window access.
- [ ] Lifecycle events and application close.

**Acceptance behavior:** launch errors, window creation/closure, and application
termination propagate to affected operations. Treat this as an explicit optional
platform decision.

## PW-41 — Experimental Android automation

- [ ] Device discovery/connection/identity and connection-server launch.
- [ ] Android native UI selectors.
- [ ] Supported tap/long-tap/drag/swipe/fling/pinch/fill/press/scroll/wait actions.
- [ ] Screenshots, APK installation, file push, and shell commands.
- [ ] Supported device sockets.
- [ ] Browser launch and WebView discovery.
- [ ] Supported browser/WebView page automation.

**Acceptance behavior:** Android is a separate experimental platform capability,
not a consequence of desktop Chromium support.

## PW-42 — MCP browser tooling and WebMCP

### Playwright MCP and browser CLI tooling

- [ ] Sessions/tabs/navigation/accessibility snapshots/references.
- [ ] Click/type/form/keyboard/pointer/drag operations.
- [ ] Dialogs, uploads/downloads, evaluation, and supported code execution.
- [ ] Console/network inspection, mocks, and offline state.
- [ ] Cookies/storage, screenshot/PDF, video/tracing, and device emulation.
- [ ] Test discovery/execution/debugging.
- [ ] Persistent/isolated sessions and remote connections.
- [ ] Configured output and lifecycle handling.

Availability depends on configuration and enabled tool capabilities.

### Page-provided WebMCP tools

- [ ] List frame-registered tools and names/descriptions/schemas/annotations.
- [ ] Call a named tool with structured input and return structured results.
- [ ] Access tools independently in nested/cross-origin frames.

**Acceptance behavior:** names are frame-scoped; navigation clears previous
registrations. Unknown tools and thrown exceptions reject calls; isError results
return as results; unsettled calls can time out. Chromium/Firefox require
experimental feature enablement; WebKit does not implement WebMCP. Page-provided
schemas, annotations, and results are not independent guarantees about a tool.

## Matching register

The initial register was unassessed. The entries below now include the first
Linux native implementation and execution evidence. A family is verified only after
its selected scope, individual capabilities, and acceptance cases have evidence.
Use additional rows for mixed statuses; record deferred/out-of-scope decisions
explicitly rather than silently dropping capabilities.

| ID    | Feature family                   | Product layer                    | SuiteCut status | Entry point / implementation                                    | Verification / gaps                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| ----- | -------------------------------- | -------------------------------- | --------------- | --------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PW-01 | Browser lifecycle                | Browser                          | Partial         | [Native launch](../src/native-browser.ts)                       | Pinned Linux build, headless capture, crash/close verified; other engines are separate scope.                                                                                                                                                                                                                                                                                                                                                                   |
| PW-02 | Contexts and profiles | Browser | Partial | [Native contexts](../src/native-context.ts) | Actual isolated/persistent contexts, first-author settings, profiles, storage, authentication and lifecycle have scoped evidence. Sealed72 verifies independent strict/ignored TLS policies and native bootstrap; d9 verifies permissions8, first-author lifecycle20 and real cross-origin storage access/clear/regrant. Initial opaque storage permission and first-author popup policy still need vendor capabilities. Privileged navigation has a startup-history gap. |
| PW-03 | Pages and popups                 | Browser                          | Partial         | [Native page management](../native/browser/context_controls.cc) | Real new pages and window.open popups, opener identity, distinct per-page native frame delivery, page/context close, default-page closure and continued process reuse pass. Broader popup navigation/action coverage remains open.                                                                                                                                                                                                                              |
| PW-04 | Navigation | Browser | Partial | [Native navigation](../src/native-page.ts) | Frame navigation, response/readiness waits and click-triggered commit barriers pass scoped checks. Real history entries, same-document navigation and HTTP 404 handling pass on a23425aa; page closure cancels owned navigation waits. Broader redirect/error coverage remains open. |
| PW-05 | Frames                           | Browser                          | Partial         | [Main frame](../src/native-page.ts)                             | Main/child identities, same-process and OOPIF contexts, process swaps, hierarchy, events, detach cancellation and DOM adoption verified. Frame evaluation 11/13; two private-driver inspection cases remain unsupported. Complete unchanged W3C numeric/element frame-switching files pass 17/17 on 3ed39576. Same-process/OOPIF input and transformed/nested iframe input pass live and unchanged upstream cases; broader element actions remain open.                                                                                              |
| PW-06 | Locators | Browser | Partial | [Native queries](../native/browser/dom_query.h) | Native CSS/text/role/test-ID, custom-world selectors and retained-root handles have scoped evidence. Earlier unchanged selectors70/evaluation100 and strict/query27/getter52 all pass. ff9863a9 strict Locator.waitFor uses genuine native selected-node diagnostics; four original authored groups pass, with a fifth checker cancellation-shape error preserved. Reviewed checker-only wait5 passes on the same binary/SDK, including real held physical receipts and exact owner abort reason. Broader locator-family coverage remains open. |
| PW-07 | Actionability | Browser | Partial | [C++ actions](../native/browser/page/dom.cc) | C++ owns visibility/stability/enabled/editable checks, scroll/hit tests, retries, original deadlines and input settlement. Click/hover/fill, frame transforms and trusted input have scoped live evidence. Broader action variants and complete coverage remain open. |
| PW-08 | Pointer and touch | Browser | Partial | [Native input](../native/browser/page/input.cc) | Native pointer, modifier, force/trial, trusted drag/drop and cancellation checks pass. Native touch passes five live groups on 0157afb8, including trusted events, navigation and physical ownership after logical timeout. Current unchanged pointer file on67:89 passed,0 failed,1 original skip, with all1199+230 inputs unchanged. 811 adds hover8 and original mouse/handle-misc26 passing with empty stderr. 811 original handle click14 was12pass/2fail; e402 repairs both and passes14/14, plus scrolling9/9 and authored Text7/selectText8/action-isolation8 with empty stderr. Broader touch/action coverage remains open. |
| PW-09 | Keyboard and text                | Browser                          | Partial         | [Native input](../src/native-page.ts)                           | C++ owns keyboard state, chords, Unicode typing, input queues, delays and cancellation. Live trusted-event/cancellation checks pass. All 50 active unchanged upstream keyboard cases pass; 3 upstream skips remain. Full feature-family coverage is still partial.                                                                                                                                                                                              |
| PW-10 | Form controls | Browser | Partial | [Native forms](../src/native-form.ts) | All46 unchanged fill cases pass in their recorded scope. Exact sealed ff9863a9 passes unchanged checkbox17/select-option33 (50/50), no overlays, skips or flakes, empty native stderr and immutable audits. Page/Frame optional strictness and native option handling are included. Genuine empty file-selection reset remains vendor-pending; an empty CDP ACK alone does not clear a populated input. |
| PW-11 | Upload and drag/drop | Browser | Partial | [Native upload and drag](../src/native-drag.ts) | b074f3e2 trusted drag5 and declared held-real-Drop3 pass. Actual callbacks survive logical cancel; real late ACKs retire all16 fields, one trusted drop/dragend and later click are observed. Original drag21 is19pass/2existing skips with the66-byte Fontconfig strict negative retained. Prior ff chooser lifetime3 and three timeout constructor-fixture cases pass. Genuine directory upload/populated-file reset and original-widget delivery retain the deferred vendor capability; no fake FileList or widget receipt. |
| PW-12 | DOM inspection                   | Browser                          | Partial         | [Native DOM inspection](../src/native-page.ts)                  | Strict queries, exact state/text/value getters and genuine main/scrolled/same-process/OOPIF geometry pass. Sealed46 adds composed native visibility, fixed-element boolean states and visible/hidden/stable/enabled/disabled/editable waits under original deadlines and physical cleanup ACKs; complete unchanged state/visibility files pass23/23 on the same sealed build. Locator.evaluateHandle preserves genuine File handles. Broader inspection remains open. |
| PW-13 | Evaluation and handles | Browser | Partial | [Native evaluation](../src/native-page.ts) | Unchanged page evaluation passes 100/100 on 0157afb8, including 100 MiB values; six handle files pass 34/34 on 52121865 with genuine DOM-preview formatting. Five actual main-handle UTF16 groups also pass on 32d9a3d0. Typed UTF16 values, genuine handles, bindings and physical callback ownership pass scoped checks. Broader serialization policies remain open. |
| PW-14 | Events and cancellation          | Browser                          | Partial         | [Native events](../src/native-browser.ts)                       | Native predicates preserve original deadlines/cursors and retain physical callback capacity. Sealed35035 adds genuine child-renderer crash listeners/waits, asynchronous predicates and cleanup before deferred visible user-listener errors; private actual publisher teardown/root survival proof passes, combined35035 live proofs pass. Initial-root crash remains helper-fatal; child reload recovery is included in d9; ca7 passes all five strict-stderr recovery/crash groups and full settings across two crashes, repairing the earlier loader diagnostics. The separate recovered-popup first-author locale gap still needs vendor ordering capability. Page/context timeout and listener ordering have scoped live evidence. Broader events remain open. |
| PW-15 | Dialogs                          | Browser                          | Partial         | [Native dialogs](../native/browser/dialog_controls.h)           | Genuine CEF alert/confirm/prompt, Unicode accept/dismiss, automatic handling and100 confirm cycles pass. All13 unchanged dialog cases pass after closed-event support. Beforeunload and broader predicate/error cases still require complete coverage.                                                                                                                                                                                                          |
| PW-16 | Downloads | Browser | Partial | [Download policy](../native/browser/controllers/artifacts/download_controller.inc) | Fifteen primary download groups, 30 popup checks and 10 shutdown repeats pass on a23425aa. The targeted close-cancellation case and all six downloadsPath cases, including real persistent profiles, pass unchanged 7/7 on 7b7b0567. Earlier full download file: 36 passed, 7 failed, 1 original skip; remaining route cases need a full rerun. |
| PW-17 | Network observation | Browser | Partial | [Native network](../native/browser/network_controls.h) | Sealed b4ec7e89 passes actual HTTP/TLS/HAR metadata6, preflight6, image6 and private-collector5 with empty native stderr and1562+230 unchanged inputs. Shared native projection covers event and navigation responses. Complete unchanged request/response57 has48 assertion passes, one original expected Chromium failure and8 unexpected failures,0 skips; response363/374 pass. Remaining request-event attribution is under investigation. Worker interception and broader observation remain open. |
| PW-18 | Request routing | Browser | Partial | [Native routing](../src/native-network.ts) | b074f3e2 exact Page unrouteAll wait/ignoreErrors5 and Context4 pass with real callback selection, original deadlines, Apply ACK retention and physical closure; all inventories unchanged/native stderr empty. Prior Context routes6/live service-worker2 remain scoped. Sealed b873192a passes all six URL matcher groups (RegExp and synchronous predicates) and six chronological generic/HAR route groups with strict empty stderr and full immutable audits. Async predicates, original CORS parent-before-Fetch ordering and Context HAR unrouteAll remain unfinished. |
| PW-19 | HAR | Browser / artifacts | Partial | [Native HAR recording/replay](../native/browser/har_recording.cc) | Exact sealed 4ba1c9d9 verifies all six original cookie cases, receipt-time expiry, and native gzip bodySize 581/compression 174419 against 175000 decoded and 581 actual served bytes. Unchanged HAR10 remains 6 pass/4 fail at real favicon indices, with archive order retained. MIME and ZIP member naming repairs are integrated in f01357a7, whose matching unchanged HAR10 still retains6pass/4favicon order/cardinality failures and original cookie6 passes. Sealed b873192a passes native generic/HAR chronological replay6 with immutable archive pins and real read/Fetch receipts. Context HAR unrouteAll and broader HAR coverage remain open. |
| PW-20 | WebSockets | Browser | Partial | [Native socket observation](../native/browser/websocket_controls.h) | Seven actual public API groups pass on repaired 6073210f with empty native stderr, including event delivery before native wait completion; earlier 0ede1709 6/7 evidence preserves the ordering defect. Coverage includes: real UTF8/binary wire payloads, canonical creation identity/listener order, original native timeout/cancel cleanup, closure and lazy subscription. Matching interception, mock/server routing, message modification and predicates remain open. |
| PW-21 | API requests | HTTP API | Partial | [Native HTTP](../native/browser/http_controls.cc) | Actual native contexts support TLS/proxies/authentication, custom DNS, stream multipart, native deadlines/retries and typed transport errors within recorded limits. Latest complete unchanged API228 on69d33955 with localhost is224 pass/4 fail/0 skip; all31 cookie originals pass. Host-only562, empty-encoded538, constructor-origins499, browser-default387 and upload-diagnostic748 recover with no new failures. Official jar6/regex3 passed onbd6; exact69 native explicit CA4 and official origin7 pass with real TLS/restore receipts. Remaining850 Node TLS environment,1394 remote CDP,360 branded UA and736 Node socket diagnostics remain explicit. Capacity/error proofs and prior failures retain their original epoch scope. |
| PW-22 | Storage and authentication state | Browser | Partial | [Native storage and credentials](../src/native-credentials.ts) | Public cookies/localStorage/IndexedDB/OPFS restoration and UTF16 pass scoped checks. Sealed72 preserves real cookie partition site/ancestor metadata and passes eight actual HTTPS delivery, isolation, export/restore, deletion and strict TLS groups. Genuine WebAuthn signatures/counters pass private proofs; integrated9789b861 passes eight originals, storage22/23 retains the favicon event failure. Concurrent ceremonies require the deferred vendor hook. |
| PW-23 | Authentication and permissions | Browser | Partial | [Native context controls](../native/browser/context_controls.cc) | Actual integrated HTTP challenge/authentication8, authenticated proxy and per-context strict/ignored TLS scopes pass. Native permission grant/clear is context scoped; geolocation permission and cross-origin permissions-policy behavior pass private live checks. Additional permission names and broader certificate/client-certificate behavior remain unverified. |
| PW-24 | Emulation | Browser | Partial | [Native media emulation](../native/browser/controllers/media/media_emulation.inc) | CSS media emulation passes real main/same-process/OOPIF before-author checks and null resets. Sealed72 context defaults include locale/timezone/UA/device metrics/geolocation/permissions; dynamic geolocation11 plus20 creation/update overlaps pass with real main/OOPIF/future-page/null behavior. Native touch is available. Broader emulation parity remains open. |
| PW-25 | Clock | Browser | Partial | [Native virtual-time budgets](../native/browser/virtual_time.h) | Current-document native runFor supports safe positive integer budgets and two-digit-hour duration strings; matching Source4f8 official11 passes, including a genuine25-hour timer jump, original deadlines/cancellation and physical pause acknowledgments. Advance retains its one-day budget cap. RAF/idle pause, system rebase/fixed Date, resume/fastForward, context/OOPIF/worker propagation and broader navigation behavior remain unsupported or incomplete. |
| PW-26 | Workers | Browser | Partial | [Native workers](../native/browser/controllers/workers/worker_controller.inc) | Dedicated worker evaluation, console, handles and lifecycle have scoped evidence; the unchanged worker file passes23 with one original Chromium skip on c734e7c4. Context-owned shared/service worker evaluation, console and handles pass real broker proofs. Exact69 pre-ready observation2, context-network4 and worker-coverage9 pass, including actual body delivery after initiating-page closure and real helper-exit receipts. Worker interception and live same-target reattachment remain open. |
| PW-27 | Accessibility | Browser | Partial | [Native accessibility](../native/browser/dom_semantic_snapshot.h) | ff9863a9 recursive JSON9/YAML7 and unchanged accessibility79 pass with empty stderr/full1951+230 audits; the prior recursive cancellation mismatch is repaired. Earlier original JSON full9/11 retains opaque reference spelling differences. Reviewed description5 checker variant passes all public matcher/count assertions while explicitly recording Chromium rawAX equal-title omission; original checker failure stays preserved. No general DOM-description/rawAX equivalence claim. |
| PW-28 | Screenshots | Browser / assertions | Partial | [Native screenshots](../native/browser/artifact_controls.h) | Selected unchanged screenshots pass67/67 on their recorded candidate. Exactbd6 passes official Input7/Screenshot8/HeldFS4/joint1 and large screenshot/PDF configuration9. Large Page/locator/fixed-handle styles and masks, matcher constructor abort after real append ACK, original deadlines, held font replies and restoration are verified. Offscreen OOPIF fullpage raster and broader convergence policies remain open. |
| PW-29 | PDF | Browser | Partial | [Native PDF](../src/native-artifacts.ts) | Real pagination/text, headers/footers, tags, CSS A5 and page ranges pass scoped proofs; selected unchanged print cases pass2/2 on their recorded candidate. Exactbd6 official large-configuration9 includes genuine long header/footer bytes, independent PDF text decoding and constructor abort after real append ACK. Other print/platform variations remain unverified. |
| PW-30 | Video and screencast             | Browser / media                  | Partial         | [Native media](../native/browser/README.md)                     | Native C++ capture, CFR resampling, PCM/Opus and lifecycle have actual Source evidence at120/59.94/29.97 FPS. Sealed72 passes original three-rate continuity/pulse checks without CPU throttling, Source11 and child-crash isolation2. Loaded runs retain genuine zero PCM at CEF ingress; unchanged six-way capture controls fail under quota throttling and pass in a quiet window. Explicit buffering, latency, held frames and stop tails remain disclosed. Full screencast/narration/rendering parity remains incomplete. |
| PW-31 | Tracing and coverage | Browser / runner artifacts | Partial | [Native tracing](../native/browser/trace_controls.h) | Exact d9b5f817 matching document coverage9, OOPIF7, renderer recovery2 and anonymous-source identity2 pass with strict empty stderr and complete1704+230 audits. Native factory exclusion uses actual FunctionLocation script identity; identical authored source remains covered. Earlier unchanged tracing43 on ca7 had37pass/3fail/3originalskips; unchanged coverage17 had16pass/1fail. resetOnNavigation:false remains explicitly unsupported: actual pinned backend probes lose final old JS calls and old CSS usage across navigation; a custom-backend repair is pending. Broader tracing retention, runner integration and original-suite reruns remain unfinished. |
| PW-32 | Assertions | Assertions | Partial | [Native assertions](../src/native-expect.ts) | Selected unchanged assertion files pass 481/481 on 02026b1e; the URL case passes 24/24 unchanged repeats on that build. Authored ARIA assertions have scoped fixture evidence. Native explicit-baseline screenshot convergence already uses actual decoded RGBA stability, original deadlines, bounded CPU jobs and physical capture/restoration fences; changing-page timeout diff artifacts and soft assertions remain open. Native callback toPass is integrated at a238fec for body/beforeEach/afterEach and real steps, with original deadlines, real raw/attachment/Source retirement, ordinary-failure retries and hard infrastructure vetoes. Eight affected checks and four affected public scenarios pass; earlier three retain separate positives. Whole-Engine Source fencing and unsupported callback modifiers/nesting/fixtures remain explicit limits; expect.poll and broader snapshot/runner coverage remain unfinished. |
| PW-33 | Fixtures and hooks | Runner | Partial | [Native runner](../native/runner/README.md) | The public Linux runner owns explicit fixture DAGs, test/worker/automatic fixture continuations, literal nested suites, inherited hooks and built-in native browser/context/page/request fixtures. Runtime beforeAll skip and native expectedStatus/annotations are compiled in5d9. Unchanged skip3/hooks311/hooks576 and modifiers568 pass on matching5d9 runner/SDK;576 retains actual cleanup timeout/errors/SIGKILL. Sealed f01357a7 integrates genuine native fixture override layers and passes all11 layer scopes in full395 CTest. Sealed b873192a adds literal/options with all9 actual scopes in full418 CTest. Native per-fixture clocks are integrated: explicit fixtures have independent setup/teardown slots; ordinary fixtures use native charged/default budgets. Native timeout/signal/amendment handling preserves original/global deadlines and raw ownership, with an authored ten-scope public checker. Full upstream fixture/hook parity remains partial. |
| PW-34 | Scheduling and retries | Runner | Partial | [Native scheduler](../native/runner/native_runner.cc) | Native scheduling, listing, filtering, retries and bounded directory cohorts have scoped evidence. Runner1a7d passes 14 repetition and nine group scenarios, including genuine same-repeat intra-file overlap in the original global pool, shared original budgets, parallel/default fixture lifetimes, serial-group retries and cross-file/project barriers. Root/project/primary-CLI counts, raw zero, repeat-qualified identity, expanded-before-shard selection and declared project sidecars pass. Native fail-on-flaky final policy passes five genuine CLI scenarios, preserving retries, dependencies, serial outcomes and failure budgets. Resource-lock and wider scheduling parity remain partial; browser child reuse is disabled. |
| PW-35 | Steps and artifacts | Runner | Partial | [Native TestInfo and reports](../native/runner/README.md) | Native steps and bounded mutable attachments preserve author references and committed history through actual retirement. Per-project output roots/retention and corrected checker composition have scoped evidence. Runner1a7d passes explicit snapshot plans, conditional common-plan sharing, default ARIA paths and per-attempt mutable suffixes: four existing and two new callback scenarios. Automatic baseline naming/update modes are the next dependent native Source/Runner batch; broader assertion parity remains pending. Original failures and scoped later positives remain in the ledger. |
| PW-36 | Reporting | Runner / tools | Partial | [Native JSON/JUnit](../native/runner/runner_report.h) | Native JSON/JUnit/HTML preserve genuine attempts, source-qualified identity, errors, artifact availability and publication receipts; standalone merge preserves original evidence. Terminal JSON/quiet and committed-file streaming have scoped checks. Runner1a7d passes repeat/group-qualified membership, exact selected/unstarted logical coverage and typed group/configuration metadata, including zero and project roles. Directory test-row serialization preserves exact output while removing repeated catalog and selection scans. Typed native flaky-run policy validates actual counts; JUnit properties preserve authored case counts and merging preserves source/aggregate policy. Custom live reporters and the full lifecycle remain incomplete. |
| PW-37 | Debugging and generation | Developer tools | Partial | [Native root debugger](../native/browser/developer-debugger.md) | Actual root pause/resume/step, source inventory, exact-ID breakpoints and paused evaluation have earlier passing b074 evidence. Activated 4ba document/frame binding passes inventory5/6, evaluation5/8 and frame1/8 before real cleanup failures; frame SIGSEGV is preserved. The native shared-state repair passes controller46/includedSource7 optimized+SAN and all22 unchanged debugger groups on development browser f01357a7; helper exits0/strictstderr0, but independent audit finds6 unreaped CEF descendants. Sealed f01357a7 subsequently passes all 395 native checks with the separate global-cleanup repair; the earlier debugger proof retains its explicit development provenance and descendant failure. Worker debugger metadata remains inactive; UI/generation unfinished. |
| PW-38 | Component testing                | Runner / application integration | Unassessed      | —                                                               | —                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| PW-39 | Extensions and attachment        | Optional integration             | Unassessed      | —                                                               | —                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| PW-40 | Electron                         | Optional experimental platform   | Unassessed      | —                                                               | —                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| PW-41 | Android                          | Optional experimental platform   | Unassessed      | —                                                               | —                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| PW-42 | MCP and WebMCP                   | Agent / browser integration      | Unassessed      | —                                                               | —                                                                                                                                                                                                                                                                                                                                                                                                                                                               |

### Current verification boundary

Current sealed Linux candidate is `4ba1c9d9e70a3e0d7ef51bcc7ce6b8eba563300a6bc35e50eaa87dda6f2171a8`.
The complete check set passes: **373/373 native checks and 986/986 SDK tests in 133 files**.
Its immutable manifest contains 2,075 hashes and 230 pinned runtime assets; the
independent audit verifies every entry. R3 runs lint, diff validation, native
configure/build and full CTest afresh. SDK build/typecheck/tests and the public
export audit are inherited from successful R2 invocations after exact complete
source and emitted artifact comparisons. Strict native frontend and code generation
are inherited from successful R2 compilation after all 734 native input hashes
match. The actual original evidence is bundled in the sealed epoch.

This candidate activates native primary-channel HAR byte aggregation, cookie
attributes and receipt-time expiry, root document adoption, frame debugger bindings,
and publication of a genuine previously observed assertion mismatch at its original
deadline. Exact matching cookie6 passes all six cases; gzip proves 175,000 decoded
bytes, 581 encoded bytes/bodySize and 174,419 compression bytes against actual wire
data. HAR10 remains 6 pass / 4 fail at genuine favicon indices. Debugger inventory
passes 5/6, evaluation 5/8 and frame controls 1/8; frame cleanup has an actual
SIGSEGV. All inputs remain unchanged. No complete debugger success is claimed;
12 defunct adopted descendants remain from those failed standalone scopes.

Separately pinned development pairs using this exact browser pass all six original
soft assertion groups and one real HTTP response assertion group. Their runner and
SDK are following implementations, not this sealed runner. Runtime evidence is
indexed in `.suitecut/diagnostics/native-4ba1c9d9-runtime-index-v1.json` (SHA256
`1153cc30bdede1e73140a8c8be1360daf82c64d4c97938b1d994bcac6839174a`).
The reentrant cleanup repair, real worker reuse, soft assertions, fixture layers
and subsequent HAR MIME/ZIP changes remain in the following integration branch.

R1 preserves the original included-source harness compile failure and strict JSDoc
warning. The repaired harness retains its original 11 controls and adds two exact
cleanup/retention cases. R2 preserves lint's OS-killed exit 137 during heavy native
code generation. R3 runs lint only after that actual compilation closes; no source
change or assertion weakening is used to obtain a passing result.

Build report: `.suitecut/diagnostics/native-4ba1c9d9-build-report.json` (SHA256
`cc51c06e027811aff6841c13781ee0701ad27be5e38479c26d7690fe8e528af3`).
Manifest SHA256: `b3e67c68c4163d2b0854d8ebbc51d253b5450a1123a67f2c475d24312c27e9d9`.
Independent inventory SHA256:
`d91d5341593e44362b1051be68ebe412eae295595e951b31da0868bef5a44852`.
Full feature parity and the user-deferred custom CEF build remain unfinished.

### Previous 9cffe672 verification boundary

Previously sealed Linux candidate is `9cffe672a149165df228f6596a858103d8759c99ee076d1694904504992f314a`.
All eight checks pass: **361/361 native checks and 984/984 SDK tests in 133 files**.
The immutable manifest records 2,046 hashes plus 230 pinned runtime assets; an
independent audit verified every entry. R2 executes all five SDK gates and the
complete native configure/build/CTest afresh. Strict native compilation is
explicitly reused from R1 after every native C++/CMake input matches exactly.

Combined CTest verifies anonymous-file parallel groups, the existing named-project
paths, native attachment append/read declarations and real output/file/worker
retirement. The append slice accepts authored references separately from actual
published files; it does not claim complete mutable-array behavior. Worker reuse,
soft assertions, cookie attributes/expiry and debugger document/frame adoption are
following changes and are not capabilities of this sealed candidate.

The original R1 result of 360/361 is preserved. One older ownership checker read a
five-field marker after O_CREAT but before the actual newline-terminated write.
Six affected Python readers now wait for complete records, retaining every native
outcome, deadline, inode and process assertion. The focused five-case checker and
complete 361-case rerun pass. Evidence is retained at
`/tmp/suitecut-post-b074-r1-attachment-marker-failure-v1/manifest.json`.

Exact sealed changing-screenshot checks pass both scopes with genuine actual/diff
PNGs, 15 capture samples, original timeout, empty stderr and physical closure.
Unchanged original HAR10 is 6 pass / 4 fail with empty native stderr; all four
remaining assertions encounter genuine favicon entries at the expected index.
The later genuine binary POST also retains a MIME/text projection difference.
No archive entries are filtered or reordered. A declared raw-packet diagnostic
observes gzip decoded body 175,000 bytes and encoded body 581 bytes, matching the
actual server payload. Its first parser failure is preserved; the positive is a
separately hashed offline analysis, not a repeated browser run. Native encoded
compression aggregation remains following work. The unchanged popup response-finished
case fails at its original 30-second deadline with diagnostics disabled and empty
native stderr. Its timing-positive diagnostic run remains separately scoped; it
does not establish a terminal-producer repair.

Build report: `.suitecut/diagnostics/native-9cffe672-build-report.json` (SHA256
`e81d10014d94eada0902b49c591d86fa1a20e45fa5b82e345df9e007ed1ca614`).
Manifest SHA256: `ee130510bc0a34336a8fa06e7b51853efdd41e61e1ba7c30adf0bd5f0558de97`.
Matching runtime proofs are indexed in `.suitecut/diagnostics/native-9cffe672-runtime-index-v1.json`
(SHA256 `196eca2fd073b70837c52b3a93bc91bac75fe8296429e375396a771fd4df169c`).
They include 28 runner cases, two changing-screenshot scopes, HAR10, the preserved
gzip parser failure and separate offline analysis, and both popup runs. The
user-deferred custom CEF build and complete feature parity remain unfinished.

### Previous b074f3e2 verification boundary

Previously sealed Linux candidate is `b074f3e2de5b57a152761c02b8d27d4280ded6adc7100718702981d75a39e9ac`.
Complete fresh native CTest passes **346/346**. SDK evidence is **964/964 tests in
130 files**; the five SDK gates and strict native compilation are explicitly
inherited from R2 after complete input and emitted-artifact hash comparisons.
R3 configure/build and the complete native test run execute afresh. The immutable
manifest records2002 hashes plus 230 pinned runtime assets; an independent full
audit found no mismatches. Native runner SHA256 is
`41f9cadd8fb3fc6dbb48a11f7428112fa4bd83be169230c45819ff0069c8726f`.

Matching browser scopes pass trusted drag5, Page/Context route removal9, debugger
script inventory6 and paused evaluation8 with empty native stderr, full inventories
unchanged and actual helper closure. The separate declared held-Drop checker passes3:
logical cancellation preserves the actual record/callback until genuine late Drop
and disable ACKs settle all16 fields0/false; it observes one trusted drop/dragend and
a healthy later click. This does not prove vendor original-widget delivery. Original
keyboard microtask119 and Alt-modifier72 also pass unchanged; no keyboard fix was
needed. Original drag21 is19pass/2existing skips, preserving its66-byte Fontconfig
strict-stderr failure.

Combined CTest includes declared-project test-level parallel groups/public14,
coordinator emergency fault8 and causal boxed attempt error/public17 checks. Native
C++ retains original budgets, actual worker/file/descendant receipts and raw author
errors while reporting causal step locations. Anonymous file groups, attachment
append/read ingestion and frame debugger adoption are following work. Startup cache
and frame ownership primitives remain inactive until their exact native adapters
are integrated; they do not claim bootstrap activation or child/worker debugging.

Current unchanged request387/popup478 is 0 pass / 2 fail. A later declared
method-entry observer proves request387 selected a genuine favicon request; that
run does not demonstrate missing headers for the intended author request. A
declared SDK936 development combination still fails original478: the real popup
and initial document request/response arrive, but neither agent emits a genuine
Network completion event. The finished wait remains pending until actual cleanup.
No page lifecycle event substitutes for that missing terminal. A separate protocol-logging diagnostic observes both primary/private
agents receiving complete ExtraInfo for the same actual author request; it proves
capability, not a missing-primary repair. Current unchanged HAR10 is5pass/5fail,
with real favicon/order, postData params and compression assertions preserved;
relative redirect now passes. These scopes have empty native stderr and unchanged
inventories. Full feature parity remains unfinished and the custom CEF build is
still deferred by the user.

R1's missing exception flags on two drag control targets and R2's345/346 result
remain preserved. The latter checker observed an O_CREAT marker before its actual
write completed. R3 waits for its complete newline-terminated five-field record;
all original process/inode/rollback/outcome assertions remain. The corrected scope
passes6 independently and in the full346 rerun. A separate mistaken focused
invocation used a nonexistent system Node path; the actual configured runtime was
then used, with that invocation evidence retained.

Build report: `.suitecut/diagnostics/native-b074f3e2-build-report.json` (SHA256
`e9e16e91674b5a09b0b8fee7194a986cf50719de4ee908bc2176159ac5ebf069`).
Runtime index v2: `.suitecut/diagnostics/native-b074f3e2-runtime-index-v2.json` (SHA256
`dd42998abdbd0ca69524d076a610e841f1d852e21e7a1f01213212e0676c792d`).
The original v1 index and all negative evidence remain unchanged.

### Previous ff9863a9 build and scoped evidence

Previously sealed Linux candidate is `ff9863a94ce29f82ab8b8d216afc227322496c158cd021e03bf45a923b4ed2c3`.
All eight gates pass: **331/331 native checks and 946/946 SDK tests in 128 files**.
The immutable manifest records 1951 hashes plus 230 pinned runtime assets; an
independent complete audit found no mismatches. The separate native runner SHA256 is
`aa45458e223e514008583d1113218a52c758cd00635414ce16562ec369c9d134`.

Matching recursive JSON9 and YAML7 pass, including actual cancellation receipt
retirement; the earlier recursive session-mismatch failure remains preserved. The
unchanged accessibility regression passes79/79, checkbox17/select-option33 pass50/50,
and chooser lifetime3, context route removal4 and paused-frame evaluation8 all pass.
Three unchanged chooser timeout cases also pass with the declared fixture mapping
to the genuine native TimeoutError constructor. Original constructor-mismatch
evidence remains preserved; genuine empty-file reset remains vendor-pending.
These exact sealed scopes have empty native stderr, unchanged complete inventories
and confirmed physical helper closure. Evaluation includes real primitive/UTF16
results, exceptions, object rejection/cleanup, local mutation and owner abort; it
does not add child/worker debugging or remote-object handles.

Two sealed checker failures remain separately recorded. The description checker
incorrectly expected Chromium raw AX to expose an equal content/title description;
the owner-signal cancellation checker incorrectly expected an AbortError wrapper.
Reviewed checker-only variants pass5/5 each on unchanged engine/SDK bytes, preserving
all public matcher/count assertions and asserting the actual raw-AX omission and
exact supplied abort reason. These are declared one-checker variants, not the
unchanged sealed checker results.

The first compile attempt's Engine error-sender typo and the second run's 330/331
native result remain immutable. The latter's25ms abort test expired during genuine
output preparation before installing its listener. The corrected checker arms the
listener in the real admitted body, then awaits native setTimeout(25), preserving
the original attempt time origin and all original abort/exit assertions. The third
run passes all331. Its strict native frontend/codegen evidence is explicitly reused
from the byte-identical second build after667 native input and browser/runner hash
comparisons; SDK, configure/build and complete CTest gates ran afresh.

Build report: `.suitecut/diagnostics/native-ff9863a9-build-report.json` (SHA256
`1b0fc4dd932f8661d68ffa7287718cf0d5dc54bb3d7f3e65bf9874249f9a9f46`).
Runtime index: `.suitecut/diagnostics/native-ff9863a9-runtime-index-v2.json` (SHA256
`9ce9978397565c2b1a820065181e616dc471e03c28d12c77e3f404e6af1007fc`); v1 is preserved unchanged.

Following source work adds native test-level work groups, script inventory,
stronger drag-command lifetime handling and boxed attempt attribution. Source review
found an emergency coordinator-exception gap: ordinary child RAII reaps direct
workers, but bypassing the drain can skip artifact rollback and adopted-descendant
retirement. The following repair passes eight injected failures with actual waitpid
and inode-specific unlinkat receipts plus14 public checks. Its combined build is
pending; this evidence does not change the sealed ff9863a9 result.
Anonymous-file parallelism, worker reuse, opaque-child factory restoration and full
feature parity remain unfinished. Custom CEF vendor compilation remains deferred.

### Previous fb2068e8 build and scoped evidence

Previously sealed Linux candidate is `fb2068e8f41b5467d74aed200431951e5ceb75462fed110b58c5a013005c6134`.
All eight fresh gates pass: **320/320 native checks and 903/903 SDK tests in 121
files**. The immutable manifest records 1903 hashes plus 230 pinned runtime assets.
The native runner is independently pinned to
`6e350c1f906b067953657aa31a992003e4eb2e353080de1abf3f17cb83ea7eeb`.
Combined CTest includes actual named-lock scheduling and unique per-attempt output
ownership, with real worker, file, output and descendant retirement fences.

Matching trusted drag checks pass 5/5, context routing 6/6 and additional live
service-worker cases 2/2, and actual root breakpoints 8/8. Each scope reports empty
native stderr, actual helper exit and unchanged complete inventories. These repair
the earlier duplicate drag-over/cancellation, page-less worker routing and missing
breakpoint behavior in their recorded scopes.

Accessibility JSON passes the official 12/12 and unchanged default 8/8 checks.
The full unchanged suite passes 9/11; remaining failures expect Playwright reference
spellings rather than the engine's genuine opaque IDs. Recursive iframe content now
passes. The dedicated recursive-frame checker passes its first eight groups but
fails cancellation cleanup: logical batches reach zero while one physical receipt
remains, followed by a native protocol-session mismatch. That failure is preserved;
physical receipt retirement and complete recursive cancellation parity are not claimed.
Evidence: `/tmp/suitecut-query-fb2068e8-aria32/index.json`.

The unchanged original drag suite passes all 19 runnable tests, preserving its two
original skips and collecting all 21. It emits a genuine 66-byte Fontconfig warning,
so strict stderr cleanliness fails. Matching authored drag5 remains separately clean.
Runtime index: `.suitecut/diagnostics/native-fb2068e8-runtime-index-v1.json`
(SHA256 `99ba4b0a3fbaf7d6185d88943feaefe7d7131fe26dbefaf4f86a6008e7dc253e`).

The first build attempt preserved nine checker failures: eight shared-observer packet
framing defects and one dependency-resolution defect. Two checker-only repairs left
native inputs byte-identical. The second attempt passed all eight fresh gates;
strict native frontend/codegen evidence is explicitly reused from the identical first
attempt, and the configure/build and complete CTest were run again. No failed run
was overwritten. Build report: `.suitecut/diagnostics/native-fb2068e8-build-report.json`
(SHA256 `b5d018bdd9edc3652abfc50561d21ddc79161038351a78b867c3a3db40871e56`).

Following source integrations add YAML frame aggregation, chooser lifetime repair,
accessible-description precedence, explicit global output directories, public context
route removal waiting, primitive paused-frame evaluation and native work-group policy.
Their matching combined build and browser verification remain separate. Empty-file
reset and directory-upload vendor patches remain uncompiled under the user's deferred
custom CEF build instruction. Full feature parity remains unfinished.

### Previous 97916950 build and scoped evidence

Previously sealed Linux candidate is `97916950c775fee46c3a6f361b6b130bf3ac62395d50b85e3d527bc626351f14`.
All eight fresh gates pass: **307/307 native checks and 877/877 SDK tests in 117
files**. The immutable manifest retains 1853 hashes plus 230 pinned runtime assets.
Twelve reviewed merge batches cover 84 changed source files, adding native serial-group
retry waves and reports, real step locations, shared snapshot publication/ACK transport,
context route globs, structured accessibility JSON and debugger owner-abort cleanup.
The full CTest includes actual serial-group public tests and step-location public tests.

Matching ordinary shared-snapshot publication passes all eight groups, with genuine
install/release ACKs, strict empty native stderr, actual helper closure and unchanged
1853+230 inventories. First-author execution still uses the existing synchronous
fallback; opaque-child restoration and new-renderer cache consumption are unverified.
Evidence: `/tmp/suitecut-init-snapshot-97916950-proof-v1/report.json`.

Matching accessibility checks preserve a real failure: ordinary buttons incorrectly
include `pressed: false`. The unchanged default JSON scope passes 6/8; the full original
scope passes 7/11. The other two full-scope failures concern opaque reference spelling
and missing recursive iframe children. All scopes collected their required test counts,
used unchanged inputs, emitted empty native stderr and physically closed. No full JSON
or recursive AI parity is claimed. Evidence: `/tmp/suitecut-query-97916950-aria23/index.json`.
Matching root debugger passes all 12 groups, including idle and paused owner-abort
cleanup before public close. Context routing passes 5/6 groups: existing/future Pages,
global invocation limits, Page priority/fallback and removal pass; a page-less
service-worker fetch reaches the actual wire without invoking its context handler.
All recorded scopes use unchanged sealed inputs and strict empty native stderr.
Runtime index: `.suitecut/diagnostics/native-97916950-runtime-index-v1.json`
(SHA256 `71b95a5a96ec43a3093a1c31561d364d51fe8a3c58bf47b282163f937c1674e9`).

Build report: `.suitecut/diagnostics/native-97916950-build-report.json` (SHA256
`1544cbbad4659021649947a49db9547f1fcdc4ec38206ac1e6934bc6285690a2`).
The interrupted first attempt and the second attempt's two zero-byte object/link
failures remain preserved. Only those reproducible object files were quarantined;
the third attempt rebuilt them and passed all checks with unchanged source inputs.
The custom CEF build remains deferred by user instruction. Full feature parity,
redirect ExtraInfo production, broader route matchers and runner scheduling remain open.

### Previous e38ddd29 build and scoped evidence

Current sealed Linux candidate is `e38ddd293867e4f195d1806ea6185931f6e71121be45897c87a50a8dbdbe99cd`.
All eight fresh gates pass: **294/294 native checks and845/845 SDK tests in114
files**. The manifest retains1804 hashes plus 230 pinned runtime assets. Ten
reviewed merge batches cover70 changed source files, including actual broker-close
waits, explicit BFCache launch policy, bounding-box selection/publication guards,
canonical network query/event ordering, authenticated local-step attachment
cancellation, native parallel projects, locator predicates and root debugger controls.
Shared snapshot receiver components are included; production IPC and factory
restoration remain following work.

Matching exact sealed locator predicates12 and bounding boxes12 pass. Unchanged
original locator-wait-for-function10 also passes10/10 with no skips. Native stderr
is empty, full1804+230 pre/post audits match, and all tracked final ownership
counters are zero. The held asynchronous predicate proof observes logical1/physical1,
then logical0/physical1 after cancellation, and physical0 only after the genuine
release ACK. Evidence: `/tmp/suitecut-query-e38ddd29-feature24`,
`/tmp/suitecut-query-e38ddd29-original10` and the explicit physical-closure receipt.

The full CTest run includes actual parallel-project7 and local-step pending-file
skip public10/ownership7/ACK3 scopes alongside existing runner regressions.
The native pool supports1..8 project workers and shares original deadlines,
failure limits and global identities. It conservatively retires descendant batches
before retry/dependency admission; test-level parallelism and serial-group replay
remain unfinished. Matching exact sealed root debugger10 also passes: genuine author frames/source,
stepOver/Into/Out, coverage start/stop while paused, original deadlines and retained
physical pause intent across caller cancellation. Native stderr is empty, actual
helper closure is confirmed and all1804+230 hashes remain unchanged.
A following SDK fix is required for page-owner abort after a session has already
opened; this candidate does not claim that lifecycle case is complete.

Build report: `.suitecut/diagnostics/native-e38ddd29-build-report.json` (SHA256
`2f28183fd7cc02d8139a13a7d3c87c25166e7a0a77bcce033f8eb45b6b74a009`).
Runtime index: `.suitecut/diagnostics/native-e38ddd29-runtime-index-v1.json`
(SHA256 `e7b9dcaa22be26794e6ea47591b3f848106f9c7852fa47fb40047fae006fc5b8`).
It retains the exact original replacement4/5 negative and separately labels the
corrected independent-observer5 positive, cache-policy3 and disabled-worker-lifetime6.
Exact e38 dedicated-worker coverage9 also passes with strict empty stderr and
complete1804+230 immutable audits.
Same-process child opaque dispatch, the missing redirect ExtraInfo producer,
custom CEF capabilities and full feature parity remain unfinished.

### Previous31a44e69 build and scoped evidence

Current sealed Linux candidate is `31a44e69cf2a630eaf546a21c16b07980386eb8fc0ae1de2ce45f704fae01cd5`.
Its manifest records **1761 hashes plus 230 pinned runtime assets**. All eight fresh
gates pass, including280 native tests and810 SDK tests in110 files. The four
reviewed merge deltas cover55 changed source files: the loading-document CSS
scope guard, shared native locator/retained-handle main-viewport bounding boxes,
marked-hop raw request header association, native StepInfo.attach with real
writer/rollback fences and JSON/JUnit/HTML provenance, and wildcard/no-deps/project
primary-test/shard composition. Native shared snapshot transport components and
a pinned CSS-retention vendor patch are also recorded; they do not restore a
runtime capability by themselves and the vendor patch is not compiled.

Matching exact sealed highlighting12 and bounding-box12 pass with strict empty
native stderr and zero tracked final ownership counters. Highlighting's previous
loading-document null-node failure is repaired. Actual dedicated-worker coverage9
also passes, including the unchanged120ms-startup contract (observed5.878ms), with
real process closure and full1761+230 audits. The full14-scope query set also
passes110/110, and unchanged original bounding-box9 passes9/9 with no skips or
flaky outcomes. Native stderr is empty, tracked final ownership counters are zero,
and exact sealed hashes remain unchanged in these positive scopes.

The new exact sealed request-header wire6 check passes1/6. A separate raw-receipt
diagnostic establishes that pinned CEF emits no post-redirect ExtraInfo for the
failing hops in either observer; FIFO association cannot recover absent wire
headers. Producer/interception behavior remains under investigation.

A separate static bounding-box first-selection group-transfer repair is committed
for the next batch; these functional positives do not claim that pending repair
is already compiled. Same-process child opaque dispatch remains under repair
(the prior c6 unchanged dispatch17 has16pass/1fail). A genuine native private
factory bridge is being tested without a page-visible global. Default pinned
BFCache can preserve the actual old author worker across navigation; the same
unchanged c6 lifetime6 passes6/6 under an explicit diagnostic cache-disabled
launch policy. No default-policy behavior is silently changed. The corresponding
public launch option is a following branch.

Build report: `.suitecut/diagnostics/native-31a44e69-build-report.json` (SHA256
`5d32f197fbc1348660b764a460b941bcd2515e0f86c8e08f134ad92f519cf262`).
Runtime index: `.suitecut/diagnostics/native-31a44e69-runtime-index-v1.json`
(SHA256 `b5224c1ff397350ff8a98d922f71a2a1ba6da282389f3680bbabe9a65d0fdd4e`),
with independent current1761+230 hash verification and separate positive/negative scope links.
The custom CEF build and full feature parity remain unfinished.

### Previous c6fbefa8 build and scoped evidence

Previous sealed Linux candidate is `c6fbefa811d009bb598cd2a46f7121378629cc88bc4e9942eafac392f0177ff7`.
Its manifest records **1731 hashes plus 230 pinned runtime assets**. All eight fresh
gates pass: SDK build/types/lint and798 tests in107 files, strict complete Source
frontend, native build, diff check and270 native tests. No SDK gate inheritance
is used for this batch. The six merged deltas cover47 source files: native
locator highlighting and shared DOM/CSS leases, same-process sibling bootstrap
containment, lazy visibility filtering, context worker cursor retention through
physical broker replacement, serial named project dependencies/teardown, and a
pinned unchanged bounding-box oracle catalog. All49 runtime exports and153 prior
type exports remain; four highlight/project types are added.

Matching actual sibling bootstrap4 and unchanged ownerFrame10 pass with clean
native stderr and full1731+230 input audits. Child utility dispatch is restored;
opaque native factory capture in same-process children remains explicitly
unsupported pending the separate transport/startup-barrier implementation.
The ownerFrame driver initially looked in the wrong output directory; its real
first-run10-check report and actual close/stderr receipts were recovered without
rerunning behavior, and the driver routing failure is retained.

The exact sealed highlight checker passes its first9 actual groups, then fails
before its iframe highlight call: a loading document without documentElement
causes a genuine null-node selector error. That negative is preserved at
`/tmp/suitecut-query-c6fbefa8-proof/highlight/.suitecut/native-locator-highlight/failure.json`.
The independent query110 run passes13 scopes/96 checks; reference scope completes10
checks before the same loading-document error. Pristine10/dispatch12 pass.
All17 unchanged original dispatch tests collect:16 pass and one valid child
DataTransfer case now explicitly rejects, exposing the containment capability
regression; full child native capture remains under repair. Worker ownership,
reattachment and historical console12 pass. A separate lifetime checker passes4
of6 groups, then exposes actual worker closure lagging awaited navigation.
The following loading-document guard is committed; these negatives remain intact.
The exact sealed project CLI11 also passes with full1731+230 unchanged inputs.
A complete build pass is not a full runtime parity claim.

Build report: `.suitecut/diagnostics/native-c6fbefa8-build-report.json` (SHA256
`42ac24e640f6b9df8b8dc7f81a12db7a6fb317e6907d5d5a860e497c34b43341`).
Private Source component/sanitizer proofs on other branches do not substitute for
a compiled matching browser. The custom CEF build remains user-deferred.

### Previous d9b5f817 build and scoped evidence

Previous sealed Linux candidate is `d9b5f81746d049f7cfe0f1bee3ddbe9d7f68e5c6b566733e17b043c3a1b32559`.
Its `.suitecut/native-integrated-d9b5f817-epoch/manifest.json` records **1704 hashes
plus 230 pinned runtime assets**. All **262 native tests and 788 SDK tests in 106
files** pass. The 73-entry integration record preserves the earlier 249/251 and
254/255 native failures and the strict C++ compilation failure. The final native
frontend/build/CTest gates are fresh; four successful SDK gates are inherited
from R3 only after the full input comparison proves that the sole subsequent
change was explicit control-flow formatting in one native C++ include. The SDK
outputs remain hashed and unchanged. All 49 runtime exports and the previous 151
type exports remain; two native attachment types are added.

Matching immutable browser checks pass pristine dispatch10, ordinary dispatch12,
and all17 unchanged pinned original dispatch tests, with strict empty native
stderr and complete before/after input audits. Actual OOPIF factory creation,
DataTransfer/File identities, captured query intrinsics, at-most-once authored
errors, cancellation, and coverage coexistence are covered by these scoped checks.
Actual worker mixed ownership4, same-source reattachment4, and historical console4
also pass without checker overlays. Genuine channel detach is distinct from
actual author target destruction; terminal closure takes precedence only after
its real receipt.

The matching sealed native runner/SDK passes file/body attachments13, actual
file/process ownership9, accepted test skip with pending attachments12/ownership5,
control EOF6, confirmed cleanup kill3, and pending-file EOF fence1. Fourteen
separate legacy runner scopes also pass their own contracts. Expected real
cleanup failures, timeouts and SIGKILL remain visible. Native file publication
requires the actual writer/rollback process and receipt fences; fixture and
afterEach admission waits for the actual callback and file retirement fence.
Step attachments, projects and other broader runner features remain separate
branches, not claims of this snapshot.

Build report: `.suitecut/diagnostics/native-d9b5f817-build-report.json` (SHA256
`499597507b3f3fb69686df6e2cf75ebf513de308a951f5fb9a6986195621db09`).
Browser dispatch report: `/tmp/suitecut-pristine-d9b5f817-proof-v1/report.json`
(SHA256 `99ac1a19464e8df7db5c7cabb5f28721c2592c1fa36d794d5b3a4ab98b8bb60c`).
Worker report: `/tmp/suitecut-worker-generation-d9b5f817-proof-v1/proof-index.json`
(SHA256 `d7fddf97d3324d58623aa5d11b3b5f4f2b9e26d834fb194785c915bc2e522627`).
Runner report: `/tmp/suitecut-runner-d9b5f817-integrated/audit.json`
(SHA256 `bc41176988f92a8dd0af5a9bf81066cb23b5d1f2b5ead789b73d171c16452365`).
All17 unchanged dispatch results are recorded in
`/tmp/suitecut-original-dispatch-d9b5f817-proof-v1/report.json` (SHA256
`1d3c6838978d6d506e132c88bb2f6d6c1771c00346afdb4e01b4b3c28bfb7f66`).
The exact sealed official clock10 and observed runFor exception4 also pass.
The latter covers actual synchronous uncaught main-document exception receipts
with a real final pause acknowledgement; it does not attribute errors exclusively
to timer callbacks. Promise rejections remain separate page errors.
The aggregate runtime index is
`.suitecut/diagnostics/native-d9b5f817-runtime-index-v1.json` (SHA256
`01da594973d9a8e34edc611de974127697af95b93f89ba558368ec83cc9d71c6`),
with an independent unchanged1704+230 audit and seven separately scoped reports.
Other matching browser families remain separately scoped; diagnostic4752
positives do not substitute for sealed evidence. The sibling bootstrap containment,
native highlighting, worker cursor retention and serial project scheduling are
merged in the next frozen integration batch, whose combined checks remain pending.
Full parity is unfinished. The custom CEF build remains deferred by user choice.

### Previous fcbaec21 verification boundary

Previous sealed candidate is `fcbaec21171d438ffcde822f5aa3e18a81823fa0eac7640ed17183962dbbb040`.
Its `.suitecut/native-integrated-fcbaec21-epoch/manifest.json` records1650
source/SDK/test/script/object/evidence hashes plus 230 pinned runtime assets.
All243 native tests and762 SDK tests in104 files pass, with all eight applicable
gates successful. The36-entry record adds the structural native-string dispatch
packet, shared debugger domain ownership, native clock duration/lifetime guards,
locator normalization, actual worker generation DTOs and the trace correction.
The successful SDK build/export audit is inherited only after the full inventory
proved the sole change was an authored fixture's required parentId field; the
initial failed typecheck is preserved, and all remaining seven gates were rerun.

Exact compiled dispatch12 now passes with strict empty stderr and final native
ownership zero, repairing the genuine previous retained-list destructor SIGSEGV.
References14, trace4, normalization9 and83 query regressions also pass, with all
reported counters zero, strict empty stderr and complete unchanged input audits.
The query index is `/tmp/suitecut-query-fcbaec21-proof-index.json` (SHA256
`e22aacf406581590ef6c916ed8c111feb09a78448c6f3e326d37bc8273bca0d4`).
Matching CEF-free runner scopes pass declared-step-skip10/ownership3/report12,
local-step-skip20/ownership3, accepted-test-skip13/ownership3, steps22 and selected
unchanged originals. Actual cleanup errors, timeout and SIGKILL remain retained.

Actual context-network4 fails because the separate native context broker omits
the required control identity on retained console handles; the SDK correctly
rejects the missing identity. This actual failure is preserved, with a native
correction following. Exact worker observation2, coverage9, OOPIF7, genuine crash/reload2 and
same-source reattachment4 pass with strict empty stderr. A separate dedicated
worker console-history checker fails after one group because actual detach is
published as permanent close. Clock official and observability-only checks fail
on crash-waiter closure; a separately labeled diagnostic establishes the first
nine groups completed. The augmented clock helper genuinely exits0/no signal.
Pristine main-realm factories and runner attachments remain
following work and are excluded from this epoch. No complete API228 rerun is
claimed; the last full result remains69d33955's224PASS/4FAIL.

Build report `.suitecut/diagnostics/native-fcbaec21-build-report.json` has SHA256
`f8bf1c7ab281d6e920b32fa2e0b01d3ac5a04e7d1808c1bf2669592459efec93`.
Matching dispatch evidence is `/tmp/suitecut-dispatch-fcbaec21-proof/index.json`;
matching runner evidence is `/tmp/suitecut-runner-fcbaec21-integrated/audit.json`.
Runtime index `.suitecut/diagnostics/native-fcbaec21-integrated-runtime-report.json`
(SHA256 `d6fa6f01603d8b142ba398e6f0f2dbfc4510c7d7e20db115bffc2ea21d3add4c`)
hashes12 scoped reports, retaining all three distinct failed families. Root
independently verified all1650 hashes and 230 runtime assets unchanged.

Previously sealed candidate is `f8778e7b1007d689a2625b10e383ddd4cfc4f0fbc5a47ffee319ab7f5653fe09`.
Its `.suitecut/native-integrated-f8778e7b-epoch/manifest.json` records1628
source/SDK/test/script/object/evidence hashes plus 230 pinned runtime assets.
All237 native tests and748 SDK tests in103 files pass, with all eight applicable
build gates successful and the existing49 runtime exports/150 named declarations
preserved. The38-entry integration record adds native snapshot references,
worker reattachment, predeclared step skipping and SDK recovery. Exact compiled
references14 and83 query regressions pass. Worker observation2, context-network4,
coverage9 and genuine same-source worker reattachment4 pass with strict empty
stderr, actual process-exit receipts and complete unchanged input inventories.
CEF-free matching runner scopes include declared-step-skip10/ownership3,
local-step-skip20/ownership3, accepted-test-skip13/ownership3, public steps22 and
selected unchanged originals; intentional cleanup errors and SIGKILL remain visible.

Two actual browser failures remain explicit. Trace verification completes two
groups before detecting an extra DOM snapshot. Dispatch completes zero groups;
GDB confirms SIGSEGV during destruction of the retained CEF argument list after
the first real dispatch result. The attempted argument-list copy did not fix it.
Following source contains a structural native-string packet repair and a trace
correction; those changes are excluded from this sealed epoch until a subsequent
compiled browser proof. Pristine main-realm event factories, shared debugger
ownership, locator normalization and clock additions are also following work.
The last complete API228 run is still the69d33955 result224PASS/4FAIL; it was not
rerun onf8778e7b.

Build report `.suitecut/diagnostics/native-f8778e7b-build-report.json` has SHA256
`d8cb354d93e778b4cfe0b751096c7e93df5819c42b21dacc0e34991d7de03018`.
Runtime index `.suitecut/diagnostics/native-f8778e7b-integrated-runtime-report.json`
(SHA256 `22f5aca997dd21f832ec028a9c0c7200f2856b97277db270edb7a0e58b062b09`)
hashes12 scoped reports, including both browser failures. Root independently
verified all1628 hashes and 230 runtime assets without mismatches.

Previously sealed candidate is `69d33955f6055776897f05c825c71e5ba0b76d9bc9c4f8cb9e958ef3d03bb19c`.
Its `.suitecut/native-integrated-69d33955-epoch/manifest.json` records1597
source/SDK/test/script/object/evidence hashes plus 230 pinned runtime assets.
All226 native tests and729 SDK tests in102 files pass; all eight applicable gates
pass, preserving49 runtime exports and150 named declarations. The34-entry record
adds native context-specific CA certificates, actual observed-worker lifecycle and
helper-process close receipts, runtime local-step skip, explicit empty-HAR fallback,
and the origin diagnostic checker correction. Two documentation-only lint failures
are retained; successful SDK build/type/unit evidence was reused only after the
complete inventory proved the sole correction was that helper comment.

Exact compiled CA4 and official origins7 pass with native ownership settled,
strict empty stderr and actual helper exit0. Unchanged API228 remains224PASS/4FAIL,
with the same four known failures and no new API failures. The worker wave passes
actual pre-ready observation2, context-network4 and worker-coverage9: genuine
service-worker request metadata arrived before published inventory while real script
bytes remained held; logical shutdown preserved identity until real process exit.
CEF-free runner checks pass local-step-skip20/ownership3, accepted-test-skip13/
ownership3, public steps22 and selected original outcome/skip/hook regressions.
Intentional cleanup faults and SIGKILL remain recorded.

Official dispatch verification genuinely SIGSEGVs after five completed groups.
GDB confirms real Text dispatch completed; the following relatedTarget DOM-handle
adoption crashes in CEF value-controller ownership. This failure is retained in
`/tmp/suitecut-http-69d33955-proof/dispatch/failure.json` and is under diagnosis;
passing build/component checks do not establish dispatch success. Empty-HAR
original60 and publicHAR7 pass; ContextHAR6 behavior completes but strict stderr
fails on the preserved Fontconfig diagnostic. Snapshot references,
worker reattachment recovery and pristine main-realm event factories remain
following work, excluded from this epoch.

Build report `.suitecut/diagnostics/native-69d33955-build-report.json` has SHA256
`c750110420e443fe2da14652976ce22d3576969ef1001f4beded3fa85a6b4335`.
Runtime index `.suitecut/diagnostics/native-69d33955-integrated-runtime-report.json`
(SHA256 `41e07a0a262d410b318b5becb581be233e0acd2e331ac0487631ffd99c71b0cc`)
hashes12 scoped reports, including genuine dispatch and Fontconfig failures.
Root independently verified all1597 hashes and230 assets without mismatches.

Previous sealed candidate is `bd6f06328749db29a30cb3138e19a18c145fd4f01b52b30cac1a7d0442928f29`.
Its `.suitecut/native-integrated-bd6f0632-epoch/manifest.json` records1577
source/SDK/test/script/object/evidence hashes plus 230 pinned runtime assets.
All220 native tests pass. The SDK has697 passing tests in100 files; build,
typecheck, lint and public API checks preserve49 runtime exports and150 named
declarations. The corrected native run reuses successful SDK gates only after
proving its sole source change was an authored C++ test; those SDK inputs are
unchanged. The initial219/220 result and old host-only test expectation remain
recorded separately.

The28-entry record integrates accepted native test skip with pending-step raw
receipts, including the real premature-exit0 cleanup failure; authoritative API
constructor-origin snapshots with actual restore-ACK commit; imported host-only
wire scope and genuinely completed empty encoded responses; screenshot matcher
constructor-lifetime forwarding; and bounded API-option marshaling. Associated
browser state remains genuine mutable capture. Official jar6/regex3 pass on this
exact candidate, including host-only wire exclusion and20 create/close cycles.
Complete unchanged API228 passes 224/228 with no skips or new failures; all five
targeted cases recover. Official extended screenshot/PDF9 and regression20 pass,
including Page/locator matcher constructor abort. Exact runner skip13/ownership3,
steps22 and selected original outcome assertions pass; intentional cleanup errors
and SIGKILL remain visible. Origin7 passes a separately hashed diagnostic-routing
correction, including actual16-origin cancellation with4803036 retained bytes
and all12 final ownership counters zero. The sealed original diagnostic-schema
failure remains recorded. Worker
observed-target lifecycle, snapshot references, local step.skip and extra CA
trust remain following/private work.

Build report `.suitecut/diagnostics/native-bd6f0632-build-report.json`
has SHA256 `b7c6f3cd149d54676ae1209832902b5afaf4ba12e40c64a8cdb7c367a210e79a`.
Runtime index `.suitecut/diagnostics/native-bd6f0632-integrated-runtime-report.json`
(SHA256 `4a1f4c4a173ee1ae7f5359510929420504c1a67c89e01d0e11dba85634484d04`)
hashes16 scoped reports, including failures and explicitly diagnostic-only
favicon observations. Root independently verified all1577 epoch hashes and230
runtime assets with no mismatches. Following source changes are excluded.

Vendor CEF compilation remains user-deferred and full feature parity is unfinished.

Previously sealed candidate is `b4ec7e897cdf7412e2c3b089329030959429879b0d5949b046b94225bc549594`.
Its `.suitecut/native-integrated-b4ec7e89-epoch/manifest.json` records 1562
source/SDK/test/script/object/evidence hashes plus 230 pinned runtime assets.
All eight build/check commands pass: 213 native tests and 686 SDK tests in 99
files, with the complete source inventory unchanged. Automatic public API audit
preserves all 49 runtime exports and all 145 prior named declarations, adding
five types for dispatch options and native steps.

The 62-entry integration record includes genuine Page/Locator/fixed-node event
dispatch, bounded real-node strict diagnostics, the cookie manager typed-ingress
and visibility reply repairs, authoritative independent API cookies with actual
header/auth receipts, native bounded content decoding and HTTP framing checks,
explicit bounded EOF replay under maxRetries, chunked large screenshot/PDF
configuration with actual receipt ownership and original-clock parser checks,
and native test steps with raw-callback acknowledgment fences. Associated browser
cookie/state policies remain separate from independent API state.

Matching exact-binary proofs pass query/snapshot83, unchanged strict/query27
and getter/state52, network metadata/preflight/image/collector23, and screenshot
regression20. Large screenshot/PDF configuration9, cookie-jar6, cookie-regex3
and dispatch12 pass separately hashed checker variants; the original authored
checker setup failures remain recorded. Unchanged dispatch17 is13 pass/4 fail:
atomic custom selectors and opaque DataTransfer arguments remain unsupported.
The complete unchanged request/response57 has48 assertion passes, one original
expected Chromium failure and8 unexpected failures, with0 skips. Request-event
attribution for the remaining failures is under investigation. Runner steps22,
original step427/modifiers568 and skip/hook regressions pass; intentional cleanup
errors and SIGKILL remain visible. Complete unchanged API228 is219 pass/9 fail,
0 skips. Five earlier failures recover; two regressions concern imported host-only
cookie scope and empty Brotli bodies. Their repairs remain following work.
These overlapping scopes are not a parity total. Vendor CEF compilation remains user-deferred and
full feature parity remains unfinished.

The immutable runtime index is
`.suitecut/diagnostics/native-b4ec7e89-integrated-runtime-report.json`
(SHA256 `6d25351c14d86bd89403b5586b7fd066d693bca88fd4065d24b7b50a6d733b2a`).
It hashes22 scoped reports, including preserved checker failures, corrected
checker variants and full original-suite failures. Root independently rechecked
all1562 epoch files and 230 runtime assets with no mismatches. Following normal
source changes are excluded from this binary and its claims.

Previously sealed candidate is `5d9a79741ab4e5491bfec48f1747afe6449d498004e606473af49d3fc02931f5`.
Its `.suitecut/native-integrated-5d9a7974-epoch/manifest.json` records 1510
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All 198 native
checks and 659 SDK tests in 95 files pass; all eight build/check commands succeed
and the complete source inventory remains unchanged. The 34-entry integration
record adds native regex cookie filtering, Page.fill first/strict options,
shared response projection for navigation and network events, native-delivered
expectedStatus/runtime annotations and actual V8 source locations, response
vector-capacity accounting, and grammar-validated missing-frame visibility
snapshots with XPath/escaped-colon parser corrections.

Existing public exports are preserved: 49 runtime exports and 142 existing named
declarations, with three new public types. Earlier build negatives are retained:
an obsolete unsupported-RegExp unit assertion and a stale private patch's removed
HTTP error export were corrected before this passing build. Native 198 includes
actual runner metadata process/ownership/report checks. Exact matching runtime
proofs pass query regressions56, Page.fill first/strict5, HTTP capacity3 and typed
transport errors4, HTTP/TLS/HAR metadata6 plus independent network17, screenshot8,
unchanged response363/3742, unchanged getter/state52, and selected original runner
skip/hooks/annotation cases. Each recorded scope audits all1510+230 epoch inputs;
intentional runner timeout and real cleanup SIGKILL remain visible.

The unchanged strict/query27 scope reports20 pass/7 fail/0 skip: all17 query cases
pass, while strict diagnostics remain3/10. Cookie regex still genuinely fails the
manager's older strings-only ingress; the unchanged original124 also fails.
The official snapshot checker first has a malformed-CSS expectation mistake;
a separately hashed syntax-only correction reaches9 checks then exposes the real
`{type:"object",value:false}` native reply. Both failures remain recorded.
Following source fixes share typed cookie admission and canonicalize only actual
native visibility booleans. They do not change this sealed epoch's results.

The completed scope index is
`.suitecut/diagnostics/native-5d9a7974-integrated-runtime-report.json`
(SHA256 `1e5e8b550daa9ae2a1e654ec832f1422d00279c97d542bcc9b95705c1c188e23`).
Overlapping scopes are not summed as a parity total. The custom CEF build remains
user-deferred and full feature parity remains unfinished.

A subsequent complete unchanged API228 rerun on this exact5d9a build also reports
216 pass/12 fail/0 skip in44.5 seconds, all228 collected with no collection errors,
empty native stderr and all1510+230 epoch hashes unchanged. Its12 failure identities
match the preserved81c localhost run exactly. This completed run is recorded in
`.suitecut/diagnostics/native-5d9a7974-api228-addendum.json`
(SHA256 `c06b03eb17ea139fa7cdb658b299eb2ff19076752d6c3fa550d154cbbd010c91`);
the earlier scope index remains unchanged.

The following81c record remains historical evidence, including its TLS metadata
failure. Neither fullAPI228 result is a full browser/runner parity total.

Previously sealed candidate is `81c1e25bb11d22afbd64ce717291f50df12348b122a9bff660afcc72f4344b96`.
Its `.suitecut/native-integrated-81c1e25b-epoch/manifest.json` records1481
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All193 native
checks and651 SDK tests in93 files pass, with all eight commands successful and
complete source inputs unchanged. This adds native beforeAll runtime skip,
structured actual transport-error metadata, response/HAR endpoint and certificate
projection, prompt screenshot-failure publication with cleanup ownership, improved
validation diagnostics and posted selector-wait fatal teardown.

Actual query56, screenshot20, nativeHTTPerror4, unchanged targeted API5 and
unchanged response363/3742 pass. CEF-free original runner skip3/hooks311/576 also
pass with1714 pre/post hashes (1481+230+3checkers);576 preserves intentional
failed/timedOut outcomes and real cleanup SIGKILL. The complete unchanged API228
on actual localhost reports216pass/12fail/0skip, all228 collected without errors
and native stderr empty. An earlier127.0.0.1 fixture run reports213/15; its literal
localhost assertions caused three failures and the first cookie83 assertion.
Both runs are preserved. The later cookie83 subdomain-filter assertion still
fails genuinely; no browser cookie metadata is relabeled to pass it.

The actual HTTP/TLS/HAR6 checker fails after two genuine HTTP assertions: the
legacy navigation path emits raw CDP certificate extras, which the strict public
schema rejects. Native preflight6/image6/privateCollector5 independently pass17.
The following source batch uses one fresh native response projection for both
controller events and navigation replies; this is not yet a compiled positive.
A separately hashed new catalog checker collects untouched strict10/query17 and
reports19pass/8fail/0skip on81c: parenthesized XPath detection, strict diagnostics,
Page.fill strict options and dispatchEvent remain observable gaps in that binary.

The historical 34-entry following integration batch includes the TLS projection repair,
native regex cookie filtering, Page.fill first/strict options, native-delivered
expectedStatus/runtime annotations and genuine source locations, actual response
vector-capacity accounting, and validated missing-frame visibility snapshots with
XPath/escaped-colon parser corrections. Its serial build/check cycle subsequently passed as 5d9a above;
private helper/component proofs do not establish this combined browser behavior.
Vendor CEF compilation remains user-deferred; full feature parity is unfinished.

The hashed scope index is `.suitecut/diagnostics/native-81c1e25b-integrated-runtime-report.json`
(SHA256 `04453d39bf6834f9a0df69b51b8b29c48298b4b2f6a63c59f734f98000250de1`).
Overlapping scopes are not summed as a parity total. The following d0/c8/ed
records are historical and do not replace the current results above.

Previously sealed candidate was `d0def75c7e2dc67cea047875075f93e61916b28ef35f80fb10773f52d4e4e90d`.
Its `.suitecut/native-integrated-d0def75c-epoch/manifest.json` records1460
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All187 native
checks and642 SDK tests in90 files pass; all eight build/check commands succeed
with the complete source inventory unchanged. It integrates adopted stream
multipart, bounded chunked screenshot input, exact preflight authorization,
private image-body collection, focus-owned fill/clear, editable cancellation,
Page first-match/strict getters, corrected contenteditable state, native dynamic
expected-failure amendments and static expected-failure cleanup precedence.

Untouched getter/state/retarget52 now passes52/0fail/0skip with all52 collected.
Authored getter14/strictPage10/ownerFrame10/frameMetadata8, focus5/signal3/preview3,
ReadStream5/auth4/default2/multipart5/header3 and receipt-deadline checks pass.
Untouched HTTP1197/1000 pass2; preflight6/image6/privateCollector5 and untouched
response363/374 pass2. The prior c8 empty-GIF failure is historical, not erased.
The contenteditable checker initially saw a still-pending real release ACK; all3
behavior assertions pass when the checker waits for actual zero ownership. Its
original immediate-counter negative is retained with the1.6→10.4ms ACK trace.

Actual screenshotInput7 and screenshot8 pass, including232989-byte PNG input from
a Buffer and real readonly path,130KiB CSS,29 native append commands and a33066-byte
largest wire command. Public held-filesystem4 and joint-capacity checks also pass
on this binary with a test-only exact-path pass-through syscall gate: real read
and close receipts establish timeout/cancel retention, same-byte mutation rejection,
context-close waiting, the actual shared2-worker cap and queued original timeout.
The first input checker observed the wrong transport's counter; its authored
failure remains separate from the corrected passing checker. Strict native stderr
is empty and full1460+230 pre/post inventories remain unchanged for these scopes.

The current full API228 result remains c8's209pass/19fail; focused d0 positives do
not rewrite that total. Actual d0 negatives remain for Page.fill default/strict
options and missing-lazy-frame visibility snapshots. Following-batch beforeAll
skip, response/HAR endpoint/TLS metadata, structured transport errors, validation
messages, posted selector-wait teardown and queued screenshot-cleanup publication
are being checked separately. Controlled128-lane SDK sender proofs do not establish
128 actual CEF controls: real ordinary event waits cap64 and screenshot leases16.
Vendor CEF compilation remains deferred by user choice; full parity is unfinished.

The hashed scope index is `.suitecut/diagnostics/native-d0def75c-integrated-runtime-report.json`
(SHA256 `138c84b73740862bdd8a85622e7f7382d546afb12c65927725727bdf6605a813`).
Overlapping scopes are not summed as a parity total.

Previously sealed candidate was `c8c823f57e82213fb6cc039f3b72296216b32d7f9a756f900840c7ee389e2ee9`.
Its `.suitecut/native-integrated-c8c823f5-epoch/manifest.json` records1418
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All177 native
checks and610 SDK tests in85 files pass, with all eight check commands successful
and complete build inputs unchanged. It integrates the matching authentication
receipt clock, required explicit controller clock, ACK-owned utility-world DOM
getters, genuine label/ancestor state observations, page state aliases, frame
locator page ownership, locator descriptions, native DOM handle captions and
label checked assertions. Runtime body/beforeEach skip is also native-owned,
including actual cleanup/receipt fences and exact bounded UTF16 reasons.

Actual getter14/frameMetadata8/ownerless3, auth4/default2/multipart5/header3,
receipt deadlines, workers9/ownership6/OOPIF7/nested2/recovery2, smallScreenshot8
and inputValue8 pass on this exact epoch with strict empty native stderr. The
untouched getter selection collects52 and reports51pass/1fail/0skip; the remaining
contenteditable inside a button is incorrectly retargeted for isEditable. Full
untouched API228 reports209pass/19fail/0skip. All failures remain recorded.

Original ownerFrame checker fails by reading text still present in the old child
document before navigation commits. A real held-response trace establishes this
ordering; waiting for genuine Frame.waitForURL makes the checker pass10. The
original preview checker passes2 then its setup returns Node.prototype, causing
an unrelated codec error before the next assertion. Returning void0 only from
that setup makes the original caption assertions pass3. Both original failures
and these checker-only corrections are retained separately.

Private body collector5 passes; unchanged response363 passes and374 fails with
an empty evicted GIF. A separate real-agent capability trace obtains the exact14
GIF bytes from the existing secondary session while direct primary data is empty;
it does not relabel the failing original374. Selected unchanged runner skip3 and
hooks311/576 pass their narrow adapters with before/after1418 input hashes; this
CEF-free run does not claim an asset audit or full hook-file parity.

Raw native forced fill/clear of a disabled target can edit a previously focused
outside input; this actual negative is retained. The following build integrates
the focus-ownership guard, public fill/clear cancellation, contenteditable state
correction, Page first-match/strict semantics, adopted stream multipart input,
chunked screenshot input, exact preflight route authorization, image admission,
and native dynamic expected-failure amendments. Their combined checks and live
positives remain pending. The c8 screenshot proof uses only small PNGs and retains
its inline64KiB limitation. Vendor CEF compilation remains user-deferred.

The exact hashed scope index is
`.suitecut/diagnostics/native-c8c823f5-integrated-runtime-report.json`
(SHA256 `9d38317a9fe2d45c108fa785441ece67e6ac9e807591c06f6d974a91451cc220`).
Overlapping scopes are not summed as a parity total.

The following ed097 evidence is historical. In particular, its authentication
clock failure and getter52 negatives are preserved rather than claimed as the
outcome of the new candidate.

The earlier sealed candidate was `ed09748016e03b904ccf3e93fe0ee2f26f4dcbff35cfe3f9e4b72eb2f2d35af6`.
Its `.suitecut/native-integrated-ed097480-epoch/manifest.json` records1405
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All173 native
checks and599 SDK tests in83 files pass; all eight build/check commands succeed
with unchanged source inputs. This integrates native inputValue, frame-scoped
Inspector commands and lossless frame metadata, private prefetch-body collection,
small-PNG screenshot comparison, byte-correct HTTP headers, and native runner
phase snapshots, timeout amendments and nested-suite skip/focus declarations.

Actual frameMetadata8/ownerFrame10/ownerless3, worker9/ownership6/OOPIF7/nested2/
recovery2, defaultAuth2/multipart5/headerWire3 pass on the exact binary. Untouched
original response363/374 pass2/2, including formerly empty prefetch bodies;
original header834 passes1/1. Original runner hooks311/576 pass their narrow
adapters; the latter preserves intentional timeout/cleanup errors and worker kill.
Its inventory audit is post-run1405 inputs only, not an independent pre-run audit.
Corrected checker-only inputValue8, smallScreenshot8 and privateCollector5 also
pass; their original authored checker failures remain retained separately. The
screenshots use small PNGs: inline input above64KiB is still an explicit gap.

The unchanged original getter/state/label selection collects all52 tests and
reports27pass/25fail/0skip with empty native stderr. Remaining gaps include public
page state aliases, protected getters and label/ancestor state retargeting,
previews/descriptions, frame locator page ownership and label checked assertions.
These failures guide the following batch; staged fixes are not claimed here.
Credential update/auth4 fails because the new receipt check uses a different
clock origin from Engine deadlines. The matching-clock injection and mandatory
constructor clock are applied for the following build, whose actual runtime proof
is pending. Controlled component tests alone do not establish the repaired browser.
The hashed scope index is `.suitecut/diagnostics/native-ed097-integrated-runtime-report.json`.
Overlapping scopes are not summed as a parity total. The full API228/network57
have not been rerun on this epoch, and the vendor CEF build remains user-deferred.

The following092 evidence is historical; its prefetch and frame-access gaps are
repaired above, while the auth receipt regression was introduced after092.

The earlier sealed candidate was `0920242b360f1a5f8615abfb6bf10469738176d2e45d268ba99cc04932fa490f`.
Its `.suitecut/native-integrated-0920242b-epoch/manifest.json` records1367
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All164 native
checks and583 SDK tests in80 files pass; all eight build/check commands succeed
and the complete source inventory stayed unchanged. This integrates bounded
literal nested suites, global runner timeout and all-hook metadata, native
multipart framing, credential-clear policy, ownerless-node and UTF16 frame-name
repairs, associated-label fill/clear, typing focus/caret behavior, response receipt
aliases and bounded image-body streaming.

Exact092 runtime proofs pass ownerFrame10/ownerless3, original ownerFrame7/7,
auth4/default-auth2/multipart5, label fill/clear6, type6, worker9/ownership6/OOPIF7,
nested closure2 and worker recovery2. All native helpers close cleanly with empty
stderr, final tracked ownership0 and1367+230 unchanged hashes. Original API
credentials593/603 and ordered form1123 pass3/3. Original type5 passes5/5 with an
explicit checker-only collection update and the exact sealed native/SDK. These
scopes overlap and are not summed as a full parity total.

Redirect functional2 and image-body wire5 pass; the pinned original image374
also passes. Original prefetch363 still returns an empty body. Both actual image
hold attempts completed their stream receipt before observation, so all3 authored
physical-ownership gates remain failed/unexercised, despite functional body
success. No live late-after-finished redirect ordering was observed.

Frame metadata passes its first5 transport groups, including isolated UTF16 units
at the1024-unit bound, then fails an authored navigation assertion that assumed
the browser preserves an isolated-unit window name. Actual raw Inspector/DOM
probes on092 and5313 both show the name resets and the Inspector name falls back
to the iframe ID. No SDK name is invented to restore the lost value. Remaining
navigation/OOPIF/binding groups need the separately staged corrected verifier and
frame-scoped native Inspector access. The complete hashed runtime index is
`.suitecut/diagnostics/native-092-integrated-runtime-report.json`.

The following5313 results are historical, including the credential crash and
ownerless-node failures repaired and verified on092.

The previous sealed candidate was `5313fc847b0b7b330a25df7a1f7db02dbad452b25156bdbca4155ec767b34bc4`.
Its `.suitecut/native-integrated-5313fc84-epoch/manifest.json` records1328
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All151 native
checks and575 SDK tests in78 files pass, along with both builds, typecheck, lint
and diff checks. Complete source inputs stayed unchanged during the final build.
The first native run retained a failing controlled session-handoff fixture that
omitted required actual protocol hasExtraInfo flags; adding those flags preserved
the original header/migration assertions and made the complete rerun pass.

Strict actual worker coverage9, ownership6, OOPIF7, nested iframe/worker closure2,
worker renderer recovery2 and native blur8 all pass with empty native stderr,
actual helpers closed, scoped ownership drained and1328+230 unchanged hashes.
Original ownerFrame7/7 and separately selected original CORS129 also pass. The
broader authored ownerFrame10 fails on genuine unattached HTMLDocument adoption;
additional ownerless-node probes confirm the related detached-document boundary.
Native frame names containing isolated UTF16 units also lose their metadata;
these exact failures and their next-batch repairs remain separate from original7.

Credential updates pass their first2 actual groups, then null clear leaves a real
browser fetch waiting; the unchanged30s verification ends with SIGSEGV during
shutdown. A separate1s diagnostic and genuine nonmatching-provider comparison
show Fetch CancelAuth returns401 while empty policy enters Chromium fallback UI.
GDB records actual inferior SIGSEGV in modal-dialog widget destruction and no
Source GetAuthCredentials entry. The wrapper exits0 and is explicitly not a clean
native-exit proof. That5313 empty-policy defect is repaired and verified by the092 auth4 rerun.

Real direct/intercepted redirect headers and canonical identities pass, followed
by24 genuine three-hop chains/72 wire hops. Their authored late-after-finished
observation gate remains unexercised. Causal review corrects the earlier inference:
the original captured defect proves stale receipt reuse, not an observed exact
raw-after-finished20f ordering. Legal late receipt/migration behavior has native
component coverage; no extra live late-order pass is claimed. All old failures,
original gate and raw evidence are retained. Offscreen OOPIF full-page pixels and
the user-deferred custom CEF build remain open.

Previous896ed617 passed multipart wire/HAR/offline6 and held-body ownership3,
credential constructor3, independent request fixtures3, browser fixtures4 and OSR
window guard3. Its unchanged original request/response57 has47 expected outcomes
(including one expected Chromium failure) and10 unexpected; original API228 has
205pass/23fail/0skip with all25 native stderr logs empty. These results belong to
that exact1304+230 epoch. The worker cleanup and TimeoutError defects found there
are fixed and verified on5313; the broader suites have not been rerun wholesale.

The previous20f979c1 epoch passed all15 authored query scopes and unchanged original
selectors70/70 and evaluation100/100, with empty native stderr, final scoped
ownership0 and1291+230 unchanged hashes. Actual25000-node transfer is4680.60ms
versus8767.33ms on e078; the original120s budget and separate1ms cancellation
check are unchanged. The largeDOM original passes its existing30s budget;
eval814 author intrinsic replacement passes. Common browser fixtures4 and HTTP
observation3 also pass. These remain evidence for that exact earlier epoch.

The following evidence belongs to the previous sealed `e4026569` epoch:

All131 native checks and556 SDK tests in74 files pass, with1271 hashes+230 assets.
Combined OOPIF7, isolated UTF16/utility2, genuine two-cycle OOPIF crash/reload
recovery, Text-node pointer7, selectText8, custom-selector action isolation8 and
HTTP redirect/CN/ancient-expiry5 pass with empty native stderr and final scoped
ownership0. Original click14/14, scroll9/9 and separately collected selectText5/5
pass; the initial selectText collection omission remains retained. The authored
adapter now rejects unknown requested filenames and checks actual declarations.

It adds flat native runner hooks and native exact-ID selection/sharding,
Text-node pointer geometry/scrolling, selectText, isolated custom-selector action
readiness, HTTP UTF8 Location/redirect Host/first certificate CN handling, and
reconciliation of genuinely completed coverage cleanup before immediate restart.
The common CTest run includes18 real hook groups and9 real native/CLI/API
selection groups. No full upstream runner parity is claimed. Native HTML and
explicit unrun declaration reporting were already compiled in e0780f2c.
The sealed manifest's descriptive scope and one hooks gap sentence were copied
from an older template; immutable hashes/logs/counts are correct. The correction
is recorded without changing the epoch in
`.suitecut/diagnostics/native-e402-seal-metadata-addendum.json`.

The previous sealed `e0780f2c` has1249 hashes+230 assets,123 native checks and552
SDK tests. All12 authored query scopes pass, including prototype isolation10,
actual25000-node transfer3, retained-handle waits10 and grammar99. DNS5 (including8
simultaneous distinct destinations), HTTP lifecycle5, managedHTTP10, HTTP8,
scrolling7, raw headers3, core network13 and body/session4 pass with empty native
stderr and zero final scoped ownership. Inputs remained unchanged.

Original selector70 is69pass/1fail: nested :scope now passes, while large DOM
selection times out. Original evaluation100 is99pass/1fail after author replacement
of Object.getOwnPropertyDescriptor. Both regressions remain under repair.
Original request/response57 is45pass/12fail/0skip. Original HTTP228 is195pass/33fail/0skip with corrected localhost, dual-stack and404
fixtures plus explicit test-only mapping of the private lookup hook to the genuine
public DNS API. Engine and fixture changes make this a separate baseline from811.
OOPIF coverage passes its first5 groups, isolated UTF16/utility2 and genuine
two-cycle crash/reload recovery, but immediate restart exposes a readiness race.
That concrete fix is compiled in e402; its full7-group live proof now passes.
The initial pre-start diagnostic helper required fields before lazy controller
creation; the corrected authored input and initial failure are separately retained.

The following evidence belongs to the previous sealed `811785a9` epoch, whose
1210 hashes and 230 assets remained unchanged during its scoped proofs:

- Nine authored query scopes pass: grammar91, shadow4, frame-owner8, queries53,
  worlds9, mixed11, semantic14, wait13 and retained13. Native stderr is empty and
  reported scoped counters finish at zero. Original selector70 is69pass/1fail;
  the remaining nested :scope case motivated the new build.
- Managed HTTP10 and HTTP8 pass, including the repaired actual storage-export
  deadlock. Separate real filesystem-fault trace-disposal and held31000ms
  zero-deadline proofs pass. Original HTTP228 is170pass/58fail/0skip, with all60
  native stderr logs empty; its older fixture used127.0.0.1. New runs will record
  corrected pinned localhost/dual-stack/404 fixture contracts separately.
- Hover8 and unchanged mouse/handle-misc26 pass with empty stderr. The initial
  authored bare :hover failure was a BackCompat fixture issue; standards-mode
  evidence and the original failed fixture are both retained. Original handle
  click14 is12pass/2fail, exposing real Text-node and zero-area diagnostic gaps.
- Coverage9/ownership6/init-registry3 and active-trace4/API-trace5/rawdirs5 pass.
  Real active-recording crash/reload3 and pending-start crash6 proofs also pass;
  they do not establish OOPIF coverage on811.
- HAR metadata4/capacity3/held-ownership3/public7 pass with empty stderr.
  Context HAR6 passes functionally but FAILS the strict diagnostic check because
  of an actual Fontconfig warning. That failure remains open.
- Original request/response57 is43pass/14fail/0skip with the corrected server
  fixture. An earlier ENOSPC-interrupted attempt has no aggregate result and is
  retained separately. An actual favicon race and a CORS page-close failure
  remain distinguishable from observed-header correctness.

These scopes overlap and are not a grand parity total. Full feature parity is
unfinished; the user-deferred custom CEF build remains pending.

The following67 evidence is historical and is not a rerun on either later epoch.

Historical sealed `67c1077e` passes112 native CTests and527 SDK tests in68 files,
plus native/SDK builds, typecheck, lint and diff checks. Its immutable inventory
records1199 source/SDK/test/script/object/evidence hashes and230 CEF assets.
Actual command statuses and complete unchanged source inputs are recorded.

Combined selector grammar76 and six selector/world/retained regression scopes
pass; original selector70 improves to65pass/5fail/0skip. Native reconnect9,
Source11 and public wrapper proofs pass with captured stderr0 and independently
decoded media. Active raw trace4/APItrace5/raw5 pass with independent decoding;
timed console/network events publish without another browser command. Public
HAR7 and real held-install ownership3 pass with stderr0. These proof families
audit1199+230 inputs unchanged.

Context HAR6 passes its functional groups but FAILS the strict diagnostic check
because of a real sandbox Fontconfig warning. Managed HTTP profile verification
FAILS at associated request.storageState with a genuine CEF same-parent-list
publication deadlock and forced shutdown. The repaired publication and actual
CEF regression await the next shared build; HTTP228 is held until that repair.
Independent HTTP8 passes with stderr0/exit0 and zero actual scoped counters.
Initial authored trace-row and HTTP diagnostic-channel verifier errors are
preserved alongside their corrected checks.

Unchanged tracing43 remains37pass/3fail/3originalskips and coverage17 remains
16pass/1fail/0skip on67. Remaining private trace paths/stack contracts and CSS
navigation retention are open. Current OOPIF coverage explicitly rejects.
Unchanged pointer90 completes89pass/0fail/1originalskip with1199+230 hashes unchanged. Native runner SIGINT/SIGTERM now preserve actual native
130/143 status and physical worker cleanup; hooks/projects/parallel/built-in
browser fixtures remain unfinished. Full parity and the user-deferred CEF build
are unfinished. See the native audit for exact evidence paths.

The following ca7 evidence is historical and is not a rerun on67c1077e.

Historical sealed `ca7ebe32` passes102 native CTests and522 SDK tests in68 files,
plus native/SDK builds, typecheck, lint and diff checks. Its manifest records1174
source/SDK/test/script/object/evidence hashes and230 CEF assets, including the
public native runner executable, build logs and copied integration record. Actual
command exit statuses and the complete source inventory were checked. The public
runner12 authored actual-process groups pass in CTest; unchanged upstream runner
parity is not claimed. Combined browser proofs and original reruns are tracked separately below.

Combined ca7 selector wait13/retained13/query53/world9/mixed11/semantic14/rawAX9
and independent actual-DOM CSS9 pass with stderr0 and zero final reported counters.
All five recovery/crash verifiers and full first-author settings across two crashes
pass with stderr0, repairing the prior d9 speculative-loader diagnostics. Both
families recheck all1174+230 hashes unchanged. The public runner exports/package
CLI also pass2 focused real-process groups beyond the12-group CTest.

Combined coverage9/ownership6/init-registry3 and APItrace5/raw5 pass with stderr0,
independent archive/raw decoding and unchanged1174+230 hashes. Public HAR7 passes
without diagnostics; unchanged HAR10 remains4pass/6fail/0skip. Its remaining
metadata/compression/favicon-index assertions are not evidence of lost POST bytes.
Combined HTTP8, permission8 and first-author lifecycle20 plus cross-origin clear/regrant pass with empty stderr/helper exit0. Original selector70 improves to37pass/33fail/0skip. Original tracing43 improves to37pass/3fail/3originalskips with actual native HTTP fixtures and unchanged inputs. Remaining tests involve private stack/default-trace/artifact paths and active flush. Original coverage17 is16pass/1fail/0skip, with only the explicit CSS navigation-retention rejection. Original HTTP228 confirms151pass/77fail/0skip on ca7 with empty native stderr and unchanged inputs.

The build integrates expanded HTTP, public HAR, current-document native coverage,
retained-root selectors and ACK-owned cleanup, crash-bootstrap diagnostics repair,
and trace resource/format fixes. Pending separate stages are independent managed
HTTP profiles, context-wide HAR, publisher reconnect and runner signal handling.
Full parity and the explicitly deferred CEF vendor build remain unfinished.

The following d9 evidence is historical and is not a rerun on ca7ebe32.

The sealed `d9a25e6c` build passes92 native CTests and522 SDK tests in68 files,
plus native/SDK builds, typecheck, lint and diff checks. Its immutable manifest
records1095 source/SDK/test/script/object hashes and230 CEF asset hashes.
Native child reload recovery, atomic selector waits, raw trace exports, automatic
preflight public events, fail-closed permission scopes and runner components are
integrated. Live API tracing5/raw exports5/preflight4/session migration4 pass on
the sealed files with stderr0; API/raw input audits retain all1095 hashes.
Raw files export at stop boundaries and early preflight routing remains open.

A broader first-author context-settings probe preserves actual configured behavior
across two crash/reloads, but fails strict stderr on native URL-loader NOTREACHED
messages from recovered renderers. The no-crash control is clean. The errors remain
visible. Recovery behavior groups pass with those diagnostic failures preserved;
public crash-only and throwing-listener controls have stderr0. Permissions8,
first-author lifecycle20 and cross-origin storage access pass with stderr0 and
unchanged inputs. Original selector70 improves to35pass/35fail/0skip. Complete unchanged tracing43
is34pass/6fail/3originalskips with all global requests now native. The earlier
35035 global request tracing cases used the oracle HTTP backend; their old pass
count is not native API evidence. Current gaps include actual POST-body resource
references, format differences and private fixture contracts. Permission reruns
are in progress. Public HAR, coverage, HTTP expansion, runner CLI and publisher
reconnect remain separate work in progress.

Previous immutable f0 evidence follows; it is not a rerun on the new binary.
The sealed `f0ce508d` build passes83 native CTests and520 SDK tests in67 files,
plus native/SDK builds, typecheck, lint and diff checks. Its immutable manifest
records993 source/SDK/script/object hashes and230 CEF asset hashes. Actual native
permission/lifecycle scopes pass8/20, and selector metadata/eval/query/world/mixed
engine/semantic scopes pass5/6/52/9/11/14, retaining the original sealed hashes.
These are separate scopes. Native time budgets retain the explicit PW-25 limits.

Complete unchanged geolocation/permissions32 reports26pass/2fail/4originalskips;
all nine geolocation cases and both clipboard cases pass. Remaining failures are
opaque-document storage permission and additional actual native network events.
Original selector70 improves to24pass/46fail; native Page.waitForSelector and scoped
ElementHandle queries remain under implementation. Full unchanged HTTP API228
reports99pass/129fail with actual native request contexts throughout. Cookie31 is
blocked by the unsupported ignoreHTTPSErrors option; HTTP options, lifecycle,
TLS/proxy, metadata and storage support are the concrete backlog.

The previous sealed35035 tracing43 reports35pass/5fail/3originalskips, including
API request tracing. Raw directories, automatic-preflight event filtering, storage
origin pairs, child reload recovery and native fixture/worker components are now
in working source with private evidence; they await a combined native build.
Standalone integrated runner components pass five actual-process CTests. Public
runner declarations/CLI, hooks, scheduling breadth and reporters remain unfinished.
Public HAR recording/replay and native coverage are under implementation.

Earlier immutable evidence includes core595/596 (directory upload fails),
evaluation100/100, assertions481/481, ARIA79/79, handles34/34, fill46/46,
workers23 plus one original skip, storage22/23, and request/response44/57.
Independent WebDriver, Puppeteer, WPT, chromedp, Rod and CEF scopes retain their
own exact hashes and harness limits in the [native audit](../native/browser/AUDIT.md).
They are not counted as reruns of this latest binary or combined into a total.

The unchanged three-rate media continuity/pulse scope passes on sealed72 during
an unthrottled quiet window. Concurrent CPU-quota pressure produces genuine
zero PCM at CEF ingress; loaded failures and causal controls remain recorded.
The actual aborted-navigation Fontconfig warning is traced to sandboxed config
access returning EPERM and remains visible. Custom CEF directory upload,
authenticator synchronization, popup settings and related vendor hooks retain
the user's deferred build. Whole-browser sanitizers remain blocked at CEF
initialization; component sanitizer results keep their explicit scope.

## Cross-cutting behavioral acceptance coverage

| Area       | Behavior to specify and verify                                                                   |
| ---------- | ------------------------------------------------------------------------------------------------ |
| Lifecycle  | Launch failure, disconnect, crash, repeated close, pending-work closure, resource cleanup        |
| Navigation | Redirects, interruptions, history, hashes, certificate errors, stalled resources                 |
| Locators   | Zero/multiple matches, changing DOM, detached elements, text normalization, shadow roots         |
| Frames     | Late creation, cross-origin/nested content, detach, hidden frames, ambiguity                     |
| Input      | Covered/moving targets, modifiers, Unicode, focus, event order, scrolling                        |
| Forms      | Labels, disabled/read-only controls, radio/checkbox state, option matching, uploads              |
| Evaluation | Serialization, promises, exceptions, callback failures, destroyed contexts, disposed handles     |
| Network    | Redirects, repeated headers, cookies, cache, service workers, aborted/failed requests            |
| Routing    | Precedence, fallback, overrides, active-handler removal, popup requests                          |
| Storage    | Origin isolation, expiry/partitioning, export/restore, replacement semantics                     |
| Clock      | Timer order, intervals, paused time, jumps, callback exceptions, popup consistency               |
| Capture    | High DPI, full-page bounds, unstable layout, finalization, FPS validation, distinct-frame limits |
| Assertions | Eventual success, timeout diagnostics, negation, soft failure, snapshots/diffs                   |
| Runner     | Fixture teardown, hook errors, retries, worker replacement, serial groups, locks, sharding       |
| Artifacts  | Retry identity, parallel path isolation, retention, reporter attachment fidelity                 |

Upstream tests also exercise WebGL, WebAssembly, media playback, image formats,
clipboard behavior, and service workers. These validate browser/site compatibility
under specific configurations, not a guarantee that every engine implements every
web-platform feature. Record the browser revision and platform for such evidence.

## SuiteCut-specific native media acceptance criteria

These extend the reference inventory. Track them separately from Playwright
matching; do not use a constant-rate output file as evidence of distinct capture.

- [ ] Full native build and runtime checks using the exact pinned Linux x64 CEF beta.
- [ ] Capture/output configuration accepts every integer FPS from 1 through 120.
- [ ] Requested paint rate, observed callbacks, distinct frames, and encoded FPS are measured separately.
- [ ] Capture timestamps and drop counters remain observable.
- [ ] Independent capture/output rates resample causally by holding/dropping, without interpolation.
- [ ] Fractional audio samples per video tick use exact whole-sample boundaries without accumulated drift.
- [ ] Backpressure has bounded pending work and does not create an unbounded capture queue.
- [ ] End-of-recording and final-render cadence are verified through decoded output.
- [ ] Live browser 120 FPS evidence reports workload, dimensions, duration, distinct decoded frames, and drops.
- [ ] Native page audio, A/V synchronization, and long-run clock drift have runtime evidence.
- [ ] RTMP/RTMPS publishing, source switching, failure propagation, and shutdown have runtime evidence.
- [ ] Existing synthetic FFmpeg checks are linked as resampling/encoding evidence, with live-browser limitations recorded.

Current evidence belongs in the [native audit](../native/browser/AUDIT.md). This
checklist is initially unassessed; it does not replace or invalidate those reports.

## Versioned sources

Use these versioned sources when resolving ambiguous behavior. Upstream tests
are behavioral evidence; their implementation is not part of this document.

- [Playwright 1.64.0 release](https://github.com/microsoft/playwright/releases/tag/v1.64.0)
- [Public browser/API documentation](https://github.com/microsoft/playwright/tree/v1.64.0/docs/src/api)
- [Actionability](https://github.com/microsoft/playwright/blob/v1.64.0/docs/src/actionability.md)
- [Locators](https://github.com/microsoft/playwright/blob/v1.64.0/docs/src/locators.md)
- [Screencast API](https://github.com/microsoft/playwright/blob/v1.64.0/docs/src/api/class-screencast.md)
- [Test-runner API](https://github.com/microsoft/playwright/tree/v1.64.0/docs/src/test-api)
- [Reporter API](https://github.com/microsoft/playwright/tree/v1.64.0/docs/src/test-reporter-api)
- [Component testing](https://github.com/microsoft/playwright/blob/v1.64.0/docs/src/test-components-js.md)
- [MCP documentation](https://github.com/microsoft/playwright/blob/v1.64.0/docs/src/getting-started-mcp.md)
- [WebMCP API](https://github.com/microsoft/playwright/blob/v1.64.0/docs/src/api/class-webmcp.md)
- [Page behavior tests](https://github.com/microsoft/playwright/tree/v1.64.0/tests/page)
- [Browser/library tests](https://github.com/microsoft/playwright/tree/v1.64.0/tests/library)
- [Runner tests](https://github.com/microsoft/playwright/tree/v1.64.0/tests/playwright-test)
- [MCP tests](https://github.com/microsoft/playwright/tree/v1.64.0/tests/mcp)
- [Electron API](https://github.com/microsoft/playwright/tree/v1.64.0/docs/src/electron-api)
- [Android API](https://github.com/microsoft/playwright/tree/v1.64.0/docs/src/mobile-api)

The reference version/commit is frozen for matching. Update it deliberately and
record changed behavior before claiming compatibility with another release.
