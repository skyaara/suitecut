# Native debugger collected-script cache

SuiteCut keeps a real Inspector Debugger agent enabled for native factory
breakpoints. Chromium's `Debugger.enable` defaults `maxScriptsCacheSize` to an
unlimited cache of source text for scripts that V8 has already collected. A
long-lived page doing repeated locator and evaluation operations can therefore
retain source text despite stable JavaScript heap usage after garbage collection.

The native DebuggerDomains arbiter applies these policies per actual Inspector
session:

- Internal factory consumers alone: 8 MiB collected-script source cache.
- An explicit Developer or Coverage consumer: the existing unlimited historical
  source policy while that consumer owns the domain, its paused capture, or real
  source/group receipts awaiting completion.
- After the last observer releases its physical ownership, a surviving internal
  factory returns to 8 MiB before the observer's release succeeds. If no consumer
  remains, the existing actual Debugger.disable cleanup retires the agent.

Policy changes use another `Debugger.enable` on the same agent. The pinned V8
implementation updates the limit before its already-enabled early return; it
preserves breakpoints and paused execution. SuiteCut records the new policy only
on the actual protocol ACK, never from dispatch or logical cancellation. New
observer acquisition waits for the historical-policy ACK. Failed configuration
closes the actual channel rather than claiming the requested policy succeeded.
A canceled or expired acquisition retains its accepted command and restores the
factory policy through another actual ACK.

This bound covers collected-script source text, not active scripts, renderer
heaps, network caches, or the whole browser. Lowering an existing limit takes
effect when V8 next trims its collected-script cache. Historical source evicted
before an explicit observer starts cannot be recovered by raising its limit.
Coverage/developer metadata and other native budgets retain their own limits.

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
