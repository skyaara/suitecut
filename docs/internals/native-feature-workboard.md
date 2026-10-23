# Native feature implementation workboard

## Latest callback retry build a238fec

Native callback retries are integrated at production `a238fec`, with Runner
`343b915d`, Source `d29461c1` and merge tool `c3889a7e`. All native targets and
strict frontend compilation pass. Eight affected registered checks and four
affected public scenarios pass; the earlier first three public scenarios retain
their separate `14b8e08` positives. Current Source acceptance passes two genuine
browser tests with three ordered retirement/resume fences. This is composed
incremental evidence among 517 registered checks, not a fresh full-suite run.

`expectNative(callback).toPass()` uses native invocation, interval, deadline and
physical-retirement control. Global cancellation now holds cleanup through both
managed invocation and parent callback retirement under the original budget.
Twelve charged/clockless native lifecycle compositions pass optimized and executed
sanitizers. Using toPass in fixtures/all-hooks, nested toPass, callback modifiers and
`expect.poll` remain unsupported; Source fencing remains conservative over the
whole Engine, without concurrent Source isolation or browser-child reuse.

The unchanged SDK has 1,276 passing tests in 163 files, full type checking, and
composed lint evidence (prior full lint plus typed lint of the only changed file).
All 692 emitted outputs and 1,269 SDK quality inputs are unchanged in the final
native-only repair; independent review checks 2,801 unique recorded paths and 230
runtime assets. Network eviction avoids repeated body-metadata serialization in
one admission. No end-to-end latency speedup is inferred from test wall times.

Original failures remain recorded, including timeout315 and historical cleanup
negatives. Automatic snapshot baseline updates and broader cookie-date parsing
remain blocked by automatic review; the custom CEF build remains user-deferred.
Full feature parity is unfinished. Compact evidence:
`.suitecut/diagnostics/native-to-pass-a238fec-result-v1.json`.

Current Source `d29461c1` also passes one authored, unfiltered network attribution
diagnostic: child-frame and main-frame fetch requests each reach the real origin
once with exact frame/context identity and one successful terminal event. A
separate favicon attempt fails with `net::ERR_ABORTED` before reaching the server.
All extra traffic is retained. The five-check scope preserves 2,045 input hashes
and retires all 15 observed process identities. Generic zero command counters
coexist with retained network metadata; actual helper exit supplies the separate
physical evidence. Historical 57-case results and broader redirect/popup coverage
remain unchanged. Evidence: `.suitecut/diagnostics/native-network-attribution-current-a238fec-v1.json`.

## Previous native build 64cc681

Native candidate `64cc681`, Runner `1a7dafcc` and merge tool `8ac59db7`, passes
all native build targets, strict frontend compilation, 1,229 SDK tests in 159 files,
type checking and lint. All 29 affected native checks pass once with 3,136 input
hashes unchanged. Independent review separates 124 genuine Runner report artifacts
from 33 controlled DTO fixtures and verifies 612 recorded processes absent,
including zombies. Source `4f8fefac`, all 1,116 compiled dependencies and 230 runtime
assets are unchanged; 680 SDK outputs and 1,954 unique compiled/SDK hashes are pinned.

`--fail-on-flaky-tests` and the strict SDK boolean `failOnFlakyTests` now apply
native final-run policy after actual execution and retirement. Five genuine CLI
scenarios and two merges preserve retries, original expected/flaky statuses,
maxFailures, later/dependent/teardown execution and serial/repeat identities.
Typed JSON policy is derived and validated natively; JUnit uses properties
without synthetic cases, and HTML/console explain the final policy outcome.
The merge tool preserves source policy and supports explicit aggregate policy.

Polling capacity reuse and indexed directory test-row serialization remain
integrated. The preceding paired component workload measures 15.30→0.80ms per
two full report writes at 4,096 tests, with identical bytes; warm descriptor
collection uses zero allocations in the measured actor workloads. These are
scoped observations, not whole-suite latency guarantees.

This is unsealed scoped evidence among 504 registered checks, not a full rerun
or full parity. Original 315 remains unresolved and was not retried. Historical
sanitizer/interrupt cleanup negatives remain, including poll assertions passing
with outer cleanup 125 from two adopted/reaped SIGKILL children of unknown lineage.
Automatic snapshot baseline updates and broader cookie-date parsing remain
pending after automatic review rejection; their inactive foundations are
preserved. The custom CEF build remains explicitly deferred.

Compact evidence: `.suitecut/diagnostics/native-fail-flaky-64cc681-result-v1.json`.

