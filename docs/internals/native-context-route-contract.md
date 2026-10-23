# Native context route admission (first slice)

The implementation in `context_routes.h` is a native UI-actor policy book. A
`NativeContextRouteRegistration` holds one immutable ID/glob and one use counter
for an actual BrowserContext. All actual public Sources and private context
worker brokers bind the **same shared object**. Registering a second Page does
not reset `times`. Zero means unlimited; positive counts are consumed by the
existing route registry at actual invocation admission. Page registrations run
first, then context registrations, in reverse registration order within a scope.
Fallback retains the original match plan and never rematches an overridden URL.

Plans capture native registration generations. Removal/re-registration of the
same string ID cannot make an already captured plan invoke the replacement.
A policy cannot be claimed by a different manager/context. The extra plan capacity is charged to the existing paused/preflight metadata
budgets. Context policy strings have a process-wide 4 MiB retained payload cap
and 4096-object cap, including policies retained by physical callbacks after a
logical controller is canceled or cleared. Each context admits 128 policies and
1 MiB; the manager admits 32 contexts, 192 actual owners, 32 mutations and 1024
outstanding physical apply receipts. Per-target queues are bounded.

The manager has these integration hooks:

- `Seed(context)` after real context allocation; `Closed(context)` after actual
  context ownership is removed. `Clear()` is logical shutdown, not a receipt.
- `Attach(context, actualOwner, Apply, ready)` before public READY / worker
  discovery. The unique owner is the existing native Source/broker namespace.
  Each attachment also gets a native generation; late old callbacks cannot
  acknowledge a replacement with the same owner string.
- `Apply(Install|Remove, sharedRegistration, originalDeadline, ACK)` maps to
  `NetworkControls::BindContextRoute` / `UnbindContextRoute`. The callback must
  report only an actual control ACK or actual channel destruction. It must use
  the existing bounded sender, retaining dispatched callbacks after timeout.
- `Install(operation, context, sharedRegistration, deadline, reply)` and
  `Remove(operation, context, IDs, deadline, reply)` complete after all selected
  actual owners acknowledge. Newly attached owners join an in-progress install.
  Empty removal IDs select only generic context routes, preserving Page/HAR.
- `Cancel(operation)` drops logical delivery and rolls an install back behind
  genuine in-flight controls. `Tick(now)` uses the original deadline. A late
  positive ACK cannot turn an expired mutation into success.
- `Detach(actualOwner)` is called at actual native channel closure. It rejects
  pending mutations; its map removal never retires a retained physical callback.
  Physical receipts are released by the actual ACK/close callback even after
  `Clear()` or destruction of the manager.

Removing a policy prevents future admissions immediately. Already invoked user
callbacks keep their existing physical route leases until actual callback
settlement; registration removal must not pretend those callbacks have ended.
A canceled/failed installer rolls back without rebinding a removed policy.
Rollback capacity/ACK failures require actual native owner teardown.

The pending SDK/native adapter must deliver a genuine route owner envelope,
including the existing callback scope/token. It must resolve an initial Page
from its real native identity even if the SDK has not wrapped it yet, and use
an actual owner command path that does not wait for public READY while holding
a Fetch pause. A broker retains page-less worker attribution. No URL/target-ID
inference, fake Page, or cross-owner sequence ordering is permitted.

The adapter adds public `BrowserContext.route`, `unroute` and default-behavior
`unrouteAll` for string globs, using native `routes` policy operations and a
strict `contextroute` envelope. The envelope preserves its exact owner namespace
and callback scope. All route operations, including body-upload chunks, return
to that actual owner; request metadata/body methods keep their canonical network
owner. Worker route projection shares actual target/request identities and has
its own native publication sequence. It emits no invented request event when a
real Fetch pause precedes Network creation. Page/context duplicate delivery is
suppressed within that same canonical publisher, never across owner domains.

