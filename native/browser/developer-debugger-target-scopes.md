# Child-frame and worker debugger ownership proposal

Status: design only. The matching ff9863a9 proof establishes the existing root
primitive evaluation slice. It does not establish child-frame or worker debugger
support. No new public API, command dispatch, Inspector session or runtime proof
is introduced by this proposal.

## Scope separation

| Proposed slice | Actual native host/channel | Exclusion and first supported operations |
| --- | --- | --- |
| Same-process child document | Existing Source and the exact frame's recorded Inspector session, which can be the real root empty session | One developer consumer across every document sharing that channel. Wait for a genuine owned pause, resume it, read exact scripts/source, set/remove exact-script breakpoints, evaluate primitive results on owned frames, close. |
| OOPIF document | Existing Source and its actual accepted flattened iframe attachment | Independent developer ownership only when its actual session differs. Start with the same operations and document boundaries as the child slice. |
| Published dedicated worker | The actual Source or broker that owns the captured worker control pair and attachment | Separate native worker-context identity prerequisites. Begin with wait/resume/scripts/source/breakpoint/evaluation controls. Agent pause/step requires its own later accepted-receipt tests. |
| Shared/service worker in the Context broker | Actual `SuiteCutContextWorkers::Client`, never a Page facade | Separate native-host integration after the Source worker slice. The Client currently lacks the shared debugger arbiter and its retained sender. No promise that Source-owned coverage exists in this host. |

All rows refer to existing real channels. Native scope tokens name admitted
ownership records; they never stand in for, fabricate, or expose a CDP session.
A frame ID is never copied into a session ID. A worker target ID is never copied
into a frame, context or session ID. Host namespaces and attachment generations
are compared exactly, not decoded to discover another owner.

The first frame slice explicitly rejects `pause()` and step operations. Those
Inspector methods control an agent, not a selected execution context. A request
could stop an unrelated same-process frame or utility realm. A step could leave
the selected document. Do not advertise context-exclusive execution suspension.
The engine's actual pause can suspend the whole corresponding renderer agent.
Future agent-wide methods need a separately documented contract and controls.

## Immutable native scope pins

An admitted scope owns a bounded immutable record, at most 4096 retained identity
bytes, containing:

- The actual native host namespace/incarnation and actual Inspector session.
- The accepted native attachment generation for that channel. A monotonic native
  attachment stamp records a real attachment; it is not a new protocol session.
- For a document: actual frame ID, loader/document identity, default execution
  context ID, genuine Runtime unique context identity, and a native default-realm
  generation. URLs and origins are descriptive, never authority.
- For a worker: exact `workerControl.owner` and decimal generation, actual target
  ID, actual session, default context ID, actual unique context identity, and a
  native context generation captured from Runtime lifecycle receipts.

Retain the exact native host object while accepted commands or leases exist.
A facade updating to a new document or a canonical worker recovering its channel
cannot retarget an existing debugger scope. A new open must capture new pins.
Missing actual unique identity, readiness or ownership yields an explicit
unavailable/unsupported error. There is no fallback to an ID spelling or URL.

Current `FrameState.context_generation` also changes for utility-world creation.
Do not treat it alone as a new default document or silently substitute the latest
frame record. Add a default-context generation, or compare an equivalent actual
default identity ledger, whose changes are tied to default-context replacement.
Same-document navigation that preserves those pins need not invalidate a scope.

Current `WorkerTarget` does not retain Runtime `uniqueId` or a default-context
creation generation. Add those fields from genuine accepted contextCreated
receipts before worker debugger admission. Do not infer them from target IDs,
console messages, request metadata or readiness alone. Initial worker admission
requires actual published-ready state and the matching control pair; a pre-ready
observed SDK identity alone cannot open a debugger.

## Channel ownership and pause routing

Keep one shared `DebuggerDomains` per actual native host. Use the existing real
session in AcquireDeveloper, Send, event delivery and actual Detached. Coverage
and factory leases continue to use that same instance. Factory-owned breakpoint
hits retain priority. A debugger scope cannot inspect or continue their pause.
Raw mutation conflicts and conservative raw ownership remain unchanged.

