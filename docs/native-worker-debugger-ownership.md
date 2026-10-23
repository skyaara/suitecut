# Native worker debugger ownership contract

Status: implemented privately for Source-owned dedicated workers. The SDK
`NativeWorker.createDebuggerSession()` routes to the actual Source worker
controller; its immutable attachment/Runtime pin grants no page or frame
identity. Focused native controls and SDK transport tests validate this slice;
matching live-browser integration remains unrun. Broker/shared/service worker
sessions and agent-wide pause/step are explicitly unsupported.

```ts
const debuggerSession = await worker.createDebuggerSession({ timeout: 1000 })
const scripts = await debuggerSession.scripts()
// Source, script breakpoints, and primitive evaluation use this captured worker.
// waitForPause observes an actual worker pause; it does not force a global pause.
await debuggerSession.close()
```

`debuggerSession.capabilities` exposes these supported controls after successful
native admission. `pause` and `step` are false for worker and frame sessions;
observed pauses, resume, inventory/source, script breakpoints and paused
primitive evaluation are supported. A missing actual Runtime unique context is
an admission error. Context-broker workers never borrow a Source transport.

## Existing receipts and missing ownership facts

`worker_controller.inc` admits each actual `Target.attachedToTarget` child into
`worker_targets_` and `worker_sessions_`. The target generation is native,
monotonic attachment ownership, not a Chromium execution-context generation.
The control owner is the actual `handle_namespace_`; neither URLs nor a numeric
execution-context ID distinguish a replacement attachment or realm.

`WorkerRuntimeEvent` records the numeric accepted Runtime context. It does not
retain the received `context.uniqueId` or a context-observation incarnation. A
new worker debugger ledger must observe the original Runtime event, retain its
actual unique ID, and invalidate on its corresponding destroyed/cleared event.
It must not retrofit identity from the latest worker URL, a parent frame's
context, the SDK wrapper, or a synthetic default-context ID. Missing or ambiguous
Runtime identity is an explicit unsupported admission, not a fallback to a page.

The public Source already owns `DebuggerDomains`, retained Debugger send
callbacks and `CoverageScriptInventory`. Source-owned dedicated-worker JS
coverage acquires this same per-session arbiter before real worker resume. The
private `SuiteCutContextWorkers::Client` has a different actual browser/observer,
command registry, process-close drain and control-owner namespace. It currently
has no shared Debugger arbiter or debugger script inventory/event router.
Context owner and worker event cursor are not interchangeable with a Source's
observer identity. Broker support therefore cannot reuse Source methods by
passing a worker session string.

## Immutable capability

The implemented capability is a Source-owned dedicated worker only. Capture:

- actual native Source namespace and observer incarnation;
- actual target ID, target kind and accepted native attachment generation;
- actual child CDP session, with its physical channel retirement fence;
- numeric Runtime context, received Runtime unique ID and native observation
  serial for that exact realm;
- actual parent-session ancestry for lifetime routing, separately from script
  authority; a parent document pin grants no worker-script authority.

A worker realm has no frame loader/document ID. Its native observation serial
must be labeled as such. An immutable pin compares all fields before admission,
after actual replies and before publication. Old accepted cleanup may use its
captured session only while that exact attachment remains; it cannot be sent to
a replacement with the same target/session text. An actual reattach requires a
new capability even when the author heap survives. Completed-close receipts
retain the original outcome; metadata eviction yields explicit stale identity.

The accepted published worker-ready receipt can gate opening. Mere network or
console observation before ready cannot authorize control. The unique-context
ledger must exist before Runtime.enable can synchronously deliver events and
must handle observer-registration reentry fail-closed. Runtime metadata arriving
before the explicit open ACK is retained under the original startup deadline.

## Domain and pause ownership

Acquire a developer lease on the same Source `DebuggerDomains` instance and
actual worker session already used by coverage. Install script observation
before the first real Debugger.enable, with no disable/re-enable replay.
Factory/native bootstrap priority remains unchanged. A published caller pause
requires every exposed frame script to belong to the exact captured worker
realm in the actual inventory. Unknown or mixed-context frames cannot lend
callFrame IDs. Foreign pauses follow the arbiter's existing unowned routing;
another consumer's pause must never be resumed, stepped or evaluated by this
capability. Actual native-attested script IDs remain ineligible for mutation;
matching author URL/source/hash has no such authority.

Keep the first worker slice to observed pause/wait/resume, real script inventory
and source, exact script breakpoints and primitive paused-frame evaluation.
Agent-wide pause/step stays unsupported until the actual session proves it has
only the intended worker realm. By-value serialization and exception limits
remain those of the existing native evaluation controller; no remote object
ownership is implied. The Source adapter supplies a separate native target classifier; it does not
construct a fake FrameDebuggerScopes pin.