An early genuine Page envelope can create its canonical SDK wrapper before the
ordinary page-created notification. This emits no synthetic discovery event,
does not await Page READY to resolve the native pause, and creates no Frame or
Page identity for broker requests. Manager mutation tasks carry an independent
native generation as well as the Engine operation ID, so cancellation followed
by client-ID reuse cannot let an old physical ACK complete a new operation.

Controlled native/SDK tests are complete; the Source hook-only patch is prepared
against the current integration tree. Root's combined Source syntax/build and
matching integrated live wire proof remain pending. RegExp/custom predicate matchers,
HAR route ordering composition beyond existing behavior, popup first-wire
vendor gaps and `unrouteAll` wait/ignoreErrors behaviors are outside this slice.

The first exact combined 97916950 wire proof passed the five Page-owned groups
but failed the page-less service-worker group: its actual request reached the
server without invoking the handler. The repair enables Fetch only for actual
service-worker targets in the private broker, under an explicit default-false
controller policy. Dedicated/shared targets and public Sources retain their
existing setup. Real worker ancestry continues to exclude worker offline setup;
Fetch authentication still uses the existing bounded authenticated configuration.
A standalone unsandboxed pinned-CEF capability proved the real child Fetch ACK,
pause, fulfillment and author bytes, with wire absence and clean physical close.
The exact fb2068e8 production runtime later passed the unchanged six-group
context route proof with strict stderr zero and genuine helper exit zero. A
separate two-group proof retained the actual Page and confirmed one native
context callback/fulfillment with no wire, followed by one real wire request
when the same context-global `times:1` use was exhausted. No standalone flags or
SDK overlays were used; the earlier 97916950 negative remains preserved.

The following wait observer is staged independently of public unroute behavior.
Its immutable native snapshot contains only already-issued callbacks for the
exact context registration owner. Accepted callback Finish and revocation of a
never-started invocation settle that receipt; closing a running scope does not.
A native observer reserves bounded storage before policy mutation, then waits
only after actual policy-removal ACK, under the original operation deadline and
generation. Cancellation drops the observer, never the physical callback.
Logical lease-book disposal reports unavailable instead of positive settlement.
The native snapshot remains valid after completed-token history eviction. Public
`wait`/`ignoreErrors` adapters and their host exception policy remain pending.

The following public-removal adapter distinguishes two native mutation receipts:
`admitted` proves the selected policies were deactivated; `applied` proves the
selected actual owner controls acknowledged under the original operation budget.
A native wait reserves its exact receipt snapshot before deactivation, releases
its mutation lock after actual controls ACK, and observes only those original
callbacks. The SDK captures bounded removable host receipt links at actual admission
and awaits their settlement after the native operation succeeds, so it cannot credit asynchronous host ACK cleanup
from the native snapshot alone. Neither path starts a replacement deadline.

The host arms `ignoreErrors` before dispatch so a concurrent handler exception
can wait for the genuine removal outcome. Only native-admitted registration IDs
and a successful matching command can authorize suppression. Actual route,
network-metadata and body control rejections retain their original Error object
and an internal provenance marker; text matching never classifies these errors.
Native callback start/finish errors also remain outside suppression. A selected
user exception still needs actual automatic abort if its pause is unresolved.

Host policy scopes retain only bounded links to currently pending invocations,
removing each link on its real invocation receipt. A held admission cannot keep
an unbounded history of completed requests. Canceled waits remove their links;
they never append another continuation to a long-running invocation Promise.
Each invocation has exactly one settlement observer. Pending policy scopes are capped at
64, pending invocations at 128, and actual selected registration IDs at 256.
Context close drops observer delivery without marking authored closures ended.
This adapter remains private until review, combined Source checks and matching
live wait/ignoreErrors proofs; the earlier native primitive is independently
complete. HAR ordering, nonstring matchers and Page unrouteAll remain separate.

The following RegExp/synchronous-predicate adapter is described separately in
`native-route-url-matcher-contract.md`. It extends the same native policy book
and physical receipt paths; its matching public browser proof remains pending.
