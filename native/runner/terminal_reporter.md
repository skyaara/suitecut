# Native terminal reporters

`--reporter json` (or `runNativeSuite({ reporter: 'json', ... })`) writes one
final native JSON document to stdout. It is the same validated JSON document
committed to `--report`, including exact errors, UTF16 titles, actual worker
output bytes and artifact references. Directory runs emit their source-qualified
merged document once; listing emits its declaration document. `--reporter none`
suppresses native console summaries. Omitting the option keeps the existing
console behavior.

Native C++ retains a read descriptor for the actual atomic publication inode.
The emitter streams it after process, Source, callback and artifact retirement;
it neither reopens the path nor reconstructs callback events from final results.
Other requested report files must finish before stdout emission. A reporting
failure leaves original committed files available and produces a nonzero exit
with the actual execution exit included in its stderr diagnostic. Stdout may
contain a partial JSON document if its consumer fails during emission.

Pipe/device output uses an independent nonblocking descriptor; socket output
uses nonblocking sends. Parent descriptor flags and regular-file offsets retain
their normal meaning. Native stop/deadline checks run between bounded chunks
and during backpressure. Already-stopped execution may report immediately
writable results but cannot wait for downstream capacity. Final publication has
a separate cooperatively checked five-second cap; it never renews an author
callback or suite budget. Blocking filesystem system calls retain their native
kernel semantics; this does not promise forced physical interruption of a
blocked kernel write. The retained handle identifies the committed inode and
length; it does not serialize unrelated external writers to that inode.

This supports native JSON and quiet terminal output. Custom live reporter
callbacks and Playwright reporter JSON schema compatibility are separate work.
