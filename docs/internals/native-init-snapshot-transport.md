# Native init snapshot transport components

The browser context owner now publishes an aggregate immutable byte snapshot to
already admitted native renderer-browser receivers. Its real CEF shared-region
install/release ACK path is verified with actual admitted renderer browsers. The cache
is **not consumed by author bootstrap**: the existing synchronous native fetch,
initial Settings hold and fresh OOPIF fallback remain unchanged. Same-process
child pristine main-world dispatch remains unavailable under containment.

The next transport must avoid the actual synchronous-XHR cancellation caused by
an ordinary debugger pause freezing another frame's load. A renderer cache alone
cannot replace the browser's current authoritative snapshot.

## Implemented component contract

`InitSnapshotWire` encodes one opaque snapshot byte sequence with an explicit
format version and exact native context, browser, renderer-incarnation, frame,
aggregate publication-version and transfer identities. It validates every length
before copying, rejects truncated/trailing data, and preserves the payload bytes.
The payload limit is 8 MiB; the frame identity limit is 256 bytes. The payload is
not interpreted or semantically validated by this transport layer.

`InitSnapshotMessage` uses the pinned CEF 156 public
`CefSharedProcessMessageBuilder` API. `Build()` invalidates the writer. A shared
message has no argument list and cannot be copied through `CefProcessMessage`.
The receiver retains `CefSharedMemoryRegion` while any returned byte view is in
use. No mutable CEF dictionary/list graph is retained or reparented. Application
code treats the payload as immutable; no authored code receives a memory pointer.

`InitSnapshotReceipts` reserves at most 64 outstanding region versions and
64 MiB of wire lengths across its owner. Installation ACK can finish the public
registration but **does not release the region reservation**. Both the exact
installation ACK and an actual receiver region-release receipt are required to
retire a successfully installed region. Their arrival order is independent.
True renderer-incarnation death can retire only that exact browser/epoch.

Abort and expiry preserve accepted physical ownership and bytes. Expiry at a
real ACK is checked before success publication. The 30-second cleanup watchdog
requests actual teardown; it neither deletes a receipt nor reports physical
retirement. An accepted current region needs no deadline polling until its owner
explicitly asks the receiver to retire it. These reservations measure wire
lengths, not all process heap or kernel page-table/shared-mapping overhead.

## Required integration before enabling capability

1. The browser must assign one aggregate publication version covering init
   scripts, bindings and credential readiness. Independent existing version
   counters are not a total order. The renderer must reject older publication
   versions and mismatched bytes at the same version.
2. Browser publication must reserve bytes before allocating/sending and keep
   real installed-region release receipts. Renderer leases must be bounded too.
   A region cannot be called retired merely because an operation was canceled,
   a document was logically invalidated, or a newer version was sent.
3. Awaited registration/settings must wait for actual install ACKs from every
   admitted receiver, under the original deadline. Failed, canceled or late
   operations cannot publish success. Concurrent publications need explicit
   ordering; they cannot silently overwrite an accepted native registry.
4. `BrowserExtraInfo` is not proof of freshness after a renderer restart. The
   first root requires the actual native Settings hold; fresh OOPIF admission
   requires its actual waiting receipt. A new renderer needs a new native
   transport incarnation. A stale snapshot must not pass as current during an
   unproved startup ordering window.
5. Snapshot access from bootstrap must use a private native V8 function argument
   or closure, without a DOM global, console bridge, authored property or
   asynchronous release of author execution. Settings/init must still precede
   first-author script execution.
6. Shared bytes remove the XHR freeze failure but do not prove a genuine factory
   capture for every nested sibling document. The actual prior Inspector trace
   parsed a second sibling's script inside the first sibling's pause without a
   second breakpoint pause. Each capability still needs its own proven native
   pre-author barrier, exact document pins, capture and real resume receipts.

## Validation scope

The standalone native wire/receipt test has 16 controlled groups, including
wrong-owner/epoch/version ACK rejection, original deadline at submission/ACK,
abort retaining capacity, reordered release ACK, bounded admission and real-death
retirement. The CEF-linked test has six actual shared-region API groups, including
writer invalidation, region lifetime after message destruction and incompatible
owner rejection. Both run optimized and with address/undefined/leak sanitizers;
the CEF-linked build uses the project's `-fno-rtti` wrapper convention.

These tests do not launch CEF or prove cross-process delivery, startup ordering,
private V8 access, first-author settings, or restored child dispatch. Those
require compiled renderer/Source integration and actual browser fixtures.

