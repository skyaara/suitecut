# Native HAR header finalization

HAR and trace resource snapshots now wait for the real request and response
header receipts when the native event marks `headersReady: false`. A body read
finishing first no longer freezes provisional redirect headers into an immutable
snapshot. The existing logical redirect-hop FIFO remains the only receipt owner;
no URL matching, synthetic header receipt, or additional protocol query is used.

The first observed pending response metadata fixes a 30-second header deadline.
Updates do not renew it. Terminal entries wait only for unfinished metadata and
existing POST/body owners; complete entries publish immediately. Header waiting
is logical work included in context readiness and trace drain, without extending
physical body-reader ownership. Missing native receipts emit an internal metadata
update. Owner closure or deadline expiry exports observed partial headers with
`_headersError` provenance rather than claiming complete raw headers. A missing
readiness field retains the previous behavior for older native producers.

This is a source correction for late-ExtraInfo snapshot finalization. It does not
resolve unrelated request/frame/favicon attribution, cached response wire-status
projection, or every historical HAR/CORS ordering failure. No end-to-end or fuzz
acceptance is claimed by this change.
