# Native attachment declaration mutations

This slice implements native admission and final report projection for array-level
attachment mutations. Matching full native-runner/author-process proof is still
required after the host/report hooks are composed; the focused controls below do
not establish complete upstream attachment parity.

Supported array operations are push/pop/shift/unshift/splice, index replacement,
index deletion, length changes within the bound, sort/reverse/fill/copyWithin,
repeated references and sparse holes. Real admitted entry identity survives
reordering. The adapter retains the actual async author scope through
AsyncLocalStorage and rechecks that callback's active ownership on every edit.
Native callback owners carry their original deadline and exact test, attempt
generation, worker index and actual host PID. Hooks can edit the same attempt;
retries and other workers cannot. Retired callback identities stay tombstoned.

Entry metadata is deliberately a frozen insertion snapshot, retaining the earlier
append contract. Direct `name`, `contentType`, `path` or `body` property assignment
is rejected; mutation of the original input object after insertion does not change
the inserted entry. The public body is an author byte copy: writing its bytes does
not change the independently captured native writer bytes. To change entry
metadata or body content, replace the array entry with a new declaration. This
is not a claim of mutable entry-field or original input-object identity parity.
Property descriptors, custom array properties/prototypes, freezing and sealing
are explicitly unsupported. There is no mutable `stepInfo.attachments` surface;
`stepInfo.attach()` retains its genuine step-owned artifact provenance.

`AttachmentMutations` accepts complete sparse ordered reference snapshots under
an exact prior native revision. Declaration references resolve only actual
`AttachmentDeclarations` records; authored paths do not imply existence or copied
files. Artifact references resolve actual validated committed records, and may
appear only after their exact native publication ticket was accepted. Declaration
IDs and artifact IDs are different typed references even if their integers match.
Registering a reference never silently appends it to the current selection.

The adapter stages synchronous author operations and submits proposals. C++ alone
accepts revisions and controls final report order. Rejection keeps the last
accepted view and records the actual native error on the owning callback/step;
it cannot hide a prior hard failure as skip. Genuine author comparator errors
propagate unchanged. Local JavaScript request/proposal maps are protocol
observers, not native ownership or physical-job counters.

Removal does not erase immutable declarations, errors, artifact bindings, native
file jobs, callback/step fences, writer children or output-directory work. Actual
COMMITTED, IPC drain and waitpid receipts retire a file job. Ordinary `attach()`
then requires a separate exact native-issued publication-view ticket ACK before
its public Promise or callback/step fence advances. This internal publication
receipt can arrive after raw author return; closed author scope still rejects new
mutations. Its original deadline is immutable and is the minimum of the actual callback deadline and, for step-owned artifacts, the genuine native step deadline. Expiry records failure; after the
fixed 6000 ms cleanup grace, native code requests closure of only the exact owning
host. Neither timer expiry nor the close request retires a ticket. Only actual
ACK ingress or the real host-exit receipt does so. Existing global-stop and hook
cleanup phase budgets remain unchanged.

Final native JSON adds `attachmentViews`, keyed by exact attempt/worker/PID.
Each row contains native revisions, final sparse slots, admitted references,
accepted changes, rejected mutations and actual publication ticket ACK/death
history. Collection requires real `RunnerAttachments::IdleReady()`. The same
attempt row is upserted across hooks and worker reuse; stale copies are not
appended. Existing `attachments` and `attachmentDeclarations` retain all physical
and error facts even when the final view is empty. HTML renders final accepted
selection separately from immutable history. JUnit carries bounded native view
JSON with an exact UTF8 hexadecimal fallback for XML-invalid scalars. Merged
reports preserve source-qualified original rows without rewriting local IDs.

Bounds are 256 visible slots, 256 native references, 512 accepted changes plus
rejections, and 64 callback tombstones per journal. All journals share 16 MiB of
retained metadata including vector/owner/error capacities; withdrawal does not
refund retained history. Prepared snapshots are separately bounded before commit.
Publication metadata has 128 globally issued ticket records; existing file and
declaration budgets remain independent. Copied native report projection and its
JSON publication are each separately bounded to 16 MiB; these are not claims of
accounting for every allocator or report writer buffer. Allocation/stale/quota
failure does not advance the accepted revision or replace an accepted report row.

Focused evidence comprises policy 26, allocation-refusal 5, publication-receipt 9,
actual included ingress 8 and report validation 24 controls under optimization
and ASan/UBSan/LSan. A real native executable also invokes the production writer
and rollback entry points: held fsync survives withdrawal/replacement/order;
ordinary real file completion remains distinct from publication-view ACK; a
withdrawn EFBIG body retains actual errno and held rollback until real receipts.
Those are native callback fixtures with real child file processes, not Node author
or CEF proof. SDK sender/array tests separately cover staging, explicit metadata
boundaries, async ownership and legacy append/step/skip behavior. Preserved first
checker failures remain in the handoff manifest; assertions and native file/error
receipts were not replaced with simulated successes.

Projection JSON embedded in JUnit uses the same 16 MiB section bound as native
projection serialization, with general JSON scalar quoting. It never passes
through the filesystem-path quoting limit. A controlled >8192-byte projection is
written through actual JSON/JUnit/HTML writers under optimization and sanitizers;
independent XML parsing verifies exact JSON roundtrips, escaped isolated UTF16,
and the genuine XML-invalid noncharacter hexadecimal fallback. The first actual
reuse report failure remains preserved; no requests or history are removed to
fit the old path limit.
