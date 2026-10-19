# Native reader and stdout publication receipts

These bounded components account for actual native stream boundaries. They are
not wired into production Source in this commit and grant no Engine idle or
retained CEF-child reuse capability. Renderer timers/listeners/page state may
remain resident under their actual worker owner; no JavaScript quiet is assumed.

Reader admission precedes reply-credit waiting/task allocation. Actual UI entry
and route stack return are independent of the actual post return, including UI
execution before CefPostTask returns. A throw with uncertain post acceptance
poisons barrier authority. Exact issued cuts reject altered, foreign and stale
receipts; later admission does not retroactively join an earlier cut.

A move-only packet receipt remains charged after native dequeue. Only actual
positive write byte counts can reach full publication. Queued frame/audio drops
have separate explicit native dispositions. Unwritten/abandoned/partial packets
poison authority and never count as fully written. The `writing` snapshot field
means dequeued and not fully published, including retained failed publication;
it does not assert an OS thread is currently executing write().

The private SDK DTO/inbox matches exact canonical uint64 native cuts on original
stdout with an authenticated Source expectation. Either control/stream ordering
is supported. Observations remain charged until the actual matching native ACK.
This bookkeeping alone proves no Engine idle, child exit or lease transfer.
No existing browser API/decoder or host protocol is changed by this commit.

Native controls cover policies, C++ owner lifetime and genuine socket prefix,
payload and EPIPE boundaries. SDK controls cover ordering, forged/stale cuts,
foreign source tickets, exact uint64 validation and bounded in-flight/history.
Actual Source Writer/reader hooks, full Engine physical/controller/task ownership,
original marker through a real SDK/helper and known CEF cohort transfer remain
separate following work; registered CEF children still require fresh hosts.