## Cancellation and lifetime

Each operation retains its original native owner/generation/deadline. Logical
cancel or expiry rejects that observer and starts bounded cleanup; it never
retires accepted set/remove/evaluate/resume ACKs. Late breakpoint creation must
remove the returned actual breakpoint before the domain lease is released.
Retain the existing per-session/global mutation and callback bounds, including
unknown successful replies and cleanup IDs. Physical watchdog expiry posts
actual owner closure and preserves records until real callbacks/channel death.

Runtime realm destruction invalidates pause/script/evaluation authority and
actual V8 groups, while accepted Inspector callbacks remain retained. A genuine
session detach closes only that control channel and is not evidence of worker
author execution termination. TargetDestroyed can prove the target boundary;
OnBeforeClose proves its owning observer/browser drain. Parent navigation/frame
removal only requests this debugger's cleanup unless an actual worker-channel
receipt follows. Source physical close drains its own workers. Closing an
individual page must not retire a context broker's shared/service workers.

## Broker extension, separately gated

A later broker slice needs a Client-owned actual observer incarnation, shared
arbiter and bounded script inventory initialized before worker Debugger enable.
Its generic sender must use the real Client command IDs, exact child session,
original deadline and retained callback ledger, with posted fatal teardown.
All actual broker Debugger events must reach that arbiter before ordinary event
consumers. Detached/targetDestroyed/OnBeforeClose routing must drain accepted
callbacks before reporting the physical capability boundary. Actual manager
context-close completion, not Target.closeTarget ACK or Page.close, is its
owner-close fence. No Source-versus-broker cursor comparison is permitted.

## Controlled validation and pending matching proof

Focused components control wrong Source/observer/context owners, numeric
context reuse, missing unique ID, same-target reattach, pause-before-open ACK,
foreign-frame pause, native breakpoint priority, coverage stop while paused,
late set ACK after cancellation, late evaluation after realm loss, stale close
and callback retention through posted teardown. No timer may manufacture an
Inspector receipt. Component tests must distinguish native controlled facts
from real protocol capabilities.

A matching browser checker must use actual dedicated-worker target/session and
context receipts; exercise source, pause, primitive evaluation, breakpoint
cleanup and coverage coexistence; then prove real helper exit and strict native
stderr. Broker/service/shared scopes require their own browser proof after the
Client adapter exists. Existing root inventory/evaluation/frame checks remain relevant regressions.
The affected frame Source component and SDK debugger/worker tests have run;
these matching live worker browser proofs remain unrun.

## Implemented central Source seams (private)

Initialize `DeveloperDebuggerWorkerDocuments` with the actual Source namespace
and observer incarnation before enabling worker Runtime observation. If worker
Runtime traffic can arrive during observer registration before that identity is
established, mark worker debugger metadata unavailable; do not seed it later
from numeric `WorkerTarget.context`. This requirement does not change baseline
worker evaluation or coverage behavior.

After `WorkerAttached` has actually inserted its accepted target/session with a
new native generation, publish that exact attachment into the worker debugger
ledger. On the original `Runtime.executionContextCreated` packet, pass the exact
accepted attachment, numeric context and actual `context.uniqueId` before
`WorkerRuntimeEvent` consumes the packet. Only the real accepted worker-global
context qualifies; missing or ambiguous metadata cannot make a capability.
`ContextDestroyed`/`ContextsCleared` invalidate its metadata, then request only
that captured controller's cleanup; they never invoke arbiter `Detached`.

On actual worker channel detach, snapshot the original attachment before maps
are erased. Retire debugger physical callbacks/domain ownership through the
existing exact real-session drain, then notify the captured worker controllers
and metadata ledger with that original attachment. A stale notification cannot
close a new same-target attachment. Actual owner `OnBeforeClose` closes the
ledger only after retained native callback/domain drain. `Target.targetDestroyed`
is distinct evidence of author-target death, never synthesized by these seams.

The worker controller receives the captured worker pin through separate hooks
rather than projecting it into `FrameDebuggerScopes::Pin` with invented frame or
loader fields. It shares the Source arbiter's real session key with existing
worker coverage. Retained-callback/channel exclusion is a separate controller
prerequisite; the new metadata ledger has no physical-retirement authority.


## Retained worker callback drain

Opening a worker controller installs a Source-only predicate retaining the
actual shared Debugger call IDs through `WorkerDetached`. Removing the logical
worker registry entry cannot erase these pending callbacks. The existing
`NativeDebuggerDetached` drains them at the actual session boundary, before the
exact observer/session/generation closes the worker controller and metadata.
Context-broker operation cleanup remains unchanged when this predicate is absent.
The worker module regression exercises logical target-map removal with a held
callback; shared Debugger glue separately exercises actual callback/channel
settlement. Neither controlled receipt claims a running worker or browser.