## Previous performance candidate 1c1f769

The preceding performance candidate `1c1f769` passed 27 affected checks. Its
report comparison, descriptor allocation controls and qualified sanitizer result
remain in `.suitecut/diagnostics/native-runner-perf-1c1f769-result-v1.json`.

## Previous incremental build 2062c50

The current native build `4ea60ac` with checker `2062c50` passes all native
build targets, 1,213 SDK tests in 158 files, type checking, lint and all 17 affected
native checks. Runner `5d63fbd2` passes 14 repetition and nine parallel-group
process scenarios. Same-repeat tests genuinely overlap within one file using the
original global worker pool. Default/parallel fixture lifetimes, serial-group
retries, cross-file/project barriers, stable public identities and declared
project counts preserve the original deadlines and failure limits.

Indexed repeat planning is integrated: its component measures 98,827 comparisons
for 4,096 declarations; the prior 41,945,088 count is source-derived. This is not
an end-to-end latency measurement. Four existing snapshot callbacks and two
mutable-suffix scenarios also pass. Source `4f8fefac` and all 1,116 compiled
input hashes remain unchanged. Root rechecks 1,947 unique compiled/SDK hashes;
the process supplement verifies 243 known PIDs absent, including zombies.

These are scoped results among 502 registered checks, not a full-suite rerun or
full parity. The earlier output-preparation timeout, unrelated sanitizer cleanup
negatives and interrupted compiler cleanup records remain unresolved. No authored
deadline, original assertion or physical retirement requirement was relaxed.

Compact evidence: `.suitecut/diagnostics/native-parallel-repeat-2062c50-result-v1.json`.

## Previous incremental build d48b6b8

The current native build `3f85de7` with checker `d48b6b8` passes all native
build targets, 1,211 SDK tests in 158 files, type checking, lint and 18 affected
native checks. Runner `22721d90` passes all 14 genuine repeatEach scenarios,
including original global budgets, retries, raw zero, project roles and shard
membership. Each repeated actor currently executes its selected tests serially;
parallel test-group activation remains separate private work.

Mutable bounded snapshot suffixes pass two real callback scenarios, including
beforeEach/body/afterEach state; four existing snapshot callbacks also pass.
Source `4f8fefac` and its 1,116 compiled input hashes remain unchanged. The 18
checks are scoped evidence, not a rerun of all 499 registered native checks.
The preceding original timeout and separate sanitizer-cleanup negative remain
unresolved. The first O3 compiler-warning failure is retained alongside its
reviewed explicit-initialization fix; no warning or authored deadline was relaxed.

Compact evidence: `.suitecut/diagnostics/native-repeat-snapshot-d48b6b8-result-v1.json`.

## Previous incremental build 9373a3e

The incremental `9373a3e` build passes all native targets, 1,200 SDK tests in
157 files, type checking, lint and seven affected native checks. Runner
`cb3b01bc` includes shared identical snapshot plans, corrected default ARIA paths
and direct descriptor aggregation. Source `4f8fefac` is byte-identical to the
preceding build. Actual emergency fault checks retire cleanly; 705 driver pins
stay unchanged and a subsequent review rechecks 1,934 compiled/SDK hashes.
This is scoped incremental evidence, not a second full 492-check run.

The preceding full run and unresolved timeout below remain unchanged. A separate
unchanged output-directory sanitizer control has an unresolved cleanup negative;
its assertions passed, but three adopted SIGKILL children lacked exact lineage.
That raw result is retained separately from the passing descriptor actor proofs.
Two static-tool attempts also hit custom JavaScript heap caps; those resource
failures remain recorded alongside the successful type/lint invocations.

Compact evidence: `.suitecut/diagnostics/native-snapshot-descriptor-9373a3e-result-v1.json`.

## Preceding common build 6ebe3ca

The common Linux build at integration `6ebe3ca` builds all native targets and
passes 1,196 SDK tests in 157 files, type checking and lint. Source `4f8fefac`
and Runner `e909be10` completed one full 492-check native run: **490 passed,
two failed, none skipped**. All 1,204 frozen input hashes stayed unchanged.
The raw supervisor reports cleanup failure for two deliberately killed children;
their exact PID/start identities and SIGKILL wait statuses match the existing
runner-death fault. All recorded processes retired. This is an unsealed build,
not a new all-gates pass or full feature-parity claim.

The directory composition failure expected the wrong diagnostic. Its earlier
native catalog guard correctly rejected retry-time configuration drift. The
checker now requires that exact error; its four-case affected scope passes once
on the same production executables and SDK. Original 490 positives plus this
following result leave **one unresolved timeout scope**, not a fresh 492-pass run.

