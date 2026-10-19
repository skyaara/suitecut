# Native worker reuse: ownership policy and adapter contract

This unit composes native ownership policy, Linux process inventory and a bounded
SDK worker-reuse adapter. Five authored actual process scopes establish the first
slice below. The pinned proof candidate predates the soft assertion controller;
genuine native/SDK soft idle fences must compose before any combined soft/reuse
claim. It does not establish full PW-34 worker reuse parity.

The bounded adapter target is one actual anonymous suite file with flat,
hook-free fully-parallel groups, no inherited named locks, no declared retries,
and ordinary expected outcomes. Named projects, hooks, retry/serial suites,
multiple-file discovery, and workers retaining browser or other child processes
remain outside this first slice. The original workers=1 path stays unchanged.

`WorkerReusePool` keeps two separate identities: an actual native host lease
`{lease,worker,pid}` and a group borrow `{host,file admission,generation}`.
Successful group completion leaves the actual host charged. Native local fences
must clear raw callbacks, steps and callback receipts, file jobs and callback/
step fences, output actors and handshakes, test fixtures, mutations, and soft
operation contexts. Ready worker fixture leases remain charged and keep their
exact original token/test/attempt identities. A later fixture snapshot must
retain every previous exact lease; terminal records are history, not live work.

Only then may the native coordinator send an idle request with exact borrow,
nonce and original absolute deadline (at most1s and capped by the original global
deadline). The SDK may acknowledge only after its corresponding real queues and
contexts drain. An ACK is neither worker exit nor resource retirement. Repeated,
stale, foreign, late or fixture-count-mismatched ACKs cannot extend the deadline.
Timeout requests cohort drain and does not synthesize readiness or an exit.

The whole live cohort must be quiescent before reuse. The coordinator obtains a
versioned fixed-size probe of all actual hosts and observes the kernel process
inventory on its original main thread. The Linux reader scans `/proc`, verifies
each expected host remains a live direct child, and rejects unknown native
children and children owned by any expected host. Observation errors propagate;
the reader neither becomes a subreaper nor kills/reaps processes. This bounded
inventory is an observation, not a tamper-proof guarantee against future authored
process creation. The adapter must prohibit new author callbacks while idle and
recheck fresh native local fences and retained leases after the actual ACK and
inventory observation, before exposing idle capacity.

An admitted idle transfer records `idleTransferred`; the host PID, physical
capacity and retained worker fixtures remain owned. Reuse creates a fresh borrow
while preserving the same native actor/controllers, global IDs, callback tokens,
fixture leases and actual SDK process. It must not call `WorkerExited`, shared
descendant `Arrive`, or publish physical0 for that live process. Failed outcomes,
stop, incompatible inventory or timeout drain the cohort through the existing
single shared descendant domain. Active unrelated work may finish under its
original policy; idle actors start real worker-fixture cleanup and close.

Only a genuine known-PID `waitpid` status admits `PrimaryExited`. Physical
retirement additionally requires raw/step/file/output/fixture/soft owners to
retire and the actual shared descendant batch to settle. Earlier idle-transfer
history remains immutable after final physical retirement. Repeated identities
or stale generations cannot retire another host. All policy vectors are bounded
and preallocated before actual host admission; post-admission policy transitions
are allocation-free. The coordinator's existing emergency drain must retain all
borrow and host leases until their genuine receipts settle.

The adapter must report explicit borrow-to-live-host transfer chains and record
an actual worker once at its genuine final exit. Existing report validation must
not be weakened to accept repeated workers without those typed receipts. Each
retained controller needs append cursors so terminal history is not duplicated
across group results. Attachment callback readiness includes the joint step and
ATTACH_CONTEXT_RETIRED receipts. Pending soft matcher operations likewise keep
the host charged even when their deadline has expired.

Behavioral oracle: original Playwright v1.64.0 public test
`tests/playwright-test/test-parallel.spec.ts`, commit
`606ecd8b34bac100db32561b6278ffd76e5eb3cd`, SHA256
`820a68ad8e051d4ea365bdcfa03eb0aa3e90aae11c50cdfecf814ec5cb54355b`.
Its beforeAll/afterAll minimization assertions require actual worker reuse; the
hook-free target and component checks do not establish those assertions. No
external implementation or altered public assertions are used.

Focused evidence:34 controlled policy groups cover exact identity, stale/late
ACKs, fixed/original deadlines, cohort readiness, every local fence, retained
fixtures, failed/stop drain, two-stage physical retirement and allocation-denied
transitions. Completion requires the exact currently live host and its current
active borrow; a retired historical borrow cannot gain a late outcome, including
after the same PID is admitted under a new host lease. Genuine completion while
a live host drains remains allowed. Two additional controlled native-child tests use real pipe ACKs,
`/proc`, same-PID in-process state across two borrows, a held worker grandchild,
an unknown direct child and exact known-PID `waitpid` receipts. Both components
pass optimized and ASan/UBSan/LSan builds with strict stderr0. These kernel
controls use a C++ helper, not the SuiteCut SDK or browser, and do not establish
public worker reuse. The first missing-namespace build negative is preserved
outside the repository alongside the corrected immutable input/output audits.


The separate native `RunReusableFileGroups` coordinator uses the immutable native
file-group plan and its own typed host/borrow ledger. It does not weaken the
existing fresh-worker `FileGroupPool` physical-exit semantics. It keeps the actual
catalog host for the first group and retains each compatible actor, fixture
manager, step/file/output controller and SDK process between groups. Readiness
handshakes begin after the whole cohort's logical groups settle, so an unrelated
running callback does not consume another host's idle-ACK budget. Failure drains
the cohort while letting unrelated work finish under original global policy.
No new author callback starts before the native coordinator admits its next
borrow. Same-host record cursors publish each terminal step/file/output record
once; IDs and quotas remain shared across every group and physical host.

