# Native test runner

## Current callback retry build a238fec

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

### Previous incremental 9373a3e

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

See the [active workboard](../../docs/internals/native-feature-workboard.md) for the
current batch, following implementation and explicit scope limits.

## Sealed combined build 0d116d39

All eight gates pass afresh: 423/423 native checks and 1,133 SDK tests in 150
files. The complete inventory of 1,554 local inputs was captured before the
checks; all 648 emitted SDK files remain unchanged afterward. Independent audit
verifies all 3,096 sealed files, 230 runtime assets, actual compiler/link metadata
and production objects. No supplemental SDK repeats are needed for this epoch.

Matching native process-registration5 passes with four real browser helpers,
three credentialed registrations, genuine process-exit receipts and empty stderr.
Browser child reuse remains disabled. Actual 40 ms assertion deadlines pass in
body/steps, beforeEach and afterEach while retaining query/group ownership until
physical completion. Hard guards pass in a separately declared fixture variant
that joins the interrupted evaluation, navigation and matcher promises. Proofs
use the exact sealed native/SDK bytes; two checker-reader corrections and that
fixture-only change are explicit. Original failed checker/fixture evidence stays
retained, and previously passing scopes were not replayed.

Full native CTest also passes listing29 and attachment mutation8, including
mutable original entry/Buffer identity, immutable insertion/file snapshots and
body entries carrying authored path metadata. Three outdated checker assumptions
were corrected narrowly after their original 420/423 failure was preserved.

Unchanged original soft6 also passes on this binary, including the previously
failing afterEach cleanup case with a genuine native mismatch/Received value.
The original 40 ms budget and all fixture/assertion bytes are unchanged; the
older f01357a7 failure remains retained. Independent audit confirms all six
scopes, strict stderr, process receipts and the full sealed inventory.

Per-fixture clock activation, HAR-inclusive context unrouteAll, output retention,
title filtering and the complete Engine work barrier are following work. Prior
original soft6/HAR favicon negatives remain preserved. The custom CEF build is
still explicitly deferred; full feature parity remains unfinished.

Evidence: `.suitecut/diagnostics/native-post-b873192a-r2-build-report.json` and
`.suitecut/diagnostics/native-0d116d39-runtime-index-v2.json`.

## Previously sealed combined build b873192a

All eight gates pass afresh: 418/418 native checks and 1,116 SDK tests in 148
files. Independent SDK typecheck/tests/lint repeats pass with all 2,240 broader
workspace inputs unchanged. The sealed inventory has 3,120 hashes plus 230
runtime assets, including actual compiler/link metadata and production objects.
Independent audits rehash the full inventory before and after matching runtime.

Exact sealed URL matcher6 and HAR route chronology6 pass with empty native
stderr. The native supervisors actually reap four adopted CEF descendants;
recorded identities are absent afterward, including zombies. Four mutable-array
callbacks also pass in two retained worker hosts with two real idle transfers
and publication ACKs. That invocation used the normal build before sealing;
its Runner and all 644 emitted SDK files match the sealed bytes. Host start
identities were not independently recorded; actual exit receipts and current PID
absence are retained. The large JUnit attachment-history and inaccessible report
artifact-reference failures are repaired with their original negatives preserved.

Literal/option fixtures, bounded array-level attachment mutation, report merging
and authenticated child-transport prerequisites are integrated. Attachment entry
object/Buffer identity changes, assertion deadlines without observed values,
listing, per-fixture clocks and active child binding are following work. Browser
child reuse remains disabled. Earlier original soft6 and HAR favicon ordering/
cardinality failures remain preserved; the custom CEF build remains deferred.

Evidence: `.suitecut/diagnostics/native-b873192a-runtime-index-v1.json` and
`.suitecut/diagnostics/native-b873192a-independent-v1/combined-independent-audit.json`.

SuiteCut has a standalone Linux C++ `suitecut-runner` and a thin Node host for
actual user callbacks. C++ owns fixture dependency order, original deadlines,
retry decisions, physical worker replacement, process cleanup and JSON/JUnit/HTML
publication. The browser executable remains independent of Node. This is the
public native runner slice with serial tests and bounded parallel projects. One unchanged upstream all-hook metadata case has
passed through a narrow declaration adapter; full runner parity remains unfinished.