Add a native developer scope manager with a channel exclusion record. The key is
host incarnation plus actual channel/attachment identity. Include root sessions
in this exclusion: a same-process child and root cannot open concurrently.
The record stays reserved through logical close, breakpoint removal, evaluation
group release, accepted callbacks and real domain-lease retirement. In
particular, an old lease becoming `releasing` is not permission to lend that
channel's old pause to a new scope. Reject a competing open until physical
retirement; do not queue it under a renewed budget.

The current arbiter offers every non-factory pause to its single developer
consumer, after which the root adapter validates the packet. Add bounded native
eligibility classification before a new scoped developer can own a pause:

1. Match factory hits using existing actual owned breakpoint IDs first.
2. Resolve the actual top-frame script ID in the actual session's inventory and
   compare its observed execution context with the scope's current default pin.
3. A known foreign document/utility/worker context is not this scope's pause.
   Keep the developer waiter pending and run the existing unowned auto-resume
   path, retaining that path's actual resume ACK and channel obligations.
4. The initial scoped slice requires every reported frame to belong to the same
   pinned context. A mixed-context stack is explicitly unsupported: fail the
   logical waiter, resume through an actual retained native obligation, and do
   not publish another document's frames or transferable pause token.
5. Unknown, malformed or capacity-failed metadata cannot confer ownership. If an
   actual scope-owned installed breakpoint hit cannot be attributed, expose the
   failure and retain cleanup rather than silently claim an ordinary foreign
   pause or let a timeout stand in for its resume ACK.

Deferred pauses must retain their original scope/lease candidate and original
budget, just as existing pending-arm packets retain their actual identity.
A later scope opening cannot adopt an earlier deferred pause. Scope rejection
must not close or expire a factory capture. No fake paused/resumed packets or
copied call-frame IDs are used to bridge scopes.

## Reusing controllers without copying root ownership

Factor native host binding from `DeveloperDebuggerControls`. Supply a captured
scope's actual Inspector session instead of the current hardcoded empty session.
Its current/owns-script/owns-pause/script-page hooks resolve only that immutable
scope. The existing root binding remains a distinct captured root document.

Source already observes actual per-session scriptParsed metadata before lazy
coverage creation. Child source reads, inventory cursors and breakpoint checks
must query that same inventory with the captured session and default context.
Inventory revisions stay context-specific. Identical script IDs in different
real sessions are separate records. Broker inventory/event observation must be
installed before its first actual Debugger.enable, even if no coverage API has
ever been created. Native attestation remains read-only
and cannot confer mutation permission. No source bytes are cloned or selected by
URL/hash equality. A child whose pristine main factory is unavailable can still
be considered only through its own actual Inspector metadata; never borrow the
root factory closure or its script/context IDs. Primitive evaluation groups
include unique native host/lease
identities and keep their exact real session and context pins.

Use the same global limits as root control: 16 live/physically closing developer
sessions, 32 logical operations per session plus reserved close observation,
16 breakpoints per session/128 per process, 8 evaluation records per session/64
per process, and 256 script cursor positions per process. Host-wide retained
sender/domain-call caps remain enforced across all scopes. Metadata manager
entries must be reserved within those admitted sessions, not in an unbounded
pre-open table. Prepare fallible allocations before quota/graph commit, with
rollback for uncommitted reservations.

`open`, wait, script and evaluation operations retain their original owner and
Engine generation/budget. Only genuinely classified close/resume cleanup uses
reserved control admission;32 waiters cannot fill that lane. A nested worker
command must preserve this distinction rather than marking all worker commands
as controls.

## Closure, cancellation and replacement

