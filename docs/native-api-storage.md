# Native API context storage

An independent `NativeAPIRequestContext` keeps the origin state accepted by its
constructor. Native C++ validates and retains that snapshot, and commits it only
after actual browser storage restoration succeeds within the original creation
and restore deadlines. HTTP responses and cookie methods update its separate,
authoritative native cookie jar. `storageState()` combines those current cookies
with the accepted origin snapshot; it does not recapture or sort the snapshot's
localStorage, IndexedDB records, or OPFS entries.

The `indexedDB` and `opfs` export options include the corresponding imported
fields. Fields omitted by the constructor remain omitted. Neither option changes
array order. Isolated UTF-16 units stay in the existing native-owned `stateJSON`
wire representation, including while native C++ removes unrequested optional
fields. `credentials: true` continues to capture the actual native authenticator
state separately.

Associated `page.request` and `context.request` APIs retain their existing
behavior: they capture the owning browser context's current, mutable origin
storage. This includes genuine localStorage, IndexedDB and OPFS changes made by
browser documents. Their OPFS capture order is unchanged.

The normal storage import limits remain 8 MiB per state, 512 KiB per origin and
1024 origins. Every pending and committed independent API snapshot shares an
additional 32 MiB native allocator budget per helper. This budget measures the
snapshot vectors and strings, including simultaneous allocation replacements and
native raw-JSON projections. Bounded parser scratch, CEF publication values and
allocator bookkeeping are separate; the counter is not a process RSS measure.
Serialized publication JSON is capped at 8 MiB before another origin is retained
in the result. CEF UTF-16 values and bounded temporary copies are not a measured
all-heap quota. Deadline checks run before parsing each origin, before admitting its retained
entry, after snapshot construction, and again before commit/publication. Export
checks bracket each origin projection. The parser is not preempted inside an
individual 512 KiB origin scan.
Diagnostics expose current and peak allocated snapshot bytes and accepted or
pending origin entry counts.

Failed or canceled restoration never publishes its provisional snapshot. Real
utility-browser and profile resources retain their existing physical close/ACK
ownership. Disposing an API context releases its snapshot when its native context
record is retired.

Run the native component check with `ctest -R native-api-origins-snapshot`.
The real-browser verifier is:

```sh
SUITECUT_NATIVE_EXECUTABLE=/absolute/path/to/suitecut-browser \
  node scripts/verify-native-api-origins.mjs
```

The observable contract follows the pinned public
[APIRequestContext.storageState documentation](https://github.com/microsoft/playwright/blob/606ecd8b34bac100db32561b6278ffd76e5eb3cd/docs/src/api/class-apirequestcontext.md#async-method-apirequestcontextstoragestate)
and unchanged public import/export tests. It does not adopt a browser-control
implementation from that project.
