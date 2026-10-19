# Native anonymous suite-file groups

This slice admits one genuine existing `--suite` file without a project
declaration when `workers > 1`. C++ resolves focus, original test IDs and the
native shard before grouping. `fullyParallel` and nested default/parallel/serial
modes use the same owner-neutral `NativeTestGroupTree` as declared projects.
The existing `workers=1` path and its report/context shape remain unchanged.
Parallel suite mode inside a serial ancestor remains invalid.

`SuiteFileOwner` canonicalizes the actual input, opens the regular target with
`O_CLOEXEC|O_NOFOLLOW`, and records its native ID, device, inode and observed
size/mtime/ctime version. The descriptor stays open through group retirement.
Native `fstat`/`lstat` checks precede and follow each actual metadata load. An
observed replacement or version change rejects that admission. Configured
symlinks resolve to their actual canonical target. Invalid UTF8 filenames and
nonregular files are rejected. These checks do not pin an import closure or
make JavaScript importing atomic with the open descriptor; transient undetected
path mutation and author-controlled host tampering are not ruled out.

The initial metadata host is reused for the first group. A file group has a
distinct `FileGroupPool::Admission` containing the genuine file token, group
index and native generation. It has no project index or project graph. A maximum
of eight groups share the original run deadline, failure policy, attempt/worker/
step/file/output IDs and physical capacities. Default groups retain ordering;
outer serial groups retain genuine whole-group worker retry semantics. Fresh
workers per group do not establish worker reuse or hook-minimization parity.

The native ready scan skips lock-blocked groups before reserving a slot. Exact
inherited UTF16 lock sets are acquired atomically. Name lookup and holder slots
are preallocated before admission; acquire/release do not allocate. Logical
completion cannot release a started owner. Actual host waitpid, raw callbacks,
fixture/hook cleanup, file/output writer and rollback retirement, and the shared
descendant batch must all settle before native `WorkerExited`. No group calls
foreign `waitpid(-1)`. The conservative shared descendant batch may delay a
retry or the next admission behind unrelated groups. Stop cancels only an exact
never-started reservation and retains started owners through real retirement.

Infrastructure exceptions use the existing preallocated emergency actor/fd
boundary. Original suspended tasks remain alive while native force-close work
retires actual resources under the fixed six-second cleanup grace. The original
global budget is never restarted. Exact terminal file-group receipts precede
held lock release. The original exception is rethrown after verified cleanup;
no successful report is published for an exception path.

Anonymous hosts receive `FILE_CONTEXT` with the native canonical path, exposing
an immutable optional `info.file` snapshot to actual test/hook callbacks. They
receive no `PROJECT_CONTEXT`; `info.project` remains undefined. The SDK performs
literal validation and callback marshalling, with no group scheduling logic.

Optional JSON `files`, `fileWorkGroups` and `fileResourceLocks` are distinct from
project DTOs. File rows report actual device/inode, canonical path, original and
selected test IDs, and the native open/close receipt times. File-group rows
report actual native admission generation and worker indices. `physicalRetired`
requires real started workers; `neverStarted` requires an exact native canceled
admission; `notAdmitted` has neither generation nor workers. No uninvoked test
acquires an invented attempt. Report validation checks full selected coverage,
worker/attempt ownership and exact lock-to-group terminal receipts.

The behavioral oracle and original declaration-mode/retry assertions are pinned
in [the shared grouping contract](test_work_groups.md). New authored component
and public checks are separate evidence; they do not count as unchanged upstream
tests. The public checker covers actual TCP overlap, serial retry, inherited
locks, focus/ID/shard composition, workers1 compatibility, expected/final failure
policy, the original global deadline, actual SIGINT, canonical symlinks,
observed module-file replacement, no-deps rejection, an empty selected shard,
and unknown native IDs. All sixteen authored actual CLI scopes pass on the
private standalone runner and emitted SDK, with strict stderr and unchanged
input audits. Four separate compiled-test-only file-group fault scopes pass at
actual staging, lock admission, descriptor allocation and poll seams. Their
receipts include twelve positive native waitpid observations and two successful
inode-matched unlinkat observations for committed attachment/output rollback.
Eighty-six observed native/SDK/writer/descendant PIDs are absent after closure.

Focused native controls pass optimized and ASan/UBSan/LSan builds: actual file
identity14, file pool17, file locks12, file report24 and two real known-PID
retirement controls. Affected existing project grouping25, pool15, locks12 and
report22 controls also pass both builds. Full-src isolated SDK emission and
typed lint pass for all four modified SDK files. These are private standalone
results; unchanged named-project actual scopes, combined CMake/CTest and an
integrated browser epoch remain separate evidence. First warning-only authored
compile failures and a nonexistent-Node invocation rejected before launch are
preserved in the private proof directory; no assertions or author deadlines were
weakened to obtain the positive results.

File discovery, multiple input files, mixed anonymous/project inputs, worker
reuse, shuffle, distributed scheduling and complete PW-34 parity remain gaps.