## Integrated cleanup repair

Sealed `f01357a7` passes all 395 native checks, including both unchanged original
global-timeout checkers and two new actual cleanup scopes. Genuine afterEach soft
assertions use the original phase clock capped by the existing cleanup deadline;
body/beforeEach remain stopped and the original global timeout remains primary.
Actual cleanup hosts exit0; no callback receipt or physical retirement is invented.
1,048 SDK tests in 139 files and its four gates and API audit retain successful original invocation evidence
after exact SDK inputs and all 620 emitted files match. Full native compilation,
CTest and diff validation are fresh. Its manifest records 2,156 hashes+230 assets.
Real worker reuse, native soft assertion transport and fixture override layers are
integrated. Matching sealed HTTP1 passes; original soft6 is 4 pass, 1 fail, 1 unrun
because cleanup receives a genuine untagged 40 ms timeout without observed Received.
It remains hard otherFailure. Both supervised runs retire all 24 observed identities,
including zombies, with full sealed inventories unchanged. Mutable attachment arrays,
literal/options and per-fixture timeouts remain following work.
The original 391/393 failure snapshot and first soft-cleanup negative remain retained.

## Latest integrated attachment and retirement evidence

Previously sealed `4ba1c9d9` passes all 373 native checks and 986 SDK tests in
133 files. Anonymous file groups and append/read attachment behavior retain the
prior sealed scope; inactive worker reuse primitives have additional component
coverage. Real worker reuse and soft assertion integration are committed and
verified separately, with combined integrated build/browser proof still pending.
This sealed runner does not claim those following capabilities.

Previously sealed `9cffe672` passes all 361 native checks and 984 SDK tests in
133 files. Combined process checks cover anonymous-file groups, existing named
parallel groups, real emergency drains and native attachment append/read views.
The C++ declaration journal distinguishes accepted authored references from actual
file publication after writer/waitpid receipts. Callback, step, file and output
fences retain physical ownership through cleanup. Complete mutable-array behavior,
worker reuse and soft assertions remain following work. R1's original marker-read
failure is retained; corrected complete-record readers pass the focused five cases
and complete native rerun. Strict native compilation is reused only after exact
native input equality; the SDK and full CTest run afresh. Browser-only proof scopes
remain in the [browser ledger](../browser/AUDIT.md).

Previously sealed `b074f3e2` passes all346 fresh native checks. SDK964/130 and strict
compilation evidence are reused transparently after exact input/artifact equality.
Combined CTest includes declared-project test grouping/public14, genuine emergency
fault8 and causal boxed attempt attribution/public17. Actual worker/file/rollback/
descendant receipts fence ownership release; JSON/JUnit/HTML retain raw author
errors/stacks alongside genuine source-step attribution. Anonymous-file scheduling,
append/read attachment ingestion, full mutable-array parity and worker reuse are
following work, not capabilities claimed by this epoch.

Previously sealed `ff9863a9` passes all331 native tests and946 SDK tests in128 files.
Complete CTest covers native resource locks, per-attempt output ownership, explicit
global output-directory configuration and per-step boxed error attribution while
preserving original authored errors/stacks. Normal worker/file/output/descendant
retirement fences pass; a source-traced coordinator-exception drain gap is repaired
in following source work, with8 actual fault scopes proving waitpid and inode-specific
rollback receipts plus14 public checks. Combined validation is pending. Test-level work
groups and attempt-level causal boxed attribution also have following process evidence;
anonymous-file parallelism and worker reuse remain unfinished. Earlier fb2068e8 and97916950 evidence is retained
in the validation ledger, independently of this candidate.

Previous sealed e38ddd29 passes all294 native tests and845 SDK tests/114files.
Combined process scopes include native parallel-project7, authenticated local-step
pending-file skip public10/ownership7/ACK3 and existing step/fixture/file retirement
regressions. The project pool supports1..8 native workers with shared original
deadlines, failure limits and global step/attachment/attempt identities. Actual
process/file/descendant retirement fences dependency and retry admission. The
first pool uses a conservative whole-batch descendant barrier; test-level
parallelism, complete serial-group retry semantics and full runner parity remain open.

