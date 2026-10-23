# Header observations and pending commands

The SDK records native event cursors separately from header-command replies.
When a header command is pending, a later native event explicitly marked `headersReady: true`
owns the canonical observed metadata. The earlier command's eventual successful
snapshot does not overwrite that newer event. Readiness is recorded from the
actual event separately from command replies; an unrelated partial event or a
concurrent command cannot manufacture that event receipt.

A plain `Request.response()` query captures its invocation cursor as a
conservative lower bound. That cursor is not a native read revision, and the
query does not acquire the current JavaScript request cursor when it completes.
A newer canonical response survives an older query's null or metadata result.
Command-origin response updates never create an event-readiness receipt.

A command failure, cancellation, malformed result, or original deadline still
rejects the pending call. A later event does not convert a failed command into a
success. If no newer ready event was observed, the validated command snapshot
continues to update the headers as before.

`headersReady` describes the native observation's readiness. It does not assert
that Chromium emitted an ExtraInfo packet or that every on-wire header is
available. If Chromium explicitly reports no ExtraInfo for a redirect hop, the
SDK retains the actually observed partial fields; it does not infer Cookie,
Host, or another hop's receipt. Source and private broker event sequences are
not compared across owners.
