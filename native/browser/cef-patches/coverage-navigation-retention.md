# Coverage retention across document destruction

Status: isolated pinned vendor candidates, **not a compiled or verified CEF
repair**. The ordinary SuiteCut binary continues to reject
`resetOnNavigation: false`. The production native/SDK adapter is implemented and probes the actual backend
contract. These vendor candidates supply no installed library or verified final
lifecycle receipts. The custom backend contract, build and runtime verification
are still required before retained navigation coverage can succeed.

## Exact source pins

- CEF: `47dbc1df023e99f006fb9e850a8d83cc19b4fdd9`.
- Chromium: `156.0.8078.17`.
- V8 from that Chromium's DEPS: `4b33f15d4d172248a95629d6aa9917b20a79ad76`.

The [CSS manifest](cef156-css-navigation-retention.json) and
[JavaScript manifest](cef156-js-navigation-retention.json) record every exact
base/output source hash, upstream URL, and patch hash. Both patches apply from
the **Chromium source root**; the JavaScript paths include `v8/`. The patches
must be applied to the original pinned files, rather than stacking the revised
CSS candidate on top of its previous revision.

## JavaScript candidate: real V8 counters before context removal

[`cef156-js-navigation-retention.patch`](cef156-js-navigation-retention.patch)
adds these opt-in Profiler commands, with explicit capability revision 1:

- `getNavigationCoverageCapabilities`: reports revision and capture bounds.
- `startPreciseCoverageWithNavigationRetention`: enables accurate counts and
  optional block coverage under one exclusive isolate owner.
- `stopPreciseCoverageWithNavigationRetention`: samples current counters and
  stops atomically, returning the retained typed measurement chunks.

The hook calls `CollectPrecise` synchronously from actual
`V8InspectorImpl::contextDestroyed`, before `contextCollected` erases the actual
context/group mapping or reports context destruction. A typed chunk preserves
its sequence, the actual destroyed context ID, timestamp, and measured scripts.
Each script includes its genuine execution-context ID, original source string,
and the engine's real script ID, URL, function names, offsets, counts and block
coverage flags. Source text is read from the measured V8 script, not evaluated
or reconstructed in the page. Empty chunks remain distinguishable from failures.
The final stop sample uses destroyed-context ID zero.

Precise collection drains isolate counters. To avoid one session stealing
another session's sample, opt-in start rejects any already active precise
collector in the isolate. While retained capture owns it, other precise starts,
stops, ordinary collection and triggered drains are rejected or suppressed.
There is exactly one collection per destruction hook, followed by filtering
using the genuine context-group registry. No URL/source heuristic selects the
owner. Existing precise collectors retain their ordinary behavior when the
vendor option is disabled.

The disabled destruction path performs a null-owner check. Typed conversion is
preflighted before allocating new protocol objects. Whole-capture limits are
2048 chunks, 2048 script occurrences across chunks, 16384 ranges, and 8 Mi UTF-16
units for source/metadata admission. URL and function-name fields have separate
8192-unit limits. Repeated snapshots count against these limits. Overflow,
unavailable source or conversion failure records a sticky error; the atomic
stop fails explicitly and releases capture state. Standard owner stop/disable
and actual agent destruction also release the retained data and ownership.

This is a same-agent candidate. The context-destruction seam still needs proof
that Blink has finished **all** final `beforeunload` and `pagehide` execution.
Garbage-collected contexts, context-group reset, agent destruction, BFCache,
renderer replacement and cross-agent handoff need verified final-receipt
boundaries. The candidate does not claim those boundaries are implemented.

## CSS candidate: preserve usage while the mapping exists

[`cef156-css-navigation-retention.patch`](cef156-css-navigation-retention.patch)
now exposes capability revision **2**, replacing the prior root-reset-only
candidate. The opt-in start owns real tracking before its synchronous initial
style/layout update. It rejects a disabled CSS agent or an existing tracker.

Actual rule deltas are collected both before root `Reset()` clears mappings and
before `UnbindStyleSheet` removes any bound sheet. This includes same-agent
sheet removal rather than relying only on root navigation. Snapshots store
immutable protocol stylesheet headers and exact text, together with the real
rule-usage records. They retain no DOM or live stylesheet objects.

`stopRuleUsageTrackingWithNavigationRetention` updates real layout, collects the
remaining usage and source snapshots, and stops in the same command. It returns
both `ruleUsage` and `styleSheets`. The earlier ordinary stop returns only usage;
it does not satisfy this stronger source-retention contract.

Bounds apply to the full retained capture: 8192 usage records, 2048 stylesheet
snapshots, sheet IDs up to 256 UTF-16 units, and 8 Mi units of source/header
strings. Each header's variable string metadata is capped at 32768 units.
Unavailable source, overflow, or mutation of an already tracked source identity
fails explicitly. Existing-sheet CSSOM/text changes require a future source
version/offset contract; they cannot be silently associated with an old text
snapshot. Stop/disable releases the tracker, snapshots and sticky state.

Root reset/unbind are concrete collection seams, not proof that all earlier
Blink destruction or imported-sheet invalidation paths reach them in time.
Agent replacement and BFCache are outside this candidate's established scope.

