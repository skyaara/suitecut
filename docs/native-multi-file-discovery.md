# Native directory discovery and coroutine cohorts

The native runner accepts exactly one `suitePath` or `testDir`. Directory mode
uses native filesystem discovery and native `testMatch`, `testIgnore`, and
filename filters. The SDK only serializes options and runs genuine callbacks.
A declared glob is not interpreted by JavaScript. Omitted `testMatch` chooses
`.spec`/`.test` JavaScript/TypeScript extension families; an explicit empty
array selects no files. Symlink/special-file traversal and extended globs are
currently excluded. Native file regular expressions use the shared bounded native matcher and its
validated flags; compilation and matching poll the original stop/deadline.

Discovery is sorted and bounded to 128 files, 32768 examined entries, depth64,
16KiB paths and 2MiB retained pathname bytes. Real opened file owners preserve
canonical path, device, inode and file-version receipts across collection and
execution. These checks do not claim atomic Node import or pin every transitive
module. Native input paths and report destinations retain their existing IO
validation.

One outer C++ coordinator collects every actual module before resolving global
focus, exact selected IDs, sharding and the project dependency/teardown graph.
Original declaration IDs and callbacks stay local to their real module; report
identity is `(sourceId, original local ID)`. Genuine Node hosts perform each
module's collection and execution. Collection admits no authored fixture/hook/
test callbacks. Importing a module still executes its top-level authored code.
Every execution revalidates the exact collected metadata transcript and file
owner before native selected callbacks can begin.

File/project actors run in native cooperative cohorts bounded by `workers`.
Every ready cohort keeps its actual actors, task frames and file pins until
host, descendant, IO, callback and Source retirement is confirmed. The existing
global physical fence then admits output retention and atomic source reports;
only afterward can successful dependencies open the next cohort. This uses
bounded ready cohorts rather than work-conserving refill. Collection still runs
serially, and each executed module is imported separately for execution so global
focus and file/transcript validation remain authoritative. Tests within each admitted
file/project actor remain sequential; combining intra-file `fullyParallel` groups
with the global file worker budget is a separate remaining scheduling slice.

If any collected test, suite or file declares a resource lock, the native
coordinator conservatively sets `activeFileLimit:1` for that invocation and
reports `resourceLocksSerializeFiles:true`. Parallel named-lock scheduling is a
remaining throughput gap; lock exclusion and existing functional behavior stay
intact. Unlocked invocations use the requested native worker ceiling.
All actors run in the same native coordinator process and share the exact
existing `RunState`: one original suite clock, worker/attempt ID allocators,
maximum-failure authority, native declaration and metadata budgets, attachment
file/publication budgets, output IO credits, step/soft journals and retained
Source leases. No quota is multiplied per file and no authored counters become
native resource ownership. A final unexpected attempt updates the shared native
failure policy before another attempt can start. Retry provisional failures do
not consume a final-outcome failure slot. The next cohort cannot start until real
host/descendant/file/step/soft/publication/Source retirement fences have closed.

`No tests found` is decided once for the unsharded global selection, with
`passWithNoTests`/`--pass-with-no-tests` as the explicit exception. An empty shard
of a nonempty primary selection succeeds. Local file partitions do not impose
independent no-tests rules. At most 128 execution sources are admitted; a larger
plan fails before callbacks and records its selected segments as not run.

The final atomic JSON report uses the existing native merged-report format plus
a typed `directory` policy sidecar. It preserves actual file identities, complete
collection protocol bytes, actual Node worker exits/streams, global-to-local test
and project mappings, native actor generations, final shared budget receipts,
selected not-run IDs and original errors. In-process actor retirement is not
presented as an invented process waitpid. Source reports preserve original
attempt, step, fixture, artifact, output and Source histories. Passed actors must
match actual terminal attempt/skipped IDs; declarations and unrun rows cannot
serve as successful execution coverage. Failed/signaled collection workers,
failed source reports, selected not-run segments or global coordinator errors
fail the overall result. JUnit and HTML retain the directory policy separately
from authored test evidence.

Owned focused checks:

- `suite_discovery_test.cc`: native traversal and file selection boundaries.
- `multi_file_catalog_test.cc`: global focus, shard and project selection.
- `multi_file_plan_test.cc`: exact admissions, max-failure and physical fences.
- `directory_report_test.cc`: controlled genuine-coverage/retirement validation.
- `check-native-runner-directory-fault.py`: test-only descriptor-allocation
  exception with a real held writer, exact-inode rollback, host and detached-child
  waitpid receipts, and truthful refusal to publish an incomplete source.
- `check-native-runner-directory.py`: actual native public CLI/Node collection,
  callbacks, native attachment publication, shared declaration limits, original
  deadline, project support/teardown and complete process-tree closure.

The older private exec/socket experiment is not activated or shipped in this
slice. No browser, heap, opaque-object bridge or deferred custom CEF capability
is involved.

Config grep eligibility is preserved separately from primary CLI grep eligibility.
Selected support projects filtered to no tests retain their graph identity and
complete logically only after their genuine dependencies and teardown barriers;
they create no test, host, source or waitpid receipts. Cancellation leaves pending
no-work projects explicitly not run. Named-project fixture defaults use the exact
file-pinned native project admission. Exceptions in the actor descriptor/poll driver
drain accepted physical work before coroutine/actor destruction; report publication
is refused if no truthful completed source report exists.

The `timing` rows record actual native monotonic actor admission/completion and
host admission, decoded-metadata, close-start and waitpid-capture observations.
A host's identity includes its globally allocated worker index and PID. An
admission-to-metadata interval includes Node import and native protocol parsing;
close-to-capture includes real fixture/descendant cleanup. Concurrent interval
sums overlap and are not wall time. The small performance checker compares two
and eight files with workers one versus two/eight on identical Runner, SDK and
inventory bytes; it does not substitute authored timestamps for native receipts.

Completed successful file outcomes remain successful when a sibling later stops.
The directory-only predicate requires the real plan's final expected outcomes,
no selected unrun IDs, no own global error and no remaining physical work.
Actual completed failures retain precedence over interrupted sibling outcomes.
Source report coverage is selected by exact actual report-worker/PID ticket
membership, replacing the serial-only ledger suffix assumption; the global
Source-outstanding publication fence remains required.

The focused concurrent process checker covers seven actual native runs: genuine
two-body rendezvous, a two-worker ceiling across four files, immediate global
max-failure stop, completed-pass versus peer SIGINT attribution, conservative
lock exclusion, retention after real cohort retirement, and overlapping shared
declaration admission. Every run requires original process-tree retirement,
zero final native physical counters and exact source-worker timing coverage.

A local same-binary O0 measurement (one sequential observation per configuration)
recorded two files at 1.965s with one worker versus 1.732s with two; eight files
at 6.221s versus 4.212s with eight. Native execution-cohort intervals were
648/448ms and 2812/717ms respectively. Collection remained serial, approximately
0.63s for two files and 2.79s for eight. These are scoped local measurements,
not production O3 latency guarantees; full collection and execution each import
the real module, and collection/host retirement remain material costs.
