# Native test work groups: policy and adapter contract

This unit defines native grouping, typed logical/physical receipts, and the
declared-project runner adapter with its declarative parallel API. Authored
actual CLI checks establish the bounded behavior below; they do not establish
full PW-34 parity. No JavaScript grouping or scheduling implementation participates.

The public behavioral oracle is Playwright v1.64.0 commit
`606ecd8b34bac100db32561b6278ffd76e5eb3cd`. Its original public docs were read:
`docs/src/test-parallel-js.md` SHA256
`98432f2152901adc12ffb3b5459504d9d93b089259cc396a2ba90f02b5514808`, and
`docs/src/test-api/class-test.md` SHA256
`3fe4f9899bae52ac9f7e20a29fcc273d9833320927303cc656ef031ec84d935a`.
The docs specify root/project fully-parallel configuration, parallel suite mode,
explicit default-mode overrides, per-test worker isolation and relevant hooks,
and serial groups replayed as one unit. No external implementation is copied.
The original `tests/playwright-test/test-parallel.spec.ts` SHA256
`820a68ad8e051d4ea365bdcfa03eb0aa3e90aae11c50cdfecf814ec5cb54355b`
requires distinct actual worker indices for three concurrent tests and rejects
parallel mode inside a serial suite, including empty declarations. Its separate
worker-reuse hook-minimization assertions are not established by this policy or
by a fresh-physical-worker-per-group adapter.

Native selection resolves original IDs, focus, project support and shards first.
`TestWorkGroups` then resolves literal suite modes in original declaration order.
A parallel region creates one group per selected test. Explicit default mode
creates an ordered subgroup until another parallel region. An outermost serial
suite inside a parallel region is indivisible, including selected nested serial
suites. Existing serial suites within an ordered default group remain inside
that group, retaining their existing whole-unit retry semantics. Parallel mode
inside a serial ancestor is rejected. Unselected siblings never enlarge a group
or its inherited lock union. Empty selected projects retain a real empty-group
admission/cleanup path, without inventing test attempts.

`WorkGroupPool::Admission` has distinct group index, original project index,
monotonic group generation and teardown marker. It is not a project admission.
The pool holds at most eight active groups, bounds groups to8192, selected tests
to4096 and projects to128. A selected project's groups may overlap. Dependency
and teardown readiness use the aggregate project result only after every group
has both a logical result and its physical worker retirement receipt. A failed
group does not fabricate attempts for unadmitted siblings or block a dependency
before the remaining sibling groups retire. The existing conservative teardown
rule waits for every selected ordinary project to settle.

Native readiness scanning skips a lock-blocked group before reservation, so it
consumes no worker slot, attempt, fixture or output actor. It may admit a disjoint
later group. Each selected group's inherited UTF16 lock set is atomic. A group's
reservation remains held through actual retry workers; project or test failure
alone never releases it. Original global timeout, final-failure limits, SIGINT,
global attempt/worker/step/file/output counters and quotas remain coordinator
owned. Waiting does not create a test clock or restart the global clock.
`WorkGroupLocks` uses the distinct native work-group receipt type and leaves the
existing ordinary-project `ResourceLocks` implementation unchanged. Native
`WorkGroupLockNames` resolves the inherited union from each selected group's
original test IDs. It shares the existing bounded UTF16 name/namespace/metadata
rules, with no SDK-provided ownership, readiness or release callbacks.

The adapter must supply `WorkerExited` only after actual known-PID host waitpid,
raw callback/fixture/hook completion, file writer/rollback, output-directory actor
retirement and shared descendant-domain retirement. It must retain the existing
single shared native subreaper batch and must not call foreign waitpid from an
individual group. That conservative batch can delay group retries or dependent
admission behind unrelated groups. Stop cancels only never-started reservations
with an exact `CanceledBeforeStart` receipt; live owners remain charged until
their genuine completion/exit receipts settle. Stale group/project/generation/
teardown/PID identities cannot retire another group's owner.

