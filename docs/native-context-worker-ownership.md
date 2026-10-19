# Private context worker owner closure

The native `worker` context command accepts `action: 'waitOwnerClosed'` with an
actual current `contextId` and `controlOwner`. It observes the existing physical
broker lease; it never creates or replaces a broker. The original Engine
operation deadline and cancellation remain authoritative. A zero timeout retains
its existing meaning of no logical deadline.

At most 64 logical observers may be retained process-wide. Cancellation or
manager disposal removes an observer without releasing any physical CEF owner.
The command succeeds only after that exact Client's real `OnBeforeClose` has
finished its worker/console retirement and retained physical callbacks, and the
manager has accepted its final event cursor and removed the Client. A rejected
cursor or a close received at/after the original deadline produces an error.
`Target.closeTarget` acceptance alone never satisfies this command.

The success result contains `contextId`, the original `controlOwner`, and
`finalEventCursor` as a decimal string. A replacement broker may then inherit
that cursor. Only cursors belonging to this same manager publisher are
comparable; public Page/Source and private broker publisher sequences are
independent. The worker event wait reads both real CEF integer and double cursor
values and rejects invalid, fractional, future, and unsafe values.

The CEF-free observer component covers cancellation, exact leases, capacity,
original deadlines, cursor rejection, and reentrant callbacks. The CEF-value
component proves the integer/double boundary. The official
`verify-native-context-worker-replacement.mjs` checker requires actual broker
closure, preserved author state, a new genuine ready owner, inherited cursor,
old-handle rejection, and final context cleanup. Its live result must be recorded
against a matching compiled epoch; component success alone does not prove broker
replacement.
