# Frame debugger Source adoption contract

Status: private binding contract. The frame scope and pause primitives are
inactive. This work does not add a frame command or SDK factory. Kit A first
adopts the existing root controller into the same channel exclusion book that
will later serve frame controllers. Kit B will bind a captured frame to a
controller only after Kit A's controls and matching root regressions pass.

## Actual Source facts

Source installs its DevTools observer before pristine startup and the initial
domain enables. `FrameState` holds actual frame/default-context IDs, Runtime
unique context identity, loader and Inspector session. Its generic
`context_generation` changes for utility worlds too, so it cannot identify a
default document. The existing live `sessions_` and `native_target_parents_`
maps record accepted iframe attachments. The network migration graph is a
different owner and can outlive those attachments; it is not debugger authority.

`FrameDebuggerDocuments` will observe these accepted facts eagerly, from
observer installation, rather than reconstructing document history at the first
debugger open. A successful observer registration supplies the checked native
registration stamp and actual Source namespace. The empty root session names
the actual observer's root channel. It is not a generated CDP session.

The ledger admits at most 1024 default documents and 1024 iframe attachments,
charging at most 4 MiB of retained identity storage. These are native metadata
bounds, not an absolute heap bound. A default pin is at most 4096 identity bytes.
Fallible copies/map nodes are staged before quota publication. Identity serials
never wrap or derive from a URL, script ID, context ID or target ID. Overflow,
invalid actual metadata or an untrackable accepted attachment makes admission
unavailable and triggers the adapter's posted close policy; it cannot leave a
stale pin current while silently skipping the new observation.

## Ledger calls and their authority

| Source fact, after validation | Ledger mutation | Physical meaning |
| --- | --- | --- |
| Successful observer installation | Construct with its actual host/stamp | One registered observation channel; no Inspector ACK |
| Accepted iframe `Target.attachedToTarget` | `Attached(session,parent,frame)` | Monotonic attachment stamp; duplicate identical live metadata preserves it |
| Actual frame tree/root identification | `MainFrame(frame)` | Names the existing root frame; does not create a context |
| Actual default `Runtime.executionContextCreated` | `DefaultCreated(snapshot)` | Records that exact default context/unique identity, actual current loader and accepted channel |
| Initial frame-tree association after its default context receipt | `InitialFrameAssociated(snapshot,observedDocument)` | Completes only the already observed exact default tuple's missing loader, with its original native document serial; no new context is invented |
| Utility context creation | No ledger call | Cannot replace a default document |
| Actual committed document, frame removal/swap | `FrameInvalidated(frame)` before client publication | Invalidates the old pin; does not close a channel |
| Actual BFCache-restored commit | Invalidate, then `DefaultCreated` using the restored actual Source default tuple | New native document serial even if Chromium restores the same context IDs |
| Exact context destruction/contexts cleared | `ContextDestroyed` / `ContextsCleared` | Logical document invalidation only |
| Actual accepted session detach | Snapshot attachment, drain native consumers, then `Detached(session,stamp)` | Removes matching current metadata; the separate scope book receives the proven channel boundary after drain |
| Actual observer/Source closure | Drain consumers, then `Closed` and book `HostClosed` | Genuine host boundary, not a public abort |

`DefaultCreated` receives a copied native snapshot, never a request-supplied
session or context. Identical live default receipts preserve the serial. Same
document URL/history changes and utility receipts do not call invalidation.
An initial default context may precede initial frame-tree metadata; the actual
initial association may complete its missing loader. A subsequent commit
invalidates it conservatively. A later genuine default receipt or explicitly
restored Source tuple is needed before admission. A loader-only lifecycle
update does not authorize copying an old context into a new document. Resolve additionally
compares against current Source frame/default facts, so that interval fails
closed rather than silently publishing an old-context/new-loader pair.

The ledger owns no callbacks or domain leases. Erasing metadata must never be
used to retire accepted Inspector work. Existing immutable scope records keep
their captured pins and physical ownership after the current document vanishes.

## Legacy root adoption, before any frame activation

The root controller keeps its current renderer epoch for `RealmDestroyed`.
The ledger's document serial is a separate native identity and must never be
passed to that V8-death hook. The root binding supplies an optional exact scope
binding to the existing controller; controls without a binding retain their
existing exclusive test path.

1. Allocate the actual Source debugger lease once. Resolve the actual root pin
   and reserve its channel in `FrameDebuggerScopes` under the original open
   deadline, before `AcquireDeveloper`. Prepare controller/callback allocations
   before physical dispatch. A rejected scope reservation sends no command.
