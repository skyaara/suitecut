# Primitive evaluation on an actual paused frame

Matching eight-group browser verification passes on the sealed ff9863a9
candidate; held-reply and actual realm-death cases retain their separate native
controlled proof scope.
`session.evaluateOnCallFrame(pause, frame, expression, options)` evaluates direct
source against a genuine frame from that session's current native pause. Native
ownership validates the actual pause identity, captured call-frame membership,
Source epoch, root context and unique context identity. The SDK also retains the
original observed frame ID, so mutating displayed metadata cannot retarget it.

The result discriminates a primitive value from a genuine JavaScript exception
receipt. Supported values are undefined, null, boolean, string, finite number,
NaN, negative zero, infinities and bigint. Exceptions retain the actual Inspector
text, location, optional description and supported thrown primitive. Selected
string values preserve isolated UTF16 units. Non-null objects, functions,
symbols, promise awaiting and remote result handles are explicitly unsupported.
Arbitrary author value dictionaries are never reinterpreted as transport tags.

Expression source is limited to 8192 UTF16 units. A JSON string token carries it
to native validation without accidental ingress normalization. Raw isolated
source units reject explicitly; source can use JavaScript Unicode escapes to
produce or inspect isolated-unit values. The serialized result is at most
65536 bytes, string values at most 8192 units, and exception text/description at
most 4096 units each. These are serialization and retained-record bounds, not an
absolute CEF/V8 heap bound.

The Inspector returns primitives by value while object serialization and preview
are disabled. Unsupported objects return only native temporary identities.
Each evaluation creates a native object group, including throwing expressions
and unsupported object results. The public result is published only after the
actual Runtime.releaseObjectGroup ACK. Up to eight records per session and 64
per process include pending evaluation replies, pending releases and failed
cleanup. Original public deadlines remain authoritative. Closing/canceling an
accepted expression cannot undo author side effects; the real reply and cleanup
remain retained. A separate fixed 30-second cleanup watchdog requests posted
channel closure and never fabricates an ACK or renews the public deadline.

Actual renderer death marks old V8 groups destroyed using the exact native
Source epoch, while accepted Inspector callbacks/Holds remain until their real
ACK or channel closure. Logical contextsCleared, a navigation request and a
caller abort never establish that death. Root document replacement recovery is
outside this session contract, as in the existing explicit debugger slice.

Controlled verification passes: 22 evaluator and 29 integrated controller cases,
each optimized and under ASan/UBSan/LSan, including late replies, real group
release, exact old-pause rejection, cancellation, capacity, actual realm death,
fractional engine budgets and deadline expiry during admission. All 32 SDK cases,
SDK build/full type checks, checker type checks and scoped lint pass.
The exact sealed `scripts/verify-native-developer-frame-evaluation.mjs` passes
all eight matching browser groups on ff9863a9. The actual proof covers a fresh
no-coverage root frame/source, supported primitives and isolated units, genuine
exceptions, unsupported-object group cleanup, real local mutation producing 12,
coverage/factory coexistence, and constructor abort after a real evaluation
producing 30. All evaluation groups, sessions, accepted developer receipts,
physical work, pauses and opaque handles return to zero before actual helper
exit 0/null. The live factory lease remains until helper closure.

The before/after audit preserves all 1951 inputs and 230 assets, native stderr is
zero, and the exact executable process scan is empty. Immutable receipt:
`/tmp/suitecut-artifacts-ff9863a9-evaluation8-v1/runtime-index.json`, SHA256
`aea703beb6fb90205eb175d1a4d635badf3b69e5f8d412041a3af57676b0b1d1`.
The report SHA256 is
`4e65a8b0e230782296a00e20edd37f870037f09eaa8eda15a08ad0509011c353`.
This proof uses the sealed SDK/native binary without an inventory overlay.
The unchanged exact checker also passes all eight groups on b074f3e2, alongside
the first matching script-inventory6 proof. The newer run preserves all 2002
inputs and 230 assets, strict stderr zero, actual exit 0/null, no remaining exact
executable processes and zero final evaluation/session/receipt/handle ownership.
Its separate immutable index is
`/tmp/suitecut-artifacts-b074f3e2-evaluation8-v1/runtime-index.json`, SHA256
`11defbffb7aa1f0cc080f87cda5cad4dc3973d111ab7601badc4f3f65c97fb48`.
The prior isolated-unit CEF test-reader and hardcoded-pause-ID failures are kept
separately; they did not change production result values or pause ownership.

Behavior reference: the public CDP Debugger.evaluateOnCallFrame and
Runtime.releaseObjectGroup method contracts. No external evaluator implementation
was copied.