Previous sealed31a44e69 passes all280 native tests and810 SDK tests/110files.
The complete native CTest includes actual step-attachment public12/ownership5 and
project selector19, alongside previous attachment and runner scopes. Step files
have real native step+callback ownership and raw/file-writer joint receipts;
JSON/JUnit/HTML retain actual step references. Pending local-step file skip is
explicitly rejected in this slice; authenticated cancellation is a following branch.
Serial project wildcards/no-deps and primary-test/shard composition are integrated;
parallel project execution remains under implementation.

Exact sealed `d9b5f817` passes all262 native tests and788 SDK tests/106files.
Matching CEF-free checks cover TestInfo body/path attachments13, actual filesystem
and process ownership9, accepted test skip while native files are pending12/5,
control EOF6, confirmed cleanup SIGKILL3, and the pending-file EOF fence1. Actual
callback and writer/rollback retirement precede fixture/afterEach admission.
Fourteen separate legacy runner scopes retain real failure/timeout/kill contracts.
The full immutable audit is `/tmp/suitecut-runner-d9b5f817-integrated/audit.json`
(SHA256 `bc41176988f92a8dd0af5a9bf81066cb23b5d1f2b5ead789b73d171c16452365`).
Broader local-step cancellation and parallel scheduling remain following branches.
This evidence does not claim complete runner parity.

## Build and run

Build the SDK first, then either build the runner with the native browser or
configure it independently without CEF:

```sh
pnpm build
cmake -S native/runner -B .suitecut/native-runner-build -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/native-runner-build --parallel 2
ctest --test-dir .suitecut/native-runner-build --output-on-failure
```

A JavaScript module declares actual callbacks without running them at declaration:

```js
import assert from 'node:assert/strict'
import { defineNativeSuite } from 'suitecut/native'

export default defineNativeSuite({
  fixtures: {
    value: {
      setup: async ({}, use) => {
        await use(42)
      },
    },
  },
  tests: [{
    title: 'uses a real fixture',
    fixtures: ['value'],
    retries: 1,
    run: async ({ value }, info) => {
      assert.equal(value, 42)
      assert.equal(info.signal.aborted, false)
    },
  }],
})
```

Use absolute paths. From an installed package:

```sh
suitecut-test --executable /absolute/suitecut-runner \
  --suite /absolute/tests.mjs --report /absolute/results.json \
  --junit /absolute/results.xml --html /absolute/results.html
```

In the source checkout, use `node dist/native-runner-cli.js` in place of
`suitecut-test`. The typed `runNativeSuite` API launches the same native binary
once and returns its actual exit code and signal. The CLI maps real signal
termination to128+the validated OS signal number. Native SIGINT/SIGTERM stop
admission, abort active callbacks, wait for physical cleanup and publish actual
interruption reports with exit130/143. SIGKILL cannot publish a graceful report.
No browser, context, page or request fixture is implicitly installed.

The e4026569 build adds repeatable `--test-id ID` and `--shard CURRENT/TOTAL`.
The typed API accepts `testIds` and `shard: {current, total}`. Native C++ validates
exact original IDs, preserves declaration order, then partitions the selected
flat list into balanced contiguous shards (one-based, total up to65536). Excluded
tests acquire no fixtures, hooks or attempts. Empty shards run no callbacks.
Unknown/duplicate IDs reject, and retry workers must declare the exact original
module metadata. Reports contain selected tests; excluded tests are not unrun.
This partitions flat declarations, without claiming project/file grouping or
parallel scheduling. Authored9 actual native/CLI/API groups pass; the combined131 native checks pass.


The e4026569 build adds flat `hooks` arrays for `beforeAll`, `beforeEach`,
`afterEach` and `afterAll`. C++ gives each all-hook a fresh fixture scope and
independent deadline; per-test hooks share the test's actual fixture scope.
Cleanup continues after callback failures while the worker remains alive. A
beforeAll failure produces one genuine failed attempt and marks later declarations
unrun. Authored18-group real-process and sanitizer proofs pass, and the combined131
native checks passed in that historical epoch. Bounded nested suites and built-in
browser fixtures are now supported as described below.