The timeout occurred during output preparation before fixtures or the author's
`beforeEach` timeout amendment. The original 100 ms deadline remains unchanged.
A single affected-case syscall trace did not reproduce it; that trace used a
different filesystem as well as tracing overhead, so it does not establish a
cause or a fix. The original failure and successful rollback remain recorded.

Matching common Source/SDK HAR11 and clock11 checks pass with unchanged input
hashes, empty stderr and actual process retirement. HAR covers strict legacy
cookie expiry forms, not the full cookie-date grammar. Clock includes a genuine
25-hour runFor jump, not broader page/context clock parity.

The compact combined native result is
`/tmp/suitecut-current-native-integration-composed492-v1.json`; the raw full run
is `/tmp/suitecut-current-full-native-ctest492-v1/`. Matching build/SDK provenance
is `/tmp/suitecut-native-feature-next-batch-v1/final/`. The latest fully sealed
baseline remains `0d116d39` (423 native checks, 1,133 SDK tests); historical
negative records remain separate from subsequent scoped results.

## Integrated capabilities in this batch

| Capability | Current evidence and limits |
| --- | --- |
| Concurrent directory execution | Registered serial and parallel directory checks pass. Bounded ready cohorts share native budgets and real physical fences; collection remains serial; native group records now permit intra-file parallel execution in the same global pool, and resource locks conservatively serialize file actors. |
| Project output roots | Registered public scopes pass, including reversed local project order, fallback roots, held-peer retention, real bad-root failure and retry catalog drift. The final composition scope uses the corrected exact-error expectation described above. |
| Explicit snapshot paths | Native controls and four public scopes pass on cb3b01bc, including the corrected default ARIA path. Native plans provide snapshotDir and explicit token paths; mutable bounded suffixes pass two real callback scenarios on Runner5d63fbd2; automatic matcher naming remains separate work. |
| Terminal reporters | Native JSON/quiet options, committed-file streaming and registered public scopes pass. Custom live reporter lifecycle is still incomplete. |
| Clock and HAR | Matching final Source passes the scoped 11+11 actual browser checks. Strict legacy expiry and safe native runFor targets are partial contracts. |
| Hook transport errors | Optimized/sanitizer ownership controls and the registered native check pass. Real callback and fixture owners survive dispatch errors until actual worker retirement. |

## Next bounded source batch

| Work | Implementation owner | Separate verification | State |
| --- | --- | --- | --- |
| Native repeatEach | forms | report_merge, clock, verification, Root | Matching Runner5d63fbd2 passes 14 actual scenarios with one original coordinator/global budgets and fresh repeat lifetimes. Nine additional actual group scenarios pass, including same-repeat intra-file overlap, fixture lifetimes, serial-group retry and declared project sidecars. |
| Repeat reporting | report_merge | verification, Root | Exact repeat-qualified selection, source membership, unstarted logical coverage and typed configuration pass matching native/public checks. Exact group-subset coverage and disjoint slice-union reporting pass on the matching native executable. |
| Snapshot plan sharing | queries | audit, Root | Integrated and matching common snapshot checks pass. Prior equal-plan fixture shrank 1,573 to 875 wire bytes; corrected default ARIA plans can differ, so this is conditional. |
| Default ARIA paths | queries | Root | Integrated; matching common native controls and public snapshot checks pass. |
| Mutable snapshot suffix | queries | audit, Root | Integrated; matching public callback2 and native controls pass. Per-attempt state persists across hooks/body; arbitrary unsafe strings and automatic matchers remain partial. |
| Descriptor aggregation | clock | verification, Root | Integrated; actual actor optimized/SAN proofs and matching emergency cleanup scopes pass. A 15-FD workload uses 4 instead of 7 allocations. Separate unchanged output SAN cleanup negative retained. No timeout-fix claim. |
| Indexed repeat planning | clock | verification, Root | Private changed component passes optimized and executed sanitizer controls: 98,827 measured comparisons at 4,096 declarations. Prior 41,945,088 is source-derived. Integrated with matching native group checks; unrelated final unchanged planner SAN compile stopped at an overly low guard and left one exact compiler zombie under PID1, preserved as an unresolved cleanup negative. |

## Following dependent native batch