## Renderer receiver component

`InitSnapshotReceiver` is a renderer-thread registry. Native integration must
admit each exact owner before accepting its packet. Installing a higher version
replaces the current snapshot only after validation and allocation succeed.
An exact duplicate transfer with identical bytes shares the existing reader;
an older version, a reused version with different bytes or transfer, and a
mismatched expected owner are rejected. There is no implicit renderer admission
from the packet, or admission from a document URL.

At most 64 admitted owners and 64 outstanding mapping leases are allowed, with
64 MiB of wire lengths charged. Superseded snapshots and snapshots retired by
logical owner closure remain charged while any reader uses them. Last-reader
destruction drops the registry's actual mapped-memory holder before publishing a
release notice. Notices themselves remain charged until drained. The native IPC
adapter must retain drained notices through a bounded send queue; draining does
not claim delivery to the browser or retire its separate physical receipt.
Mapping holders must be exclusively managed by this adapter and its reader
leases, and all reader destruction must occur on the renderer thread. These are
native ownership preconditions, not properties inferred from incoming bytes.

The receiver does not run a V8 bootstrap or release an author execution hold.
The production message adapter now routes actual native process messages and
context registration waits. Actual cross-process publication/ACK validation is
described below. Startup admission is still independent of this renderer cache. Same-process child pristine dispatch
remains unavailable in the containment implementation.

The receiver has nine controlled lifetime/admission groups. The CEF-linked test
now has eight actual same-process shared-region API groups, including mapping
installation after message release and rejection at the exact supplied deadline.
Both pass optimized and ASan/UBSan/LSan builds. The CEF-linked test retains the
project's `-fno-rtti` convention. The original authored checker negative (observing
temporary destruction before its full expression ended) is preserved separately;
its correction changes only when the actual destruction is observed.

## Production publication and IPC slice

`InitSnapshotPublications` assigns the next context version only when the actual
native store accepts the complete serialized snapshot. It does not compare or
order script, binding, or credential counters. Identical accepted bytes reuse the
current version. Its 64 MiB / 64-body quota includes old string bodies retained
by actual native IO store aliases, and is separate from the shared wire-region
64 MiB quota. Neither quota claims to measure all allocator/CEF/kernel overhead;
JSON and native metadata envelopes retain their existing independent bounds.
If native acceptance throws, a new placeholder is erased, existing state remains
unchanged, and the original thrown value propagates.

ContextControls hooks accepted script, binding and credential publications in its
single UI-owner order. The credential controller forwards the original command
owner; rollback uses a separate cleanup owner rather than renewing the public
budget. Each mutation records its aggregate version and the actual admitted
receiver epochs at that commit. Successful existing installation callbacks then
wait for real shared-region install ACKs for those receivers, under the deadline
and operation generation captured before asynchronous work. Superseded versions,
logical aborts and timeouts do not erase accepted physical region receipts. A
cleanup watchdog requests posted browser closure, never synthetic retirement.
An observed positive install ACK for a newer version starts that watchdog for
strictly older installed regions of the exact same receiver owner. A late older
install ACK is checked against the already observed newer version too. Neither
case frees its mapping reservation or treats a missing release message as an ACK.

The renderer requests native admission from its real main-context callback;
ContextControls assigns the browser/receiver incarnation and waits for its exact
native ready receipt before publishing. The publication channel identity stays
separate from later document/routing-frame identities. Main-context release
clears only its native V8 reference: it does not discard the renderer/browser
cache. Actual native BrowserDestroyed sends a bounded incarnation tombstone;
Source's genuine renderer-termination hook and actual browser close retire only
that exact ownership. Late old-instance closure cannot erase a new peer.

The Source hook patch also supplies the same Engine-relative millisecond origin
for operation deadlines, current time and operation generations. No new clock is
inferred from wall time or from a renderer. Public command success is checked
against the original budget at actual ACK publication. ACK-current versions,
physical reservations and genuine install/release receipt counts are exposed in
the existing native context diagnostics; unknown progress is not reported as an
invented measured zero.

`verify-native-init-snapshot-publication.mjs` is an authored eight-group real
browser verifier. All eight groups passed on the matching sealed native and SDK.
It checks actual native cross-process installation, aggregate ordering across
scripts/bindings/credentials, unchanged first-author fallback, browser cache
lifetime through same-renderer navigation, two admitted receivers, actual region
release and owned context closure. It does not test or claim opaque dispatch,
new-renderer pre-author cache use, or fresh OOPIF cache readiness.
