# Active native browser backlog

Browser control only: PW-01–PW-31, plus native debugger and locator-generation APIs
referenced by PW-37. PW-32–PW-42 test/tool/platform families are outside this task.
This list separates remaining implementation and evidence work; it is not a parity score.

**M** = recorded implementation gap; **U** = implemented/unmapped behavior needing
specific evidence, not a proven missing API; **D** = backend/platform or explicitly
deferred vendor work; **H** = composed implementation with the specific acceptance limits below. The [matching register](../playwright-feature-parity.md#matching-register),
[browser README](../../native/browser/README.md), and [audit](../../native/browser/AUDIT.md)
retain the actual evidence and original negatives. Where their historical wording
differs from a newer private handoff, neither is silently upgraded or erased.

| Family | Bounded remaining browser work | Class |
| --- | --- | --- |
| PW-01 Lifecycle | CEF/Linux remains the target. Wider engine/channel, remote-attachment and platform inventory needs a separate capability decision; do not infer it from Chromium. | U/D |
| PW-02 Contexts | Broader settings/lifecycle coverage; opaque-origin startup permissions, first-author popup policy and privileged-navigation startup history retain backend constraints. | U/D |
| PW-03 Pages/popups | Native activation and close with before-unload are frozen below. Broader popup/navigation combinations retain existing ownership. | H/U |
| PW-04 Navigation | Redirect/error and readiness combinations beyond the recorded history/HTTP-error scopes. | U |
| PW-05 Frames | Broader same-process/OOPIF element actions and transformed-frame combinations; private upstream inspection hooks are not new browser API requirements. | U |
| PW-06 Locators | Integrate frozen native suggestions/picking below; remaining locator-family combinations still need mapping. | H/U |
| PW-07 Actionability | addLocatorHandler/removeLocatorHandler first slice is composed below. Retained-handle, normalized and native snapshot-reference registrations plus selectText triggers are implemented. Audited callback I/O has native retirement tracking. Opt-in direct CEF input remains excluded; handler-driven recovery of another crashed child page is implemented. | H/U |
| PW-08 Pointer/touch | Broader touch/action combinations beyond the recorded trusted-input/drag/scroll scopes. | U |
| PW-09 Keyboard | Targeted press/type queue admission and retained-handle metadata fixes are frozen below. Remaining platform/input combinations retain prior evidence. | H/U |
| PW-10 Forms | Populated file-input reset needs genuine vendor support; retain the original empty-ACK negative. Broader form combinations are evidence work. | D/U |
| PW-11 Upload/drag | Directory upload/original-widget delivery retain deferred vendor hooks; preserve true drop/cancel receipts. | D/U |
| PW-12 DOM/state | Broader inspection/geometry combinations; existing getters/state waits are not missing because the checklist is unchecked. | U |
| PW-13 Evaluation | Broader serialization policies and lifetimes beyond recorded page/handle evidence. | U |
| PW-14 Events | Broader event/cancellation combinations, preserving original deadlines and actual callback retirement. | U |
| PW-15 Dialogs | Native close attempts now retain page ownership after before-unload dismissal in frozen source below; genuine before-unload dismissal/approval passed the bounded live check below; broader dialog predicates remain unrun. | H/U |
| PW-16 Downloads | Remaining routed-download negatives need causal classification; do not declare all download behavior missing. | U |
| PW-17 Network | Unresolved original request/frame/favicon attribution and redirect ExtraInfo boundaries; a later narrow diagnostic is not a fix for the original failures. | U/D |
| PW-18 Routing | Frozen page/context route.fetch below; worker-owned route.fetch is also frozen below, with pre-Runtime bootstrap pauses explicitly unsupported. Route URL predicates are synchronous as specified; request/response waits already support async predicates. Original CORS ordering negatives remain separately recorded. | H/U |
| PW-19 HAR | Frozen multipart matching below needs integrated replay acceptance. Existing Context HAR removal is not a new implementation task (owner source clarification); old ordering/cardinality negatives stay open. | H/U |
| PW-20 WebSockets | Page/context URL callbacks, mocking, message modification and target inheritance are implemented against the isolated transport contract. Stock CEF lacks that contract; independent server-close interception still needs a backend event. Observation remains separate. | D/U |
| PW-21 HTTP API | Classify remaining TLS-environment/remote-CDP/branded-UA/socket-diagnostic cases by their real dependency; they are not all missing native HTTP operations. | U/D |
| PW-22 Storage/auth state | Concurrent credential ceremonies retain vendor requirements; broader restoration/permission combinations remain evidence work. | D/U |
| PW-23 Credentials/permissions | Captured-surface control uses the actual setting. Ordinary MIDI overrides are rejected because pinned CEF traps on that unregistered type; SysEx remains supported. Clipboard-write/screen-wake-lock are intrinsic browser capabilities, not controllable overrides. Other client-certificate combinations need backend mapping. | H/U/D |
| PW-24 Emulation | Viewport/screenshot metrics and native page idle override/reset passed the bounded root-page live check below; OOPIF acceptance remains unrun. | H/U |
| PW-25 Clock | Document-scoped controller ownership is implemented. Context/OOPIF/worker isolated scheduler propagation, RAF/idle control, realtime resume and fast-forward require actual backend scheduler primitives; process wall time is explicitly renderer-process scoped. | D |
| PW-26 Workers | Same-target reattachment, new generations and stale-handle rejection exist with historical scoped proof. Broader interception acceptance remains separate; debugger work below retains actual session/generation retirement. | H/U/D |
| PW-27 Accessibility | Opaque reference spelling/rawAX-vs-DOM differences need compatibility decisions; broader snapshot combinations need evidence. | U/D |
| PW-28 Screenshots | Full-page screenshot metrics restoration is frozen below. Preserved-viewport offscreen OOPIF raster remains a backend gap; broader native capture/convergence coverage stays separate from out-of-scope snapshot baseline updates. | D/U |
| PW-29 PDF | Remaining print/platform variations are unverified, not presumed absent. | U/D |
| PW-30 Media | Context video recording and Video file methods are composed below. Launch/default persistent-context recording and dynamic viewport/odd video canvases are implemented in the source pass below; the matching native binary and SDK build passed and are installed; live acceptance remains deferred. Sustained delivery/load/audio reliability is unestablished; narration/rendering orchestration is SuiteCut product scope. GPU sharing/hardware encoding remain backend work. | H/U/D |
| PW-31 Diagnostics | Broader browser trace retention and coverage evidence; resetOnNavigation:false retains a demonstrated backend limitation. Runner tracing/report exports are outside this task. | U/D |

## Frozen browser handoffs

This table preserves the original pre-composition source handoffs and their
then-current verification boundaries. The current integration evidence below
records subsequent build and live results; these rows are not the latest status.

| Handoff | Exact frozen source | Remaining boundary |
| --- | --- | --- |
| Page/context route.fetch | `f9b691c7078e18d4a4fbab74fd320e405a90a201` | Matching Source/live HTTP acceptance unrun; ownerless workers must reject. Worker extension is not part of this frozen handoff. |
| HAR multipart matching | `e166ce7139921b2322d4bc7b7c8d869946569bc2` | Strict boundary-aware byte equivalence; malformed/unsupported MIME stays rejected. Integrated replay acceptance unrun. |
| Dedicated-worker debugger | `b7c2f0692a64dea0ad0aa4372743141645a99c7e` + `829bfeae4cd645e7ca577cba8dd5b24d2d117912` | Native inventory/source, observed pause evaluation and supported breakpoints; live Inspector acceptance unrun. Unsupported agent pause/step/shared-service debugger capabilities remain explicit. |
| Retained-node suggestions / point picking | `96197a9f55c774e896e9c09f26f16ff37ce43783` | Native ranking and same-node uniqueness, document/frame/handle ownership; live getNodeForLocation/OOPIF/quad behavior unproved. No JS hit-test fallback or future-DOM uniqueness guarantee. |
| Worker-owned route.fetch | `34b08267432faca4b0386f7acd5b05784867e9e2` on `0b5f8a73` | Actual context/session/execution-context ownership with cancellation; rejects pre-Runtime bootstrap pauses. Live worker HTTP unrun. |
| Page activation | `430f79accf327d239768bc41be01ee5cb5da1715` | Real retained root Page.bringToFront command; live CEF/OS focus unrun. |
| Viewport and screenshot metrics | `7863c0f51444c427ff4e13c2dcaec75dac4928e1` + `771f7c1ebbc27fd426a3c00c907da71d4eaeae4b` | Native emulation policy preserved across resize and temporary screenshot capture/restoration; live raster acceptance unrun. |
| Targeted keyboard ownership | `06c5d1fc682026e1fadf02b7a4a63c01d270b81c` on `80a9a455` + `84a276f` | Queue admission precedes focus; retained target metadata and real callback/key-release ownership. Live focus/event delivery unrun. |
| Native permissions | `32003f6d675c63093da40d92ee87b6df3c6d8b7d` + `cc884e974f0c7b9e23a34efd1bbdaab3d55b098b` | Superseded for ordinary MIDI by correction `7308ec5`: type27 is not registered in the actual CEF156 library and caused a real grant crash. No ordinary-MIDI setter call remains; explicit overrides reject before mutation. SysEx13 and captured-surface97 remain. |
| Before-unload close | `106d53439ddf8b1003b78116a0311e86ed8eda81` + test-only `3da2dc40b921fcf27e462babd0022491d615ff3e` | Dispatch ACK is distinct from actual closure; dismissed real dialog must leave page live. Native controls/SDK projection pass, actual CEF close unrun. |
| Idle emulation/reset | `d376b164e99c97440781c9b64e9dd8ddbe8c734f` on `fdf9af3` + `08ad64a` + `ace2d953` | Real root/OOPIF command serialization and acknowledged inheritance, with physical cancellation retention. Live CEF support unrun. |
| Native locator handlers | `7133d49` + `3f6e708`, ledger/policy prerequisites, SDK `76a1180` + `8fcbf19` | Native invocation ownership, real callback retirement, original action deadline and hidden wait; lazy locators only. Callback I/O and direct CEF/drag transport explicitly rejected. Focused native/SDK checks pass; combined Source/live acceptance pending. |
| Context browser video | `d9364fd` + `ec0c3c8` + `9421f87`; native `ce5db2b` through `6d5d8dc`, controller through `6a39409` | Automatic native VP9/WebM, actual page/context finalization, surviving Video handles and native path/saveAs/delete. CSS fit, no upscale and top-left padding. Explicit canvases must be even; launch/default-context recording unsupported. Focused native/SDK checks pass; matching binary/live acceptance pending. |
| DevTools scalar processing | `e50e3fbdc17ce1a7c5e6a564da313d6a959a5130` | Removes per-token copies and duplicate decoding; no measured latency claim. |

Next work should close a named **M** gap or compose a frozen **H** handoff before
expanding scope. **U** does not authorize broad replay; **D** does not authorize a
custom CEF build. Preserve original deadlines, ownership and physical cleanup.

## Composition checkpoint

All handoffs above are composed in the browser-only checkpoint. Integration
preserves targeted keyboard admission/retirement, before-unload pending ownership
and the distinct native handler invocation provenance. The combined browser-only
build and bounded live check passed as recorded below. The installation receipt
in `.suitecut/diagnostics/native-browser-control-integration-20261016.json` records
the installed binary and exact source checkpoint. Test-runner parity remains
outside the requested scope.

## Previous integrated binary evidence

The composed browser binary at `e6ced009` linked successfully after a GCC-only
compiler heap correction; ABI, runtime optimization and warning settings stayed
unchanged. Its bounded live scopes proved activation, mobile metrics and full-page
capture restoration, native overlay dismissal/removal, and genuine before-unload
dismiss/accept with a separate page remaining usable. That binary also exposed a
real permission-grant SIGTRAP before idle execution. Native diagnostics identified
unregistered ordinary MIDI content-setting type27. The correction rejects that
override, retains SysEx, and removes unsafe implicit-deny writes. A debug-only
filtered probe exercised idle set/start/clear but is not production acceptance.

The combined `bf480072` binary built successfully and passed the unshimmed live
scope: native MIDI rejection, idle set/start/clear, activation, mobile viewport
and screenshot restoration, overlay dismissal/removal, and actual before-unload
dismissal/approval with an independent page remaining usable. Three real VP9/WebM
videos (page, popup, immediate close) passed finalized path/save/delete, identical
copy bytes, dimensions/frame counts and decoded top-left/no-upscale padding.
Helper exit and the scoped process retirement passed. These are bounded checks;
route-fetch/HAR/worker/locator combinations retain their separate evidence limits.

The verified code checkpoint is `bf480072`; later checkpoint edits only record
these results and documentation limits. Earlier build interruptions and the
original permission crash remain preserved. Native ordinary MIDI override,
WebSocket interception, broader clocks, old-document coverage and the explicitly
deferred custom CEF upload/reset build remain unavailable. Overlay callback I/O,
video on the launch/default context, odd video canvases and matching dynamic
resized-viewport video geometry remain implementation gaps.

## Browser SDK implementation pass — 17 October 2026

This pass implements browser control only. Broad E2E and fuzz testing are deferred
by user direction. The consolidated native binary and browser SDK from
`a084e6e` now build and are installed together. The build/installation receipt is
`.suitecut/diagnostics/native-browser-build.json`; the earlier source-application
receipt remains `.suitecut/diagnostics/native-browser-sdk-implementation-20261017.json`.

- `suitecut/browser` exposes browser-control classes and types without public
  test-runner/assertion/fixture exports. `launchBrowser`/`NativeBrowserController`
  own the actual process and use native atomic page/context creation.
- Launch/default/persistent-context video recording uses genuine native
  preparation, startup deadlines and ready metadata, including popup inheritance.
- Automatic videos accept odd canvases and fit real current viewport paints on
  the encoder worker, including fractional-DPR CSS extents. The active recording
  index avoids scanning retained terminal metadata on every paint.
- Locator handlers accept retained element handles and normalized locators;
  selectText invokes handlers. Callback identity lookups use indexed maps.
- HAR/trace finalization waits for real unfinished header receipts under its
  original bounded deadline, rather than freezing an earlier partial snapshot.
- Native packet writing uses scatter/gather without concatenating header/payload
  or polling before writable output; partial writes retain actual byte receipts.
- Repeated clock pause avoids redundant renderer IPC only after earlier work and
  an acknowledged paused state. Broader clock capability flags remain unchanged.
- Default native builds exclude component-check executables and the optional test
  runner; explicit component targets remain available. Use
  `-DSUITECUT_BUILD_BROWSER_CHECKS=ON` to include the checks in the default build.

The composed Source syntax check, focused native compile checks, strict SDK
typecheck, production browser target compilation/linking, SDK emission and CMake
configuration passed. Installed dynamic libraries and the public SDK import
resolve. A compact packet-write component check
covered real pipe output, partial writes, cancellation and broken-pipe failure.
These checks are not end-to-end acceptance. Static SDK import-graph reduction is
not a measured latency gain. Broader clock controls, WebSocket interception,
ordinary MIDI overrides, old-document coverage and the deferred custom CEF
upload/reset build remain isolated vendor work. Audited handler callback I/O and
snapshot registrations are implemented in the current stock-CEF pass.
Historical route/HAR/frame/favicon negatives retain their separate evidence.

## Current stock-CEF completion pass

User direction isolates CEF fixes and builds the remaining native browser SDK;
fuzz testing stays deferred. Audited handler I/O now tracks real async callback,
background-job and retained-state lifetime; native snapshot registrations avoid
resident group borrows and never rebind stale identities. Successful recording
and stream startup transfer to their resident native owners; canceled startup
and stop retain actual encoder retirement. Permission callbacks deny invalid or
opaque wildcard origins, closing scopes and empty permission masks.

`browser.capabilities()` reports the executable's actual vendor-dependent
capabilities without an extra command. The stock backend continues to report
its unsupported vendor features as false. Eight CEF/Chromium/V8 patch candidates
are isolated in [the vendor bundle](../../native/browser/cef-patches/README.md),
with one ordered pinned-source check. Candidate applicability does not establish
a rebuilt vendor library, complete clock semantics, surface synchronization,
production adapters or runtime acceptance. No vendor patch is applied by the
ordinary build. The consolidated build result is recorded in
`.suitecut/diagnostics/native-browser-build.json`.

Handler exclusions remain opt-in direct CEF input, renderer recovery inside a
handler, unknown wire actions and out-of-scope test-framework screenshot
comparisons. Historical browser/network failures retain their recorded limits.

The stock completion pass compiled and linked the native browser target, emitted
the strict browser SDK, and installed both. All945 selected source inputs stayed
unchanged during the build. Dynamic libraries and the installed SDK import and
capability API export resolve. Broad E2E/fuzz and custom-library builds were not
run. The source bundle is buildable on stock CEF with the explicit handler and
backend limitations above; it is not a claim of complete browser parity.

## Native adapters and CEF reporting pass

The current source pass implements a root-target WebSocket controller and public
SDK with actual versioned backend probing, one-shot message decisions and retained
configuration/cleanup acknowledgments. Stock CEF rejects activation. Native file
mutations now serialize through actual receipts; genuine empty selection checks
the original input and rejects unsupported stock behavior. The authored reset
workaround is removed. Directory entry activation remains isolated and stock
calls still fail before dispatch. Cleanup failures request actual page closure.

Credential synchronization now recognizes only the exact pending import echo;
other overlapping ceremony events fail explicitly instead of silently losing
credential material. Capability metadata truthfully describes the unsupported
certificate-import, concurrent-ceremony and original-widget-receipt paths.
This historical pass compiled retained coverage source/identity helpers. The later
controller completion below supersedes its adapter and ownership gaps.

The combined vendor bundle contains ten patches across 40 pinned paths. New
candidates guard unregistered public CEF content-setting types and preserve exact
native original-widget mouse callbacks/leases. The latter retains detached held
or uncertain leases until actual widget death. Ordered source applicability passes
for the vendor bundle and the four separate native adapters. This establishes no
vendor compile, runtime behavior or completed browser parity.

Backend/implementation boundaries recorded before controller completion:

- Full context/frame/worker clock ownership, lifecycle inheritance, realtime
  pacing, fast-forward and animation/idle semantics. Process wall time is narrower.
- WebSocket page/context URL callback registration, popup/OOPIF/worker inheritance
  and independent server-close interception. The compiled adapter controls one
  exact root target.
- Coverage final unload/pagehide receipts and cross-agent, BFCache and renderer
  replacement barriers; the isolated adapter refuses unsupported activation.
- Confirmed child-before-root offscreen OOPIF surface synchronization.
- Context-local client-certificate/private-key import and genuinely concurrent
  credential ceremonies.
- Production original-widget input receipt consumers; the isolated backend
  primitive covers raw mouse events, with other input families still outside it.

Custom CEF compilation and genuine runtime activation remain deferred by user
direction; broad E2E/fuzz also remain deferred. The final native/SDK build receipt
is `.suitecut/diagnostics/native-browser-build.json`.

Two reviewed upstream CEF reports and standalone reproduction code live under
[docs/cef-issues](cef-issues/README.md). Ordinary MIDI raises SIGTRAP through the
named public CEF enum in the standalone probe, while the SysEx control returns.
Directory enumeration has actual protocol and native debugger evidence.
Publication was attempted but GitHub denied the integration write access; the
publication receipt records this separately from report preparation.

The consolidated browser and strict SDK for this pass compiled and installed from
`746bdb7`. All 963 selected source inputs stayed unchanged; the installed
SDK emits 220 files and its 43 public runtime exports import successfully.
Dynamic libraries resolve, and the native/SDK supervisors retired their actual
descendants without cleanup failures. No custom CEF build or broad E2E/fuzz was run.

## Controller completion — 17 October 2026

The subsequent source pass closes the recorded SuiteCut controller gaps:

- Page/context WebSocket URL registration, callback dispatch, one-shot message
  decisions, removal/wait ownership, and popup/OOPIF/supported-worker inheritance. Target
  readiness uses actual backend acknowledgments. Incoming payloads use bounded
  binary pushes; outgoing payload staging preserves the 64 KiB wire-frame bound
  while supporting one actual decision for messages up to 2 MiB. Setup failures
  close the actual owner; service-worker bootstrap that requires an early author
  resume is rejected while a policy is active.
- Production retained-coverage coordination, final-event identity validation,
  target joins and cancellation cleanup. The old manual coverage adapter is
  removed. Activation requires the actual final-lifecycle backend contract;
  older isolated coverage candidates do not satisfy it.
- Explicit original-widget raw mouse control and SDK selection, gated on an
  actual read-only backend capability probe. Leases, physical acknowledgments,
  document identity and button cleanup retain the original native owner.
- Exact clock-document ownership and invalidation; renderer-process wall-time
  scope is explicit. Credential queues skip duplicate synchronization only after
  the canonical material and earlier acknowledgment agree.
- Locator-handler recovery of another crashed child page retains real renderer
  and document readiness or actual closure. Direct void CEF input still lacks
  physical acknowledgments.

The remaining implementation dependencies are in CEF/Chromium/V8: isolated
frame/worker clocks and scheduler barriers; final coverage lifecycle and paused
renderer-handoff receipts; independent WebSocket server-close interception and authenticated renderer-agent
replacement handoff and service-worker pre-author startup barriers;
child-before-root offscreen surface synchronization; context-local private-key
import and atomic concurrent credential ceremonies; and input receipt primitives
for additional input families and shared-widget subframes. Directory/reset and
ordinary MIDI support also require a rebuilt vendor library. These backend gaps
are distinct from later E2E/fuzz evidence and historical unclassified cases.

All vendor modifications remain isolated. The authoritative native/SDK build and
installation receipt is `.suitecut/diagnostics/native-browser-build.json`; source
implementation alone does not establish runtime acceptance.

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

The subsequent SDK/native hardening build from `87d3e513` is installed. Seeded
fuzzing, native component sanitizers, 773 browser-control tests and final installed
live probes passed. [The hardening report](browser-sdk-hardening.md) records exact
scope, measured memory/performance changes and remaining validation limits.
