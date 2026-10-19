# Native serial group policy and adapter contract

PW-34 requires ordered serial groups to retry together. The `SerialGroup`
component owns one already-selected outermost serial group. `RunnerRetryPlan`
composes it with the unchanged ordinary retry policy; the native event pump
executes it through existing genuine nested hooks and physical worker fences.
The component alone is not evidence of callback or OS process execution.

The behavioral oracle is Playwright v1.64.0 commit
`606ecd8b34bac100db32561b6278ffd76e5eb3cd`. Only its public documentation and
original tests were read; no external implementation was copied:

- `docs/src/test-retries-js.md`, SHA256
  `073b8a570b80e54a11f73a4277fc8b5d8b966aafbde3bba66cf8e48afe0e30d0`:
  an unexpected failure omits later serial tests; retry reruns earlier passed
  members and beforeAll in a fresh worker.
- `tests/playwright-test/test-serial.spec.ts`, SHA256
  `01b59dc1211d2356625839218437fa0eecfe5255bdc741babda687bd51c884fa`:
  ordinary nested suites belong to the enclosing serial unit; beforeAll and
  afterAll failures replay the unit; expected failures continue, unexpected
  passes fail the wave. The original final `serial fail + skip is failed` and
  `serial skip + fail is failed` controls require retaining actual histories:
  mixed expected/unexpected actual outcomes classify flaky even when the final
  actual member outcome was unexpected and later members were omitted.

The native adapter must resolve outermost effective serial groups from the
original declaration tree after native focus/filter/shard selection. Selected
members keep original catalog IDs and declaration order. The retry allowance is
the maximum effective member retry count, bounded0..100; a replay uses the same
wave index for every member, including members that passed earlier. Full-group
projected results and omissions are bounded8192 and64MiB before admission.

`Begin` accepts one actual native worker identity `{index,pid,generation}`.
Worker indices increase and cannot be reused; unchanged PID alone cannot release
another worker or wave. `Next` takes a process-global attempt generation from the
existing run coordinator, validates it before mutation and never allocates IDs in
JavaScript. Runtime expected-failure amendments require the currently active
exact worker and generation. Native `Finish` takes the real complete attempt
outcome, including associated hook/fixture failures. Infrastructure failures
cannot be accepted as expected test failures.

Unexpected outcome stops new member admission and records remaining member IDs
with the actual failure cause and wave index. These are omissions, not attempts:
there is no fabricated callback token, generation, PID, duration or result. Runtime
skip is an actual expected result and can continue the wave. Static skipped
members keep the existing separate declaration-skip reporting path; the policy
must never manufacture callbacks to account for them.

Logical success or failure leaves the wave in `Retiring`. Before calling
`Retired`, the adapter must close relevant beforeAll/afterAll scopes and fixtures,
settle raw callback ownership, retire file writers/rollback processes, observe
exact actual primary waitpid and finish the shared adopted-descendant batch.
Only then may it replay from the first selected member or publish a terminal
state. The conservative shared parallel-project batch fence remains authoritative.
This component deliberately accepts a retirement receipt from its owning native
adapter; pure component calls are not evidence that an OS process actually exited.

The adapter retains the single run coordinator's original suite deadline,
maxFailures policy, stop timestamp and signal priority. The component has no
clock and cannot grant a new global budget. A native stop forbids further member
or replay admission; an active callback must still produce its actual logical
result and physical retirement. Interrupted outcomes retain interruption priority
rather than being relabeled flaky. Intermediate group waves never increment the
shared final unexpected-test counter. After physical terminal settlement, the
coordinator can charge the component's final unexpected-test count exactly once.

`NativeNestedSuiteDefinition.mode` accepts `default` or `serial`. The host sends
only the declarative `SMODE` row; native C++ resolves the outermost selected
serial group, pins mode bytes in every actual worker signature and owns all
admission, order, retries and final failure accounting. Project-local tests remain
serial even without explicit mode; serial mode adds whole-group failure/retry
semantics. Nested ordinary/default suites inside a serial suite remain members
of that outermost unit. No extra JS scheduler or preflight worker is introduced.

JSON optionally includes native `serialGroups` with original suite/test IDs,
retry budget, final status and omission causes referencing actual failure
attempts. Omissions have no synthetic PID, duration or callback receipt. Group
history validation rejects reordered attempts, reused physical workers across
waves, overlapping groups, invented omission causes and unconfirmed worker
exits. Final flaky classification uses actual cross-wave expected/unexpected
histories, while interruption retains priority. JSON, JUnit and HTML retain raw
actual attempts. No-group summary/classification and ordinary HTML labels keep
the original behavior.

The three native component suites cover policy transitions, composition with
ordinary tests, exact identity/generation fences, skipped subtrees, stop priority,
metadata bounds and real-report validation. The authored public CLI driver has
12 controls: whole-group order and fresh worker/hooks/fixtures, genuine nested
steps and file writers, expected failures on one worker, unexpected pass,
beforeAll/afterAll infrastructure failures, maximum member retry allowance,
mixed-history final failure, final maxFailures, the original global deadline,
actual two-project barrier overlap plus shared physical batch retirement, and
native-only kernel-validated SIGINT. No sleeps or fabricated callback/process
results are used. Separate unchanged no-group project and no-project nested-suite
checks exercise the original paths. These are scoped SuiteCut controls, not an
unchanged upstream/full serial scheduling suite. Fully parallel tests within a
project, isolated retries, shuffle, locks and external file/group scheduling
remain separate gaps.