| Work | Implementation owner | Separate verification | State |
| --- | --- | --- | --- |
| Fail on flaky tests | report_merge, queries | audit, clock, Root | Integrated 64cc681: all 29 affected checks pass, including five genuine CLI and two merge scenarios. Native final-run policy preserves retries/dependencies/failure budgets; strict SDK boolean forwards a native flag. |
| Retrying an authored block with toPass | clock, queries | audit, report_merge, Root | Integrated at a238fec: native scheduling and real body/hook/step/file/Source/worker retirement, including original global cleanup ordering. Current eight registered and four affected public checks pass, with prior three scoped public positives retained separately. Twelve charged/clockless native compositions pass O2 and executed sanitizers. Whole-Engine Source and unsupported callback modifiers/nesting/fixtures remain explicit limits; expect.poll is separate. |
| Live custom reporter lifecycle | clock, queries, retry_attachments | report_merge, verification, Root | Active private implementation from18770a3: native ordered events and bounded isolated reporter callback execution; default behavior preserved. Public activation and actual integration evidence pending. |
| Automatic screenshot naming and update configuration | queries | verification, audit, Root | Native naming/config policy foundation is frozen privately at5126f659; 14 optimized/executed sanitizer controls pass. Public API activation is pending the complete Source pipeline; no accepted no-op update option is exposed. |
| Authenticated snapshot plan lifecycle | clock | audit, verification, Root | Inactive native DTO/wire/registry foundation is frozen privately at84c1dad8; 37 policy and eight credentialed kernel controls pass optimized/executed sanitizer checks. Source Engine/document binding and public activation remain pending; a JSON token is not authority. |
| Snapshot update policy and file publication | artifacts | verification, audit, Root | Automatic review rejected the baseline file-update subtask as a possible cybersecurity risk. Completed policy leaf and unfinished IO draft are preserved; no retry or alternate delegation. Prior four-mode observations retain their no-browser scope. |
| Poll descriptor capacity reuse | clock | verification, Root | Reviewed six-loop change is integrated; matching Runnerf05d passes27 affected checks. Optimized real-actor controls eliminate warm collection allocations; separate sanitizer outer cleanup failed on two adopted SIGKILL children of unknown lineage despite passing program assertions. |
| Directory report serialization | report_merge | Root | Integrated at1c1f769; exact old/new bytes in11 cases and same malformed-shape exception, optimized/executed sanitizer controls, source review and matching27-case gate pass. Scoped paired2-write median15.30→0.80ms; no whole-suite guarantee. |
| Broader cookie-date grammar | pending | pending | Automatic review rejected the delegated parsing task as a possible cybersecurity risk; no implementation or bypass attempted. |

## Performance and verification practices

The integrated regex scratch change uses four allocations instead of 29,696 for
the same 4,096 matches, preserving results, work charges and cancellation polls.
Its paired local median changed from 6.30 to 5.84 ms. A separate fresh process
inventory observation improved from 120.25 to 90.95 ms without cached PIDs or
inferred exits. These are scoped observations, not whole-suite speed guarantees.
A same-binary private O0 directory comparison observed eight-file execution
2.812 to 0.717 seconds and total time 6.221 to 4.212 seconds; production timing
is not established by that single comparison.

Agents use private worktrees and surgical handoffs. Root integrates reviewed
changes, builds one common batch and runs appropriate integration checks.
A full passing gate is not replayed without a relevant change or concrete
unresolved concern. Checker-only repairs run their affected scopes. Keep compact
commands, hashes, logs and real exits instead of copying whole source/runtime
epochs. Temporary measurements do not restore a benchmark framework.

One heavy-build lock and two component locks permit at most three compilers.
Use observed memory needs for compiler guards; component compiles now allow
2 GiB after legitimate 704–714 MiB jobs exceeded an overly low 650 MiB guard.
Retain those interrupted builds and their actual cleanup outcomes separately.
Use installed Node defaults rather than custom low JavaScript heap caps.
Emit the SDK before type/lint tools read its generated declarations. Keep browser
acceptance free of compiler/static-tool contention, and retain orchestration
failures separately. Reuse unchanged emitted inputs instead of repeating passing
SDK gates after native-only changes.

Original authored deadlines, cancellation, bounded queues, inode identity,
publication durability and actual waitpid retirement stay intact. A logical
completion never substitutes for physical cleanup.

## Remaining scope

The 42-family register still has 37 partial families and five unassessed optional
integrations; test counts do not establish full parity. Remaining work includes
network/HAR attribution, transparent WebSocket routing, broader clock control,
worker/debugger coverage, automatic snapshot matching, live custom reporters,
and developer/platform integrations. Browser child reuse remains disabled.
The custom CEF build and its dependent features remain explicitly deferred by
the user. Source/software and unfinished worktrees were preserved during cleanup.
