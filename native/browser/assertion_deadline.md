# Native assertion original-deadline receipts

This internal slice distinguishes an original native assertion deadline with no
admitted value from an observed mismatch. It does not classify an Error by its
text or turn a generic command timeout into an assertion failure.

The Source adapter admits only a native utility-world scalar DOM matcher on the
main frame, without custom/mixed selectors, a fixed handle, a frame path, or an
accessibility capture. Unsupported families retain their existing error path.
Admission runs immediately before the real query dispatch, after native utility
context readiness, and binds the existing operation generation, absolute
original deadline and complete document identity. An expired context setup that
never dispatched the query remains hard.

The original deadline freezes observation admission and stops query retries. An
already admitted mismatch takes precedence. Otherwise a late actual query reply
must pass native protocol/schema validation, and the actual selector query and
all object-group release receipts must retire before Source can issue the
separate `native-assertion-deadline` failed response. Its `observed:false` means
that no value was admitted, not that an unobserved value was empty or unequal.
No late value, node count or preview becomes `Received`. A predeadline success
whose later publication stalls remains a hard publication error.

Cancellation, document changes, selector exceptions, malformed responses,
transport errors and cleanup failure remain hard. The bounded cleanup watchdog
requests the existing posted page teardown; it never retires physical query or
release ownership by clock alone. Logical cancellation and command-ID reuse do
not establish a physical receipt.

The SDK recognizes a separate private receipt only while decoding the genuine
failed response for that pending native command ID and matcher kind. The public
assertion Error remains the exact object retained by the soft settlement. The
runner consumes a distinct `assertionDeadline` settlement; it must report
`acceptedDeadline` separately from `acceptedMismatch` and preserve ordinary
hard/timeout/cleanup priorities.

The policy and actual included Source/selector-sender tests use controlled
clocks and real CEF values. They do not claim integrated browser behavior. The
Source hooks and paired native-runner adapter must be composed and compiled
before that behavior can be tested against an immutable candidate. Existing
original six-scope soft assertions and their historical negative are unchanged.