Supported declarations are named fixtures with explicit dependencies, test or
worker scope and automatic setup, plus tests with requested fixtures, retries,
expected failure and timeout. The current per-test timeout range is0–120000ms;
0 disables the ordinary test timeout while native startup and cleanup remain
bounded. Fixtures retain actual `use()` continuations through teardown. A failed
worker must physically exit before a retry can begin in a fresh process.

Reports preserve every actual attempt and worker exit status. JSON preserves
UTF16 strings and raw worker stdout/stderr as hexadecimal bytes. JUnit records
final outcomes, prior attempts and worker-level output; XML-invalid code units
are replaced and counted explicitly. Output uses owned atomic temporary files.
HTML is a standalone document with expandable tests, attempts and workers. It
escapes visible metadata, preserves original UTF16 in embedded JSON, displays raw
worker bytes as hexadecimal, and uses no remote assets. `runNativeSuite` accepts
`htmlPath`; the CLI accepts `--html`. Output paths must be distinct. Each output
is atomically published independently; the three formats are not a transaction.

Declared tests that never ran are separate `unrun` records with reasons, without
invented attempts, PIDs or durations. JSON counts them in `didNotRun`; JUnit uses
explicit skipped records with native unrun metadata; HTML has a separate section.
These are SuiteCut report schemas, not compatibility claims for another runner.

Static `skip: true` or `skip: 'declared reason'` to
test declarations. Native C++ records selected skips before admitting callbacks,
fixtures or hooks. Empty string reasons still skip; `false` runs. JSON has a
separate `skipped` declaration array; JUnit marks `type="declared"`; HTML has an
explicit section. These records have no attempted execution, duration or PID.
They remain distinct from `unrun` tests blocked by an earlier failure. Selection
and sharding operate on original declaration order, including skipped tests.
Six actual CLI groups, independent JSON/XML/HTML decoding and full native
sanitizer proofs pass; the feature is compiled in the sealed20f979c1 epoch.

The runner reserves a Linux native child subreaper before the
host starts, then gates retry and report publication on confirmed descendant
exit. Leaked descendants get a native grace period, actual SIGKILL and waitpid;
abnormal cleanup is recorded as a global error. The contract is a single serial
native process spawner. Catastrophic runner death is a separate boundary.

Focused tests use `only: true`. Native selection applies focus, exact IDs, then
balanced shards to original declarations. `forbidOnly` / `--forbid-only` rejects
focused declarations before fixture or hook admission. Ten actual CLI/API/native
groups and sanitizer checks pass; the feature is compiled in896ed617.

`createNativeBrowserFixtures` provides worker-owned native browser access and
isolated test contexts/pages. Its optional `request` fixture creates an independent
native API request context for each test and each all-hook scope, and awaits real
disposal before scope completion. Exact896 live browser4 and request3 groups pass.

`maxFailures` / `--max-failures N` stops admitting tests after N final unexpected
test outcomes, following all configured retries. Zero is unlimited; accepted values
are0–4096. Flaky passes and expected failures do not consume the limit. The native
runner awaits actual worker retirement and reports untouched declarations as
`unrun`. Optimized and fully sanitized actual-process checks each pass11 groups;
the private runner suite passes27/27. The feature is compiled in5313fc84 and
retained in the0920242b build, whose combined native checks pass164/164.


The0920242b build adds ordered literal nested `entries`, with test declarations
or `{title, hooks, entries}` suite nodes. Use exactly one of legacy root `tests`
or root `entries`; fixtures remain root declarations. Native C++ owns depth-first
admission, inherited beforeEach, reverse afterEach, all-hook lifetime and subtree
exclusion after exhausted beforeAll failure. Bounds are256 suites,64 levels,
4096 tests and8192 hook declarations. Selected last use determines afterAll;
retries require a fresh, physically confirmed worker. Twelve actual-process and
sanitizer groups pass. This does not add a describe DSL, projects or parallel
workers.

