# Native named resource locks: bounded contract

The native controller, declaration resolver and project-pool adapter implement
named locks for the existing inline default/serial file groups. The SDK sends
literal declarations only; no JavaScript scheduler computes ownership or order.
This bounded slice does not establish full PW-34 lock parity.

The behavioral oracle is Playwright v1.64.0 commit
`606ecd8b34bac100db32561b6278ffd76e5eb3cd`. Only its public docs and original tests
were read; no external implementation was copied:

- `docs/src/test-parallel-js.md`, SHA256
  `98432f2152901adc12ffb3b5459504d9d93b089259cc396a2ba90f02b5514808`:
  named sets acquire atomically; locks work across projects; enclosing groups add
  inherited names; default/serial groups hold the union for the whole file;
  waiting does not consume test timeout. Lock scope is one runner invocation.
- `tests/playwright-test/test-locks.spec.ts`, SHA256
  `0357bf381438284db8e8b365e9445c85928d78276fd68a7756b6287199ea06bf`:
  same-name exclusion, disjoint overlap, cross-project ownership, inherited and
  multiple names, whole-file default-mode hold, no test-time charge while waiting
  and non-string declaration rejection. The original delay-based test bodies
  are behavioral evidence only; new controls must use genuine bounded IO/event
  barriers rather than sleep-based inference.

The existing inline project is one default/serial file group. Native selection
first resolves original focus, IDs, shards and project support closures. Native
C++ then unions that group's selected test declarations, actual ancestor suites
and enclosing-file names. An unselected sibling's names do not participate.
There is no per-test fully-parallel execution or external file discovery in this
slice. Ordinary default/serial order and whole-group retry policy remain intact.

Names are canonical UTF16LE bytes, including empty names, NUL, non-BMP pairs and
isolated code units. Wire hex case and JSON escaping cannot create different
keys for equal code units. Unicode normalization/case folding is not performed.
An empty string is a real lock name; an empty array declares no names. Duplicate
names collapse before acquisition. Names are bounded1024 code units,64 names per
declarative owner,4096 unique names,65536 references and8MiB native metadata per
resolved invocation. Excess metadata is a visible native error before callbacks.

`ProjectPool::Next` accepts a native read-only readiness predicate. It checks a
project's complete set before allocating a pool reservation. Locked waiting
projects stay Pending, receive no PID/attempt/fixture/output-directory actor and
consume no pool slot. The ready scan may pass a blocked project to admit a
later disjoint ready declaration. Existing dependency/teardown readiness remains
native and authoritative. All locks are reserved with the exact returned
`{projectIndex,projectGeneration,teardown}` before actual worker admission. The
controller stages all map allocation before committing any part of a set.

Release requires the same exact pool admission. `ProjectPool::Retired` is issued
only when its logical Complete and actual WorkerExited receipts both settle;
wrong generation, index or teardown marker cannot retire another owner. A
never-started reservation removed by original global Stop receives a separate
CanceledBeforeStart receipt, not an invented OS exit. The native adapter must
still wait for its actual actor cleanup before releasing that cancellation.
Neither logical success nor a bare waitpid alone releases a held set.

The adapter releases only after genuine raw callbacks, all-hook/test fixture
cleanup, file writers/rollback, primary host waitpid and the shared adopted-
descendant domain retire. Any added output-directory actor must join these same
physical project guards before release. These are the same
physical fences used by serial-group replay and project dependency advancement.
Locks remain held across retry workers until the whole file/project group settles.
The conservative shared descendant batch can delay release behind unrelated
active projects; it must not invent per-project descendant attribution.

Waiting precedes the original attempt/test deadline origin. It remains inside
the single native suite deadline, final-failure limit and actual signal policy;
there is no lock-specific clock reset or JS waiting scheduler. Global stop forbids
new author admission, retains existing physical ownership and cancels pending
reservations without fabricated attempts. With no lock declarations, the existing
ready/admission path remains unchanged.

`check-native-runner-resource-locks.py` supplies twelve authored actual CLI
controls: same-name exclusion through hooks, fixture cleanup, file-writer and
host exits; disjoint overlap through a genuine TCP barrier; atomic multiple
names and blocked-ready scanning; inherited UTF16 equality; original test-ID
selection; waiting outside test timeout using actual bounded crypto work;
whole-group retries; failed dependencies and teardown; failure limits; original
global timeout; native-only SIGINT; and malformed declarations. It checks real
process exit, strict stderr and complete native/SDK/checker input hashes. It does
not prove output-directory actor composition before that actor is integrated.
For an actor stopped before its initial worker starts, original Stop policy
first records the exact native pool cancellation. Unplanned completion accepts
only that same `CanceledBeforeStart` receipt, or a started owner with its genuine
worker-exit receipt before logical completion. Live, stale, mismatched and merely
absent owners remain rejected. Eight separate pure policy controls cover this
repair; a deterministic actual CLI admission-race proof is not claimed.

Optional native JSON `resourceLocks` records contain the admitted project ID,
pool generation, canonical names and either `physicalRetired` or `neverStarted`.
They are cross-checked against selected project identities and actual worker
exit receipts. No-lock reports retain their existing shape. Fully-parallel tests
within a project, cross-process or cross-machine lock namespaces and full
external file scheduling remain gaps.