| Actual observation | Logical scope action | What remains physically retained |
| --- | --- | --- |
| Selected frame's real navigation/default-context replacement or removal | Fail its public operations and close its debugger scope | Installed breakpoint removals, actual resume, evaluation/source replies and group-release callbacks until their own ACK or real channel close |
| Unrelated frame/utility change | Preserve an otherwise exact current scope | Existing receipts and leases |
| Actual OOPIF session detach, including genuinely observed descendant-channel closure | Close that exact channel incarnation | Drain accepted callbacks at the existing actual session boundary; never detach another channel merely to finish cleanup |
| Worker channel detach or actual owner replacement | Permanently retire the old debugger wrapper | Accepted work until the actual old session boundary; worker author death is not inferred |
| Actual worker target death or exact broker/Source host closure | Fail affected scopes with the truthful boundary | Retire only the records belonging to that proven boundary |
| Root renderer crash while another OOPIF/worker may survive | Root session follows its existing root failure policy | Do not call child/worker RealmDestroyed using the root renderer's epoch or discard their Inspector callbacks |
| Caller/constructor abort or original deadline | Reject that observer and begin canonical cleanup of its captured scope | Retain actual pause/resume, late breakpoint ACK/removal and evaluation group obligations; watchdog requests posted closure rather than faking settlement |

A document invalidation is not an Inspector detach. A Runtime registry clear is
not an ACK to any outstanding command. Target kind alone does not prove worker
death. An actual resumed event and the ACK of an accepted resume/step remain
separate facts. Preserve current root replacement-recovery limitations.

Canonical close is unsignaled and resolves only the exact old native debugger
token, including when its worker has acquired a newer control generation.
Normal commands revalidate current pins; cleanup must be able to finish old
retained ownership without routing it to the new generation. Keep a bounded
exact cleanup-completion receipt if a canceled close observer can retry after
native retirement; an unknown token is not proof that cleanup succeeded.
SDK user signals, frame detached flags and Context close intent do not supply
physical boundaries. Source can use its actual browser-closed promise. Broker
cleanup must use its exact native Client/owner-close receipt or retained native
close operation, not a fabricated Page.closed or another broker's cursor.
Late cleanup failures remain observable under the existing bounded SDK ledger.

## Native implementation and proof gates

Implement in order, without exporting APIs before the corresponding native gate:

1. Scope resolver/manager, channel exclusion through actual retirement, bounded
   eligibility routing, and generic captured-session controller hooks. Controlled
   root/child conflict, wrong scope/session/context, mixed stack, deferred pause,
   factory priority, raw conflict, operation reuse and allocation failures.
2. Same-process child binding. Controlled navigation/utility distinction,
   actual-breakpoint late ACK/remove, retained evaluation/release cancellation,
   original deadlines, all-global quotas and real close cleanup.
3. OOPIF binding using actual attachment records and real detach fanout. Prove
   unrelated root/child ownership stays intact and no root-renderer death is
   substituted for a surviving child channel receipt.
4. Source dedicated-worker binding after actual unique-context tracking. Cover
   old target/control reuse, pre-ready rejection, real detach versus author death,
   pending callbacks, newer attachment with the same target and old cleanup.
5. Context broker host integration of the same arbiter/retained sender/event
   inventory and typed owner-close fence. It must preserve network policy,
   actual broker replacement and page-less worker lifetimes independently.
6. Only then add thin scope-specific SDK factories, opaque pause/frame/cursor
   maps and canonical-close forwarding. Proposed entry points are
   `frame.createDebuggerSession()` and `worker.createDebuggerSession()`, with the
   first-slice operation restrictions explicit in types and native validation.

Matching browser checks require separate actual fixtures for same-process child,
OOPIF and each supported worker kind. Prove independent source/local values,
exact native breakpoint hits, foreign pause exclusion, root/child conflict,
factory/coverage coexistence, cancellation and clean physical closure. Use
actual attachment/control receipts, not matching URLs or public wrapper identity
alone. Keep strict stderr, exact helper exit and immutable SDK/native/assets
before/after audits. Capability-only experiments cannot replace compiled public
wire proof. No broad root recovery, pristine-before-author worker debugging,
foreign-frame evaluation or remote-object adoption is claimed by this design.