`globalTimeout` / `--global-timeout MS` is a native monotonic whole-run deadline,
including module loading, fixtures and cleanup. Zero disables it; the maximum is
2147483647ms. Expiration stops new admission and aborts active work while keeping
actual callback and process-exit fences. It retains genuine cleanup errors and
OS signal status. Nine actual-process groups pass under ordinary and sanitizer
builds. Earlier per-test deadlines remain independent.

All-hook info now carries its actual associated attempt ID/retry, copied
`titlePath`/`suiteTitlePath`, and native suite identity. All-hooks still have
separate fixture scopes. Retry declarations validate actual suite titles,
including empty and unselected suites. Seven authored actual-process groups and
one unchanged original all-hook metadata body/inline fixture pass; this is a
narrow original test, not whole-file hook parity. Live timeout changes and
callback status/duration are included in ed097480 as described below.

The ed097480 build adds native nested-suite `skip` and `only` declarations. C++
resolves inherited skips (nearest explicit reason, including empty strings),
more specific child focus within focused parents, exact filters and balanced
shards. An empty focused suite selects no tests and still triggers forbidOnly.
Skipped/excluded subtrees admit no hooks or fixtures. Twelve real-process groups
pass in optimized and ASan/UBSan/LSan builds; unchanged focus/nested/global/phase
runner checks pass. The combined ed097480 build passes173 native checks and includes this feature. Dynamic skip and root declaration modifiers are separate.


The same ed097480 build adds native phase status/duration snapshots and
`info.setTimeout(ms)`. The timeout Promise waits for actual native acknowledgment;
`info.timeout` exposes the acknowledged value. Main work retains its original
attempt start, cleanup retains its separate start, and the global cap remains
independent. Ignored amendments still hold callback completion until their real
ACK, and genuine rejection fails that callback. Thirteen actual-process groups,
two actual fd3 ownership groups and sanitizer checks pass. One unchanged original
hooks576 case passes; its intentionally pending body still records real cleanup
errors and a worker kill. Full hook-file parity is not claimed.

The c8c823f5 build adds `info.skip()` / `skip(reason)` and conditional
`skip(condition, reason)` in the actual body or beforeEach. The synchronous host
unwind only stops the JavaScript callback; native admission and its actual ACK
establish the skip. Native C++ retains the real raw callback until settlement or
confirmed worker exit and still runs afterEach and fixture teardown. A caught
skip followed by another exception, cleanup failure/timeout, global stop or real
worker kill overrides a clean skipped outcome. Runtime skips retain an actual
attempt, PID, generation and elapsed duration; they are distinct from unrun static
declarations. Reasons preserve up to8192 exact UTF16 units in native JSON, with
explicit XML sanitization counts in JUnit/HTML. True all-hook/fixture skip and
dynamic expected-failure amendments remain separate work. The integrated native
suite passes177 checks, including the authored18 process and genuine ACK2 groups;
unchanged selected original skip3 and hooks311/576 pass their narrow adapters.
The separate CEF-free audit checks all1418 epoch inputs before and after, without
claiming a runtime-asset audit. The intentional timeout and worker kill in576
remain visible. Dynamic expected-failure support is in the following build,
whose combined checks remain pending.

The81c1e25b common build also admits runtime beforeAll skip in flat and nested
native scopes. Accepted causes retain actual scope, callback/token, associated
attempt generation/PID/worker and exact reason. Unopened descendants receive only
a cause reference, without invented attempts or duration. Propagation waits for
the associated attempt and all entered cleanup to finish cleanly; real cleanup,
timeout, global stop or worker kill overrides it while retaining the cause.
Fresh retry workers reevaluate their actual beforeAll. Four integrated beforeAll
CTest groups pass within the193-check native suite; modifiers568 annotations
remain a private following-batch proof until matching common integration.

The 5d9a7974 common build adds readonly expectedStatus from actual native admission
and ACK snapshots, plus runtime fail/skip annotations and genuine V8 call sites
when available. Awaiting fail() provides an actual native read-after-ACK boundary;
an immediate unawaited read does not predict acceptance. Three integrated metadata
CTest groups pass within the 198 native checks; unchanged modifiers568 passes
against the exact sealed runner and SDK, including actual line5/column14 source
locations and two native-propagated skip reasons. Mutable custom/static
annotations and whole runner parity remain unfinished.