Adapter report contracts must keep project IDs separate from work-group IDs and
include original selected test membership and real worker indices. Named-lock
records need genuine work-group identity when several groups share one project.
The existing no-parallel path and report shape remain unchanged. Per-attempt
output roots stay assigned after the original attempt start and before hooks or
fixtures; `OutputDirectoryOwner` and `StepOwner` continue to use the shared actual
test/attempt/worker/PID identities. No new SDK-controlled scheduling receipt is
accepted. External file discovery, cross-machine scheduling, shuffle and broader
isolated-retry scheduling remain separate gaps.

The declared-project adapter retains genuine project identities. The separate
[one-file anonymous adapter](file_work_groups.md) uses an actual native file
owner and distinct typed admissions/reports; it does not fabricate a project ID
or metadata. Both paths share the owner-neutral declaration grouping rules.
Single-worker execution retains its original path. Multi-file discovery and
mixed anonymous/project input scheduling remain separate gaps.


The adapter adds `fullyParallel?: boolean` to root/project declarations
and literal `mode: 'parallel'` to nested suites. Explicit project `false` wins
over root `true`; explicit nested `default` forms an ordered subgroup. The SDK
only validates and sends literal metadata. C++ resolves selection, grouping,
readiness, admission, retries, global policy, locks and physical retirement.
The initial actual catalog host is reused for the first admitted group.

The optional JSON `workGroups` array keeps `id`, `projectId`, nullable native
`generation`, nullable `suiteId`, `mode`, terminal `status`, selected `testIds`,
and actual `workerIndices`. Receipt `physicalRetired` requires genuine admission
and actual worker exit; `neverStarted` requires exact native cancellation with
no worker; `notAdmitted` has neither generation nor worker and describes only
blocked or unrun declarations. Native report validation checks complete selected
membership and prevents attempts or workers from crossing group owners.
Group lock rows use `workGroupId` and `workGroupGeneration`; ordinary project
lock rows retain their existing `projectGeneration` shape. The authored public checker has fourteen actual CLI scopes: three tests in one
project overlap at a real TCP barrier; explicit project false/default overrides;
outer serial whole-group replay; inherited locks with disjoint admission; all-
group dependencies/teardown with real output/file/host/descendant retirement;
failed dependency; focus/ID/shard composition; workers1 original path; expected
failure accounting; maximum failures; original global timeout; actual SIGINT;
and parallel-inside-empty-serial rejection. Each scope requires real process
exit, strict stderr and unchanged inputs. Separate controlled report tests reject
stale, foreign or fabricated group, worker and lock receipts. No unchanged
upstream driver or integrated-browser epoch result is claimed by these checks.

Coordinator infrastructure exceptions use a shared emergency boundary in both
project and work-group paths. Actual actors and their original suspended tasks
remain owned while precreated native force-close continuations cancel live jobs,
reap actual hosts, run inode-checked file/output rollback, and retire the one
shared descendant batch. Fixed actor/descriptor storage and the emergency reason
exist before actual workers. No new author hook/body is admitted. One six-second
physical cleanup grace never restarts the original global budget. Cleanup must
prove exact actual owner retirement before releasing a held lock; ordinary and
emergency release immediately clear the local ownership flag. Re-observing an
already-arrived descendant lease requires the same generation, worker index,
host object and PID and still waits for the real batch retirement.

After verified emergency cleanup, the original exception is rethrown and the CLI
exits2 without publishing a successful report. Unconfirmed cleanup refuses
publication. Dedicated test-only builds inject at actual staging, lock admission,
descriptor allocation and poll operations; production has no fault CLI or
environment API. Eight actual fault controls cover both scheduling paths, with
real positive known-PID waitpid receipts and successful unlinkat receipts matching
committed-file/output device+inode identities. Helpers retain their genuine
fd3/control protocol and OS PIDs; the observer never fabricates an exit or native
artifact receipt. These checks supplement the unchanged normal-path14 scope.
