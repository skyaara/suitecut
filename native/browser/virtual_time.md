# Native current-document clock

`installPaused`, `pause`, `advance` and `runFor` use Chromium's native
virtual-time scheduler; Date, Temporal, timers and performance are not replaced
with JavaScript. Each bounded advancement retains its native owner through the
actual final pause acknowledgement, including after user cancellation.

Repeated `pause` calls reuse an already acknowledged paused state. The native
FIFO admits each request only after any earlier physical command or cleanup
fence retires, checks its original deadline and reports its own generation.
This avoids one redundant renderer command and acknowledgement per repeated
pause. Installation, advancing and cancellation still require actual backend
receipts. Non-finite native admission clocks reject before queue mutation.

A zero budget remains rejected: pinned Chromium omits its budget fence when
`budget` is zero. `policy: advance` without a budget runs virtual time without a
wall-time pacing guarantee, so it cannot implement realtime `resume`. Chromium
does not expose a matching disable/rebase command that safely restores the real
clock after this installation. Neither operation is approximated here.

The controller pins the actual root default document by frame, exact session,
unique execution-context identity, numeric context ID and renderer epoch before
its first dispatch. Individual destruction or replacement of that document, root
frame detachment, all-context clearing and navigation invalidate the controller.
Late destruction receipts with a reused numeric ID and a different unique
identity do not invalidate the pinned document. Every dispatch and physical ACK
rechecks the real document, so an old owner cannot complete in a replacement
realm. Actual target closure still retires physical work; logical invalidation
does not fabricate an ACK. Frame-targeted clock wire requests reject.

Control ownership remains the installed current document and its same-process
frames. Returned `scope` metadata separately identifies Chromium's native wall
clock as renderer-process-wide and explicitly denies context isolation. It does
not infer isolation from a page-scoped controller. Older helpers can omit that
metadata; SDK consumers must treat omission as unknown.
Context/OOPIF/worker inheritance, navigation, controlled animation/idle callbacks,
fixed Date with running timers, rebasing and at-most-once fast-forward remain
unsupported. The returned capability flags remain authoritative.

The focused controller checks cover queue ordering, stale budget events, original
deadlines, cancellation fences, target lifetime and exception receipts. They do
not establish end-to-end browser clock parity or measured latency improvement.

Full context-clock propagation needs backend primitives that do not exist in the
stock or isolated process-wall-time contract: a context-isolated native Date/
Temporal/performance and timer-scheduler owner; pre-author installation in frame
and worker targets; matching multi-target commit/retirement barriers; and native
paced scheduling, fast-forward and animation/idle delivery semantics. Broadcasting
current `setVirtualTimePolicy` commands to targets would share process-global
wall time, admit author execution between targets, and cannot supply these
semantics. The missing primitives are backend feature requirements, not an
unfinished SuiteCut propagation loop that can safely be wired to this protocol.