2. Set the controller state, then bracket the actual arbiter call with
   `BeginAcquire`/`AcquireReturned`. Ready must pass the scope's current pin,
   original deadline and real `HasLease` gate before public open succeeds.
   A reentrant failed Ready is logical failure, not lease retirement.
3. Closing marks the controller state closing before calling scope `Close`, so
   the book's cleanup callback can reenter canonical controller cleanup safely.
   Do not release the channel fence merely because the controller has no public
   operations or its domain lease is already marked releasing.
4. Each accepted controller send captures the immutable scope token and holds
   both the scope receipt and actual arbiter receipt. The actual callback first
   lets the consumer enqueue late breakpoint/group cleanup, then retires its
   preceding holds and polls retirement. Dispatch refusal retires only the
   unsent reservation. A throw after possible dispatch requests posted closure
   and keeps uncertain accepted ownership until actual receipt/closure.
5. Controller retirement polls the same captured token; Source does not use a
   mutable latest-token lookup. Callback closures keep their own old token even
   after state retirement. Close completion cannot make a new same-channel
   open admissible while one old physical receipt remains.

The controller integration uses an optional shared `FrameDebuggerScopes` and
an exact `scope_pin(Document)` hook. The controller State retains its immutable
native scope token and each actual call retains its book receipt alongside the
existing arbiter Hold. This avoids a second mutable Source lease-to-token map
and keeps late cleanup attached to the exact old owner. Controls without these
hooks retain their existing exclusive fixture path. The binding retains at most
the already admitted 16 developer owners and existing bounded calls;
source-wide lease allocation remains shared with coverage and factories.

An opt-in root open received before actual default metadata is available waits
in one bounded native pending-open slot under its original startup deadline.
It creates no scope token or lease until the actual pin is ready. Metadata
wakeup retries admission; cancel/deadline removes that logical observer.
No startup budget renewal or invented context is allowed. The existing root
API cannot regress merely because the new ledger has not observed its real
default context yet.

Initial association retains the first actual default document serial for that
initial frame-tree request; it must never look up a newer serial to repair a
stale reply. An intervening commit, frame/attachment invalidation or default
replacement blocks the association, even if a stale Source cache repeats the
same numeric context and unique identity. Source also records invalidation
before the first default receipt, so an earlier frame-tree request cannot bind
a later default after a commit. Those bounded initial-request observations are
cleared when that exact tree reply is processed or its actual channel closes.

The initial-tree metadata guard reserves the actual Source-assigned command
before domain enable. Before its real SendDevToolsMessage attempt, current default receipts
may replace earlier preparation facts, since no tree snapshot was requested.
Immediately before that actual send in both immediate and deferred paths it
switches to the dispatched phase, allowing synchronous native replies. Queue
admission retains the exact session/attachment but does not freeze defaults.
From then on, a commit, default
replacement or frame/context invalidation blocks association. A clear before
the first default receipt blocks future associations too. Transport refusal
releases only this metadata; the existing Source registry still owns all real
command receipts. This guard is bounded to 1024 initial requests, 1024 frame
observations and 1 MiB of charged metadata, separate from the document ledger.

On actual iframe detach, the existing observed descendant walk closes the same
real session's arbiter and drains accepted Source callbacks. Only afterward may
the scope book receive `ChannelClosed(channel,oldAttachment)`. A frame removal,
context clear, renderer-failed flag or constructor AbortSignal cannot call it.
On root renderer death the legacy root realm hook remains distinct; surviving
OOPIF and worker sessions are not retired using the root epoch.

## Separate activation and proof gates

Kit A comprises this ledger, optional root ownership hooks, controlled retained
root cleanup and a separate narrow Source patch. It adds no public frame path.
Kit B then captures exact frame/session pins, uses scoped arbiter eligibility,
and enables native binding before thin SDK factories. Child agent-wide pause
and step, mixed-context stacks, worker context identities and broker integration
remain unsupported prerequisites.

Required controls cover utility/repeated-default stability, actual commit and
ID reuse, BFCache replacement, wrong-session/attachment/context receipts,
metadata quota/serial overflow, allocation failure, root-child exclusion through
late ACKs and reentrant failed open. Matching browser gates first rerun root
debugger12, breakpoint8, evaluation8 and inventory6 against one exact new epoch,
then add separate same-process child/OOPIF checks after activation. Controlled
metadata tests do not establish browser lifecycle or physical ACK claims.
