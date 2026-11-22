# Native debugger collected-script cache

suitecut keeps a real Inspector Debugger agent enabled for native factory
breakpoints. Chromium's `Debugger.enable` defaults `maxScriptsCacheSize` to an
unlimited cache of source text for scripts that V8 has already collected. A
long-lived page doing repeated locator and evaluation operations can therefore
retain source text despite stable JavaScript heap usage after garbage collection.

suitecut no longer sets `maxScriptsCacheSize`: every `Debugger.enable` it sends
leaves V8's own collected-script cache policy in place. The 8 MiB cache it used
to request for internal factory consumers is gone. The native DebuggerDomains
arbiter still tracks two states per actual Inspector session:

- Internal factory consumers alone.
- An explicit Developer or Coverage consumer, while that consumer owns the
  domain, its paused capture, or real source/group receipts awaiting completion.
- After the last observer releases its physical ownership, a surviving internal
  factory returns to the first state before the observer's release succeeds. If
  no consumer remains, the existing actual Debugger.disable cleanup retires the
  agent.

State changes use another `Debugger.enable` on the same agent, which preserves
breakpoints and paused execution. suitecut records the new policy only
on the actual protocol ACK, never from dispatch or logical cancellation. New
observer acquisition waits for the historical-policy ACK. Failed configuration
closes the actual channel rather than claiming the requested policy succeeded.
A canceled or expired acquisition retains its accepted command and restores the
factory policy through another actual ACK.

Diagnostics expose actual acknowledged cache states as
`nativeDebuggerFactoryScriptCacheSessions` and
`nativeDebuggerHistoricalScriptCacheSessions`.

Validation includes all existing DebuggerDomains component groups, factory-paused
policy transitions, retained developer receipts, cancellation and late ACK
expiry, protocol failure, and 1,000 repeated coverage upgrade/release cycles.
These component tests instrument owned code with ASan, UBSan except vptr, and
LSan. The prebuilt CEF library/wrapper is not instrumented; its wrapper was built
without RTTI, making UBSan vptr incompatible with that ABI boundary. Whole-browser
memory and latency are measured separately against the rebuilt engine.