## Native steps: current integration candidate

The following common build integrates `info.step(title, callback, {timeout?})`.
The method returns the actual callback value or throws its original error. Step
info carries native identity, parent identity, title and cancellation signal.
C++ validates attempt, worker, callback token and ancestry; owns monotonic timing,
original test/optional step deadlines, status and bounded report storage; and
retains physical callbacks through actual settlement or confirmed worker exit.
The thin host uses AsyncLocalStorage only to propose the author's current parent.

Native logical cancellation closes child admission while actual raw callbacks
remain owned. A native settlement/receipt handshake prevents a root callback from
retiring before a late abort or child receipt arrives. Bounded cleanup can kill a
genuinely unresponsive worker, and reports retain that outcome. JSON/JUnit/HTML
contain native step records without adding steps to test counts or overlapping
step duration to total test duration. Compact checked UTF16 failure packets
preserve isolated code units; old failure packets remain accepted.

Private evidence passes the complete60 runner tests,22 actual public process
groups under optimized and sanitizer builds, and unchanged original step427
under both. The sealed b4ec7e89 common build passes213 native checks and686 SDK
tests. Its exact runner/SDK also passes 22 public step process groups, unchanged
original step427/modifiers568 and five skip/hook regressions, with1800 audited
inputs unchanged. Original427 and hooks576 retain genuine cleanup errors and
SIGKILL. These scopes do not establish whole step-family parity.

The bd6f0632 candidate accepts true test skip while steps remain pending, after
native admission authenticates the exact request/callback cause. Native step
admission closes, logical owned steps become skipped, and physical callbacks
remain retained until real raw receipts or confirmed worker exit. A same-text
ordinary error remains a failure. A premature worker exit0 without its raw
cleanup receipt fails the attempt while preserving the real exit code. Common
CTest220 includes13 accepted-skip process groups and3 ownership checks; separate
sealed-runner reruns and original1709 are recorded independently. False skip
remains a no-op.
The sealed69d33955 candidate adds `NativeTestStepInfo.skip()` with conditional
and reason overloads. Native admission authenticates the exact active step,
callback/attempt/worker and original deadline. It skips that subtree while the
parent, siblings and test expected status continue independently. Actual raw
callbacks remain owned; genuine cleanup errors or premature exit0 still fail the
attempt. Explicit local/test causes and first-cause ownership are separate.
Matching runner checks pass20 public local-skip groups with independent JSON/XML/
HTML decoding and3 physical-ownership groups; common CTest226 and SDK729 pass.
False skip is a no-op; absent and empty reasons remain distinct.

The sealedf8778e7b candidate adds predeclared `NativeTestStep.skip()` with reason
and conditional forms. A true skip records the declared step without invoking its
body; conditional false executes normally. Matching public process proof passes10
groups and3 ownership groups with independently decoded reports. Common native
CTest237 and SDK748 pass. This does not establish complete step-family parity.

Attachments, boxing/custom locations, automatic action/
expect/fixture instrumentation, a global test.step DSL and browser-action deadline
propagation remain unfinished.

## Native component contracts

FixtureGraph validates named worker/test fixture definitions, duplicate/unknown/cyclic dependencies and invalid worker-to-test dependencies before any host callback. Plan admits automatic worker fixtures, automatic test fixtures and requested dependencies in that order. Limits256 definitions/roots and4096 edges.

FixtureOwners belongs to one actual host worker. Begin(test,generation,roots,realDeadline,now) asks host to start actual fixture factories. Ready(token,test,generation) means the factory has entered its actual use(value) continuation; it does NOT release the physical callback. Settled only retires a lease when the actual factory promise resolves/rejects. Reverse teardown resumes real continuations, preserving worker fixtures across passing tests and test isolation. Failure retires the worker before new admission. Setup/body timeout keeps callback leases and triggers bounded cleanup; if physical callbacks never settle, terminateWorker requests actual worker termination, and only WorkerClosed after OS-confirmed exit releases remaining ownership. Up to64 physical fixture callbacks; stale tokens and owner/generation replies ignored. Diagnostics cap8192 bytes. Explicit synchronous callback-to-hook reentrancy is not supported: the host bridge enqueues real asynchronous IPC events.

