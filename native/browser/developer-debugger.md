# Explicit native debugger sessions

This slice adds native controller and SDK code for an explicit developer lease.
The authored12-group browser proof passes on exact sealed binary
`97916950c775fee46c3a6f361b6b130bf3ac62395d50b85e3d527bc626351f14`
with empty native stderr, helper exit0, and all1853 inputs/230 assets unchanged.
The earlier10-group e38 proof remains preserved separately.
It does not open an Inspector UI or
implement Playwright's private debugger objects or `page.pause()` UI behavior.

The public entry is `page.createDebuggerSession({ timeout, signal })`. The session
supports `pause`, `waitForPause`, `resume`, `stepOver`, `stepInto`, `stepOut`,
`scriptSource`, and `close`. Continuations accept an actual pause object returned
by that session. Native ownership validation is authoritative; SDK objects also
reject a forged or consumed pause before a command is sent.

The first scope is one actual current root document. Child/worker targets,
renderer replacement recovery, breakpoints, call-frame evaluation, live editing,
and developer UI are outside this slice. Source reads require an exact observed
`Debugger.scriptParsed` identity in that document. A source read is bounded to
262144 UTF-16 units. Pause metadata selects at most64 actual frames/64 total
scopes. It never projects arbitrary author `RemoteObject.value` dictionaries.

Factory breakpoint hits retain priority. Developer and factory leases require
pauses enabled; coverage alone may skip pauses. Closing coverage or a developer
lease cannot disable a domain still owned by the other consumers. Unowned author
pauses keep their existing native auto-resume behavior. Raw Debugger mutators
still conflict with active internal ownership.

Each call carries one original native deadline. A pause's original wait/request
deadline also limits ownership of that delivered pause; `timeout:0` explicitly
allows sustained pause ownership. An unsolicited pause without an existing wait
uses a native30s ownership budget. An inspection command cannot renew an earlier
pause budget. `Debugger.pause` needs both its actual ACK and a genuine subsequent
paused event, and may time out on an idle page that executes no statement.

Canceling an admitted pause request does not prove the next-statement pause
intent is gone. Its separate native Hold survives the command ACK and public
rejection. An actual later developer pause is resumed before the lease can
retire. If that event never arrives, a bounded30s cleanup watchdog requests posted
channel closure; only the actual channel boundary retires the retained intent.
An actual factory pause is not guessed to have consumed that intent. Source reads
and continuation ACKs likewise remain physically reserved after logical cancel.

Process admission is16 sessions including pending acquisition/cleanup, with32
public operations and32 actual Source commands per controller. One extra close
observer is reserved so a full set of waiters cannot block cleanup. These are
ownership/payload limits, not an absolute allocator heap bound.

## Source composition

The shared Source owner must add the following hooks. `browser.cc` remains owned
by integration; the controller and `developer_debugger_source.inc` are separate.

1. Include `developer_debugger_controls.h` with the native headers, then include
   `developer_debugger_source.inc` in Source after the debugger/coverage adapters.
2. Classify only `debugger` actions `open`, `close`, `resume`, `stepOver`,
   `stepInto`, and `stepOut` in the actual native control lane. Open is bounded to
   one per Source/process16. Pause/wait/source retain ordinary admission and
   cannot consume the cleanup reserve. Dispatch the command to
   `NativeDeveloperCommand(owner, message)`; its absolute budget comes directly
   from `operations_.Get(owner)->deadline_ms`. Permit only its `close` action as
   cleanup traffic after a renderer failure. Never queue resume behind author JS.
3. In both real DevTools event paths, call `NativeDeveloperEvent(method, params,
   session)` before updating/removing root execution-context metadata. Its
   logical invalidation does not retire Inspector callbacks. The existing shared
   `NativeDebuggerEvent` still owns paused/resumed routing and exact factory hit
   classification.
4. Call `NativeDeveloperChanged()` at actual root renderer termination before
   changing the renderer epoch. Do not call `Closed()` for a logical abort,
   contexts-cleared event, or renderer realm death.
5. Cancel a public owner with `native_developer_debugger_->Cancel(owner)` from
   Source completion/cancellation. Run `NativeDeveloperTick()` from the native
   deadline loop; include `NativeDeveloperNeedsTick()` in its idle predicate.
6. At actual `OnBeforeClose`, after the shared Debugger channel has drained, call
   `NativeDeveloperClosed()`. Publish its diagnostics when the controller exists.

`NativeDebuggerSend` must invoke callbacks only on actual ACK/real channel close
or proven dispatch refusal. The controller adds a shared-domain Hold before that
send and retires it only after processing the actual callback. Its fatal hook
uses the existing posted `NativeDebuggerRequestClose`, never synchronous browser
teardown inside a DevTools observer.

## Verification scope

The native controlled suite exercises actual ACK orderings through its sender
seam, source/intent retention, original deadlines, scope/token bounds, and
reentrancy. SDK units cover metadata validation, constructor lifetime forwarding,
per-call signals, zero-disabled deadlines, and no host polling/retry loop.

The matching browser proof covers genuine author pause and call frames without
ever starting coverage first, exact served source bytes, step over/into/out with
observable author effects, coverage start and stop while paused, factory
coexistence, default unowned behavior, and actual cancellation/close. Its cancel
gate observes the actual pause-command ACK complete with an armed intent still
physically reserved, then proves the reservation survives caller abort and
retires only after a real later pause/resume. The official checker is
`scripts/verify-native-developer-debugger.mjs`.

The SDK forwards a later NativePage constructor abort to one
canonical close for the exact returned native session. It retains that promise
through actual native cleanup and lets public `close()` join it. It bypasses only
the constructor caller signal, using the actual Source page lifetime; a Source
close race waits for its actual closed promise. It never classifies error text
or a composed user signal as a physical boundary. A bounded16-entry SDK ledger
includes open subscriptions, pending cleanup and unreported cleanup failures;
the next same-Source admission reports an actual prior cleanup failure.

The matching12-group checker passes idle and paused post-open constructor
abort, including actual author continuation with effect97 and zero debugger
ownership before public close. The native factory lease remains active. The
immutable runtime index is
`/tmp/suitecut-artifacts-97916950-debugger12-v1/runtime-index.json`
(SHA256 `252c99f62d5dc952b57a40b49970ffdbc75114fed327d91cdc28a0da2b62b8ee`).
Controlled SDK tests include the full
128-command control lane, abort/open and abort/close races, actual closed-promise
retention, and late failure reporting.

Behavior references: the public Chrome DevTools Protocol Debugger domain and
Playwright's public debugging/Page.pause documentation. No external debugger
implementation was copied.