## Checks actually performed

The two patches pass `git apply --check` against all eight exact pinned source
files. The actual PDL parser from Chromium `156.0.8078.17` accepts both modified
protocols, and references to local extension types resolve.

There is **no** generated protocol C++ compile, Chromium/V8 build, rebuilt
`libcef.so`, or runtime proof for either candidate. Patch application and PDL
syntax checks do not establish C++ correctness or browser behavior. The stock
browser and its default current-document coverage remain unchanged.

## Production native adapter and remaining backend contract

The native merge helper and native/SDK adapter are now production source.
`resetOnNavigation: false` sends the actual Inspector capability query before
enabling domains. There is no environment override, build flag, or embedder
attestation hook. Ordinary current-document capture keeps its existing path.

Retention requires `Profiler.getNavigationCoverageCapabilities` revision **2**
or `CSS.getNavigationCoverageCapabilities` revision **3**, a nonempty bounded
`agentId`, and the exact `lifecycleContract` string
`suitecut.coverage.final-lifecycle.v1`. The isolated vendor candidates above
currently advertise JS revision1 and CSS revision2 and **do not meet this
contract**. Stock CEF does not expose these commands. Both cases fail explicitly;
no production capability flag is enabled by patch presence.

The required backend lifecycle contract is:

- Context samples include final `beforeunload`/`pagehide` execution and preserve
  exact native source versions and counters through context destruction/BFCache.
- Retained start acknowledges the same revision and `agentId` plus a unique
  `captureId`. Atomic stop acknowledges those exact identities and `final: true`
  alongside the typed JS chunks or CSS usage/source snapshots described above.
- Before a child/worker Inspector agent genuinely detaches, its
  `Profiler.navigationCoverageFinalized` or `CSS.navigationCoverageFinalized`
  event carries that same complete atomic payload and `terminal: true`. A live
  context-destruction notification is not an agent-final receipt.
- Root renderer replacement additionally requires a real paused replacement-agent
  admission barrier before author execution. Without it, the controller reports
  incomplete coverage rather than claim a continuous recording across processes.

Our controller validates capability/start/stop identities, source snapshots,
contiguous JS chunk sequences, monotonic timestamps, final-stop/destruction
receipts, UTF-16 offsets and safe counts. It merges each function's real
innermost nested deltas across changing block partitions, independently sums
root call counts, and retains separate actual context/script identities. CSS
projects genuine nested rule usage against exact immutable source versions.
Crossing/duplicate ranges, source mutation, unknown source identity, missing or
stale final receipts, capacity overflow and counter overflow fail explicitly.

The native target coordinator retains old document data across same-agent
navigation, gates newly attached targets before their existing author-resume
callback, freezes target admission at stop, and preserves the first failure.
It saves real terminal target measurements and waits for genuine session detach
before treating their physical ownership as retired. Detach without a final
payload, renderer termination, duplicate terminal measurements and root agent
replacement without the paused handoff barrier fail explicitly. Retained output
includes optional typed agent/capture/session/target identities; equal text or
URLs never merge distinct physical sources or targets.

Original operation deadlines and the existing physical command ledger remain in
force. Cancellation does not retire accepted Inspector commands. Domain-disable
ACKs or genuine channel closure retire cleanup even when a retained stop reports
a sticky backend error. The shared payload, target, source, range and 2 MiB
combined output limits apply across each recording. Ordinary script inventory
also passes attested native-owned entries to the controller so old native binding
scripts cannot become author coverage after their contexts disappear.

Focused controlled-protocol native checks cover real production adapter dispatch,
old/new document payloads, terminal payloads before actual detach, stop freeze,
missing/stale receipt rejection, native-script exclusion, cancellation, cleanup,
and existing reset/multiple-target behavior. Strict selected-source SDK checking
passes. These checks use actual CEF value types, but controlled Inspector ACKs;
they do not execute a rebuilt vendor library or prove browser lifecycle behavior.

The remaining work here belongs to the CEF/Chromium/V8 contract and its custom
build/runtime validation. The existing isolated vendor patches are preserved and
must be extended to supply the final lifecycle receipts above. No custom library,
browser E2E or fuzz run is claimed; those remain deferred by the user.

## Historical stock-backend evidence

The earlier actual-Inspector diagnostic on binary
`4752eaaf3e7dbd586c2711ce9bbbc96171a0d896fb2ee6c57887ce34e61949ca`
retained the first document's actual stylesheet ID, exact stylesheet text,
function location/script ID, and executed function body before navigation.
After navigation, stock `CSS.stopRuleUsageTracking` reported only the new sheet,
and `Profiler.takePreciseCoverage` reported no old script. Its clean diagnostic
exit was a capability observation, not a passing integrated build: the snapshot
had 254/255 native tests passing.

A separate paused-navigation diagnostic observed `beforeunload: 1`,
`pagehide: 1`, and three author calls, while the samples retained only two calls
and no final old CSS usage. The arbiter had resumed an unowned pause; diagnostics
also contained truncated lines. It did not establish a usable fence. These
negative observations remain unchanged by the uncompiled candidates above.