RetryPlan serially schedules explicit TestCase declarations. Native state owns original/retry attempt identity, expected-failure results and retry limits. Unexpected outcome creates a physical worker retirement fence; Next cannot admit a retry until WorkerExited(exactPID). Expected failure does not retry; unexpected pass does. Max4096 tests,100 retries per test,64MiB projected attempt metadata/error budget. First slice is serial and has no isolated-retry mode or serial-group/project semantics.

WorkerProcess owns real fork/exec child PIDs, bounded nonblocking socket/pipe IPC, authentic stdout/stderr and actual waitpid exit/signal status. It retains the child through SIGTERM grace and real SIGKILL/exit. Input uses MSG_NOSIGNAL without modifying process-wide signal disposition. Max64 pending outbound lines,64KiB protocol line and1MiB buffered stream/queue. POSIX/Linux implementation; other platforms remain missing. Caller must construct from a single-threaded launcher; this first slice has no CEF browser process integration.

## Evidence

fixture_graph_test: strict optimized and ASan/UBSan cover dependency validation, auto ordering, reverse teardown, worker reuse, exact token/owner/generation checks, failed teardown retirement, never-settling callback/real-close ownership and10000 fixture cycles.

retry_plan_test: native failure/expected status rules, stale owner/generation and actual worker-exit retry fence.

worker_process_test: real Node PID identity, actual stdio bytes and exit3, SIGTERM ignored then genuine SIGKILL, retry fence held through actual waitpid.

callback_process_proof + callback_host.mjs: real factories return actual objects through use(), native C++ chooses setup/teardown/body/next attempt. First native run has two distinct OS workers and three attempts: passing test, actual thrown user Error on first attempt, passing retry in a genuinely fresh worker with fresh fixture counter. Worker reuse and35ms asynchronous teardown are observed. Native JSONL records36 actual lifecycle events. Separate40ms original native timeout keeps never-settling callback until1000ms cleanup watchdog kills actual worker; seven JSONL rows show physical exit before retirement, zero remaining native leases. The host is only a callback executor, not the scheduler. The proof executable's fixed scenarios are authored tests, not a generic public CLI.

## Next implementation work

Fixture overrides/options, per-fixture clocks, parallel process scheduling,
project dependencies, locks, serial groups, steps, attachments, retention and
merged reports are integrated with scoped evidence recorded above. Continue
remaining fixture and reporter contracts rather than treating those capabilities
as new implementation work. The earlier component JSONL is an ownership journal;
the public JSON and JUnit schemas have separate validation. Preserve original
inline project sources/assertions and genuine native execution evidence.

## Reviewed corrections

Begin rejects non-finite deadlines/times. Actual Ready/Finish/Settled ingress checks the original native deadline before admitting fixture readiness or a successful body, even when Tick has not run. Token and generation exhaustion reject before wrap/reuse. WorkerProcess records an unknown exit status on non-EINTR waitpid error and never manufactures exit0; an actually externally-reaped child proves ECHILD, unknown status and noexcept destruction. Once wait status becomes unavailable it never signals the potentially reused PID. Linux parent-death SIGKILL and the getppid race guard keep real host processes from surviving a crashed native launcher. Generic public host control uses dedicated inherited socket fd3; the first authored fixed-scenario proof uses stdout as its explicitly narrow private control stream. The public runner preserves user stdout/stderr as separate raw bytes.

## Public integration evidence

The staged generic CLI has12 authored actual-process groups under optimized and
ASan/UBSan/LSan builds. A separate final reporter-header build passes a focused
fresh-process retry and independently decoded JSON/JUnit proof under both builds.
The full repository build additionally caught a TypeScript tuple-narrowing issue
under `noUncheckedIndexedAccess`; the applied one-line correction preserves the
same validated protocol values. Combined repository build and runtime results
are recorded in the browser audit after they complete. These authored checks are
not unchanged upstream runner parity.
