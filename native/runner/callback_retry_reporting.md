# Callback assertion reports

The native controller publishes one compact final `callbackAssertions` record
per admitted assertion. Each real iteration remains an existing `StepRecord`,
linked by `callbackAssertion.id` and its native invocation nonce. The record
counts actual dispatches, separately from test retries. A recovered assertion
does not create a flaky test attempt. Failed iterations remain visible.

Validation reuses the existing step index and checks actual attempt/worker
association, contiguous invocations, immutable parent ownership, raw and physical
receipts, optional fixed assertion caps, and original native effective deadlines.
A later owner timeout amendment may shorten the final cap before an earlier
admission; it does not invalidate historical admission. Accepted inner soft
failures retain their original test/expected-failure authority even when the
actual callback returned and the retry operation passed.

An optional iteration `sourceFence` preserves the actual credentialed challenge,
cuts, generation and ordered original-decoder/ACK/resume receipts. It does not
assert Source process-lifetime retirement. Public attachment ownership stays
unchanged; a separate optional `callbackAssertion` identifies its native retry
consumer. Report metadata never creates a callback, Source or file receipt.

Unused callback fields are omitted from JSON, JUnit and HTML. JUnit adds
properties rather than test cases; its test/failure totals retain the existing
native attempt authority. HTML shows the compact assertion and actual step
history. Uncertain RUN delivery after an exceptional native admission follows
the controller's hard cleanup/publication-failure path, with its original cause,
instead of manufacturing a successful or zero-invocation summary.

The focused report program uses real controlled `TestSteps` and `SoftAssertions`
state, plus explicitly controlled report DTOs. Its optimized and executed
ASan/UBSan/LSan checks and the existing report/HTML programs passed. These are
component checks, not actual Runner, CEF or Source-process acceptance. The
first strict compile warning and its original receipt remain recorded separately.