The optional JSON `workerReuse` object contains native `hosts` with immutable
lease/worker/PID identities and final actual exit/retirement times, and `borrows`
with original file-group identity, generation, receipt, actual idle request/ACK
and transfer times. A file group may report `idleTransferred` only through that
validated chain. Its host still has to appear exactly once among real final
worker exit records before report publication. A later borrow must follow a
successful exact idle transfer of that same host. Native validation rejects
live/missing/foreign exits, absent or duplicated borrows, forged file identities,
invalid ACK/transfer ordering, and repeated workers without ownership chains.
The no-reuse report shape remains unchanged.

Additional focused checks cover26 native report-chain rejections and four
native ready-worker-fixture/raw/step/append controller controls, optimized and
ASan/UBSan/LSan. Five SDK unit controls establish literal idle packet validation,
queue readiness, retained fixture count, stale nonce rejection and actual send
failure propagation. All changed SDK/test files pass typed lint, and an isolated
full-src emit/typecheck passes. These components do not substitute for an actual
SDK process proof.

The five actual CEF-free public scopes use independent socket barriers and
observed kernel PID handles: two hosts retain the same live PIDs and ready worker
fixtures across four artifact-producing groups; hosts also reuse with no fixture;
a failed cohort genuinely exits before new hosts start while an unrelated held
body finishes without cancellation; actual Node children disqualify reuse; and
causal native SIGINT retires both admitted owners without admitting the remaining
two groups. Captured worker stderr is empty, actual exit statuses are known,
shared generations/step/file/output IDs remain unique, output and file writer
receipts remain genuine, and final fixture callback retirement precedes report
publication. The original CLI/native/SDK/source/package closure is hashed before
and after. The first invalid explicit Node path invocation is preserved as a
prelaunch failure; it launched no native or helper process. The corrected run
resolves and pins the actual installed Node binary.

Hook minimization, nested/default/serial suites, inherited locks, declared
retries/expected failures, named projects, multi-file discovery and live browser
children remain on their existing fresh-worker paths. The adapter's emergency
exception boundary uses the existing preallocated force-close continuations and
shared actual descendant batch; additional reuse-specific allocation-fault
coverage and combined soft-operation readiness remain following proof work.


A genuine exact idle ACK already in flight during Stop, drain or idle expiry is
observed once as a canceled request. It never establishes Idle, enables another
borrow, extends the original deadline or reduces physical ownership. The
original native request, current borrow, live host, fixture count and nonce must
still match; a previously accepted or observed ACK, a foreign generation or a
retired host is rejected. Optional `canceledIdleRequest` and
`canceledAcknowledgedMs` fields record that real canceled-work receipt before
final actual host exit. Admission publishes a new borrow only after validating
it, preserving the previous exact owner if a policy rejection occurs.

Native completion classifies actual unexpected non-interrupted test outcomes
and native cleanup failures before consulting shared stop policy. A genuine
failure that reaches maxFailures stays failed; a sibling with a genuine
interrupted result stays interrupted. Unfinished original-policy cancellation
does not invent a failed test result, and an already completed expected result
is not relabeled solely because another owner stops the shared run.

The additional actual held-ACK control preloads a test-only native syscall
observer into the isolated proof processes. It holds each genuine SDK
WORKER_IDLE_READY write unchanged, sends actual native SIGINT, observes both real
native TEARDOWN sends, and only then releases the captured ACK bytes. This is
process I/O observation, with no SDK method or external driver substitution.
The original39 candidate genuinely exits2 with its preserved protocol error;
the repaired candidate exits130 with stderr0, both canceled receipts, no idle
transfers, two genuinely retired workers and two unadmitted groups. The original
anonymous16 checker runs unchanged on the repaired candidate, including its
original global deadline and failed/interrupted/notRun assertions. All five
ordinary reuse scopes also pass on that same native/SDK pair. The separate
registered held-ACK CTest and all combined soft/reuse proofs remain required on
the following matching integrated build.


The following composition imports the genuine native soft controller and SDK
bridge without replacing either controller on a live reused PID. Native idle
requests, inventory confirmation, final host retirement and report publication
require the controller's actual Physical/IdleReady fences; every actual result
generation is checked for accepted soft mismatches before reuse. Each retained
actor appends its soft-history delta once. The SDK idle snapshot requires all
four real soft counters/readiness fields, preserving joint step/append/soft
callback retirement. Accepted error history does not count as a living raw
operation, but an accepted mismatch disqualifies reuse.

Focused composition evidence covers three native controller/core fence groups
optimized and under sanitizers; 33 SDK controlled idle/soft units, full isolated
SDK emit/typecheck and typed lint of every merged changed SDK/test file; the
actual native soft protocol10, ordinary reuse5, genuine held idle ACK1, and
unchanged anonymous16 on the same compiled runner and matching emitted SDK.
Two additional actual native/SDK controls await genuine source-tagged soft
operation settlements across same-PID reuse, and prove an accepted soft mismatch
forces physical cohort exit before fresh workers. These controlled mismatch
producers make no DOM/browser matcher verdict claim. The initial new checker
incorrectly expected an empty softAssertions property for passed-only runs; the
failure is retained and the checker now accepts the serializer's existing
omission of empty error history. Matching combined CMake/public gates remain
required on Root's following integrated epoch; browser matcher6 remains a
separate pending proof against the new browser candidate.
