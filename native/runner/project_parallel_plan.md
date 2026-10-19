# Native project worker parallelism: next implementation contract

The native `ProjectPool` and C++20 continuation controller are implemented and
component-tested. The native event-pump adapter now admits1..8 project workers
with one coordinator; tests within each project remain serial. Project selectors and primary/support composition are complete before
worker admission; the callback host sends declarations and callback receipts.
It never assigns work, advances dependencies or computes schedules.

## Native authority and stable ownership

One native run coordinator must own the existing original `NativeSuiteDeadline`,
final unexpected-failure count, stop state, original catalog and monotonically
increasing attempt generation and worker index. Moving the present `Runner`
object into one independently budgeted runner per project would reset those
policies and is unsuitable.

A native project admission has `{projectIndex, projectGeneration}`. Each actual
worker has `{workerIndex, pid, projectGeneration}` and its own control socket,
`HookWorker`, `FixtureOwners`, `RunnerSteps`, `RunnerAttachments`, descendants and
immutable project context. A worker slot remains occupied until real waitpid,
control drain, descendant retirement and artifact writer/cleanup receipts settle.
A logical test or project result cannot free a slot or release a dependency.
Stale generations and reused PIDs cannot release a different admission.

Existing callback ownership remains unchanged:
`{testId, attemptGeneration, workerIndex, pid, callbackToken, scopeGeneration}`.
Native step IDs remain process-global and monotonically increasing. Project
retirement also waits for each raw step callback and its actual file writer or
rollback process exit, as required by the existing attachment owner.
Original test IDs stay catalog-global, even across projects, shards and retries.
Attempt generations and worker indices come only from the run coordinator, never
from a new per-project retry-plan counter. The host snapshots `PROJECT_CONTEXT`
before callbacks and retains that immutable context for their entire lifetime.
No mutable global project context is shared between still-live workers.

## Bounded first parallel slice

The smallest useful slice is independent projects on a native worker pool,
with one worker active per project. Project-local tests, suite hooks, worker
fixture values and retry replacement stay serial using their current controllers.
Do not split a project's suites or worker fixtures across workers initially.
There are at most128 project admissions,4096 original tests and a separately
validated finite worker limit. The existing serial mode remains the default.

Replace the planner's single `active_` with bounded admission records. A ready
ordinary project requires all declared dependencies to have passed and physically
retired. Admit ready projects in declaration order while pool capacity remains.
A failed dependency blocks pending dependents without creating attempts or PIDs.
A shared dependency runs once per invocation, including each separately invoked
shard. Effective no-deps mode removes readiness/failure edges as it does now.

The conservative teardown rule can remain: wait until all selected ordinary
projects settle physically, then admit teardown projects in declaration order
within pool capacity. Teardown author callbacks are subject to the same original
stop policy. An exact logical project completion plus all physical worker exits
makes the project terminal; neither receipt alone is sufficient. Retry replacement
reuses the same project admission but receives a fresh native worker index/PID
and global attempt generation.

The event pump polls every admitted worker and artifact descriptor with the
nearest original native deadline. No per-project blocking `Runner::Run` loop,
arbitrary waits or host-side Promise scheduler may serialize or advance the pool.
Global timeout, failure-limit or SIGINT atomically stops new author admission,
then invokes existing bounded cleanup for already-owned work. In-flight failures
can arrive after the cap; they remain actual report results rather than being
suppressed to manufacture an exact count. No fresh project/teardown budget starts
on stop, and no ordinary queued callback is admitted afterward.

## Required proof before exposing workers

Pure native controls must cover shared dependency fan-out, unrelated overlap,
slot saturation, reverse-order failures, shared teardown, no-deps, retry worker
replacement, exact stale-PID/generation rejection, logical-before-physical and
physical-before-logical receipt order, and global-stop races. Actual public CLI
proof must establish overlap using native callback/process receipts and a bounded
author barrier, with no sleep-based inference, then prove dependency and teardown
callbacks begin only after actual predecessor exit. Include artifacts/steps,
worker fixture isolation, original IDs and global attempt generations, maxFailures
and one original suite deadline across all concurrent projects. Preserve the
serial and no-project process checks. Parallel tests within a project, fixture
reuse across projects, external file grouping and full worker parity remain
separate gaps.

## Controller proof scope

`ProjectPool` reserves at most8 active projects. Its exact project generation,
monotonically increasing worker index and actual PID fence logical completion
and real worker exit independently. Failed dependencies block without fake
attempts; pending/never-started owners stop without releasing live workers.
Native controls cover independent ready reservations, saturation, fan-out,
reverse logical/physical receipt order, stale identity rejection, retry worker
replacement, shared teardown, failure propagation and global stop. Worker
identity retirement uses a bounded high-water mark, rather than an unbounded
set of historical worker indices. The continuation controller proves nested
suspension/resumption, preserved native locals, exception propagation and exact
frame cleanup. Neither component alone establishes public parallel execution.

## Integrated adapter contract

`--workers N` and SDK `workers` are thin declarations; C++ owns the ready pool.
Default1 retains the serial project planner while the common native worker
phases use cooperative continuations. One run state owns the original deadline,
failure count and attempt/worker/step/file identities and global admission quotas.
The original budget starts after subreaper/process ownership setup, matching
the prior serial constructor order. The initial real catalog worker is reused
for the first admitted project. All later actual worker metadata is pinned to
that catalog.

There is one subreaper domain for a batch of concurrent workers. Every primary
waitpid and actual file writer/rollback must retire before it begins adopted
descendant cleanup. Project completion, dependent admission, retries and slots
remain fenced until that real cleanup completes. This conservative batch rule
can delay a retry/dependent behind unrelated work. Detached descendant identity
cannot always be assigned to one project; actual nonzero/signal descendant exits
are global infrastructure errors without invented project blame.

Actual public CLI controls establish overlap through an authored TCP barrier,
bounded worker batches through genuine Linux pidfd readiness, actual dependency
and teardown PID retirement, nested step/file ownership, retry after batch
retirement, one global failure cap and suite deadline, and native-only SIGINT.
They use actual worker/author/file receipts; no sleeps or JS scheduler. These
are authored SuiteCut controls, not an unchanged external runner suite or full
parallel parity. Parallel tests within a project, per-project worker overrides
and external file/group scheduling remain unsupported.
