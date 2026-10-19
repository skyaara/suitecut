# Inactive native frame debugger primitives

This unit adds native ownership controls without activating a Source command or
public frame/worker debugger factory. Existing root binding behavior remains
separate. The broader scope design is in `developer-debugger-target-scopes.md`.

`FrameDebuggerScopes` retains exact host/observer, real Inspector session,
accepted attachment stamp, frame/loader, default context unique identity and
native document generation. Its Source hooks must compare all current pins with
real registry facts. Root and same-process children share one actual channel
fence; OOPIF channels can be independent. Logical close, Ready failure, native
lease `releasing` state and public timeout do not retire that fence. Retirement
requires actual arbiter lease disappearance and accepted-call drain, or the
exact actual channel/host boundary. Reused native lease IDs and stale receipts
cannot affect a replacement scope.

The process book permits 16 scopes, 128 actual-call reservations and 65536 charged
identity bytes; each scope permits 32 calls and 4096 identity bytes. These are
retained-record/string payload limits, not an absolute heap bound. Fallible
record/channel-node allocations precede quota publication. Pending acquisition
is bracketed separately from its logical Ready result. A fixed 30-second cleanup
watchdog requests posted real host closure once and retains ownership until
actual ACK/closure. The primitive itself never sends a protocol command.

`FrameDebuggerPause` reads actual script-context associations from the selected
real session's inventory. Same-ID records on other sessions cannot substitute.
The initial classifier requires all reported frames in the same pinned default
context. A foreign top context is declined; mixed ancestor stacks, unknown
metadata, stale scopes and genuinely attested native frames are Unsupported.
Matching author URLs/hash tuples do not attest native ownership.

The new opt-in `DebuggerDomains::AcquireScopedDeveloper` preserves exact factory
breakpoint priority. Declined foreign pauses follow real unowned resume and
receive no developer pause token. Unsupported attribution fails the scoped
observer after recording the actual resume obligation. Deferred foreign packets
cannot upgrade into ownership or be lent to a later consumer. Classification
blocks reentrant pumping until the actual pause is recorded. Legacy
`AcquireDeveloper` callers are unchanged.

Controlled verification passes 16 scope-ledger, 8 metadata-classifier and 45 shared
arbiter groups, each optimized and under ASan/UBSan/LSan. The scope groups cover
root/child exclusion through cleanup, independent channels/hosts, acquisition
failure and late ACKs, actual lease/call drain, wrong attachment/observer closure,
old receipt/native-lease rejection, disabled/expired budgets, capacity, cleanup
watchdog retention and reentrant closure. The arbiter adds actual sender-seam
controls for foreign/unsupported routing, factory priority, deferred attribution
and reentrant release/session closure. These are controlled native fixtures,
not matching child-frame browser evidence.

Before activation, Source must adopt the channel book for existing root opens,
add default-only document/actual attachment ledgers, bind the retained sender,
and compose exact lifecycle drains. Keep child agent-wide pause/step, mixed
context evaluation, worker/broker ownership and broad root recovery unsupported.
The separate Source seam review document makes no applied Source changes.
