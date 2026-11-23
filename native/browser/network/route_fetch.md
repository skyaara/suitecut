# Native route fetch

`NativeRoute.fetch({ url?, method?, headers?, postData?, timeout?, maxRedirects?, maxRetries? })`
fetches the actual paused request through the native HTTP controller without
resolving the interception. The result is a genuine `NativeAPIResponse` and can
be passed to `route.fulfill({ response, json: replacement })` or read/disposed
through its existing methods. Calling `fetch` again remains possible until the
route is resolved. One fetch may be active for a pause at a time.

The native pause supplies the original URL, method, headers and exact body.
Both page routes and context routes forwarded to that actual page owner use
this path. Independently owned worker routes use their real context broker and native paused
session. The broker must still own the exact worker control generation, and its
retained CEF request context must match the current native browser-context
registry. Missing, closed, foreign or ownerless workers reject; they never borrow
a page or the root context. The native HTTP profile shares the actual browser
context's cookies, proxy, TLS and credentials policy; it is not an SDK transport.
Native HTTP owns redirects and connection-reset retry decisions. Defaults are
unlimited redirects (2,147,483,647), zero retries and 30 seconds. Redirect and
retry counts and the millisecond timeout accept any nonnegative signed 32-bit
value.
Zero timeout remains bounded by the original Source operation and paused route.

The original pause, Source operation and explicit fetch cap are intersected.
Large explicit body upload freezes that cap at its existing native reset-body
admission, before subsequent chunks. Cancellation/expiry/page or session loss
cancels actual native HTTP IO. Shared native leases remain held by real HTTP
state and outstanding cookie callbacks until their actual retirement. Fetch
does not create a successful browser response event or complete the intercepted
request; only a genuine continue/fulfill/abort command can do that.

Request, fulfillment and response bodies have no size limit of suitecut's own.
`postData` supports string or Buffer, not object serialization. Exact inherited
transferred bodies are retained whole. Response bytes require explicit
`response.dispose()` when no longer needed.
HTTP(S) only, context-wide HTTP policy and existing HTTP connection/cookie
semantics remain the bounded contract; no general request replay/parity claim.

The policy/NetworkControls component uses a controlled HTTP producer to check
native admission, cancellation and actual C++ lease lifetime. SDK controls use
controlled native command replies. These controls and HTTP translation-unit
syntax are not browser/HTTP acceptance: the shared matching Source build and
actual local HTTP page/context/worker route proof are required before activation.

A matching actual worker execution-context destruction/clear also cancels its pending fetch, including a pause intercepted on the still-live parent session. This does not grant parent-channel closure or release the HTTP physical callback lease early.
