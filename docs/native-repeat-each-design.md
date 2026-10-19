# Native repeatEach activation design

Status: design only, based on integration `6ebe3ca`. No repeat policy, SDK
adapter, compiler or runner activation is implemented by this document. One
bounded existing stock-public observation is owned by Report for the unresolved
public facts below; it must not be presented as a production proof.

## Execution identity and authority

Keep the immutable actual declaration identity `(file, local project, local
test ID)` separate from a logical execution identity `(declaration,
repeatEachIndex)`. An actual attempt additionally has the existing native global
attempt generation, retry number, globally issued worker index and real host
PID. Repetition is not a retry: retry zero restarts for each repeat, while
attempt generations and every shared resource allocator remain monotonic.
No declaration is cloned, renumbered or synthesized in the Node module. No JS
loop invokes callbacks or reruns a report. Only the selected native admission
can start genuine callbacks against the original local declaration.

Use the existing native file/project coroutine actors as the physical boundary
for a repeat. Extend their native admission with the effective repeat count and
zero-based index. Each repeat actor owns its original local RetryPlan and
fixture/host state; retries remain inside that repeat and retain the existing
real failed-worker retirement gate. An actor can run the selected tests in that
file/project sequentially with ordinary worker fixture reuse, but a host/fixture
scope is never transferred to another repeat. Existing intra-file parallel
actors, when enabled, belong to the same repeat identity and share the original
run-global worker credits. No repeat actor can manufacture a waitpid receipt.

For suitePath with an effective count other than one, generalize the same outer
native coordinator over one actual collected file. It must own global selection,
project dependencies, worker admission, failure limits, deadline and report
publication; independent full Runner invocations followed by concatenation are
not an acceptable substitute. Preserve the existing count-one scheduling fast
path and its public behavior. Both paths expose native-selected index zero for
an ordinary single execution.

## Configuration and selection

Proposed surfaces are `repeatEach` in the thin Runner options and root/project
suite declarations, CLI `--repeat-each`, and readonly TestInfo.repeatEachIndex.
A present CLI/Runner override applies only to primary project selection. A
selected dependency/teardown project keeps its own project configuration or real
file root configuration; it is not forced to one. Project fields override that
file's root declaration, and omission means one. Keep declared and effective
scheduling counts separate: the genuine stock observation exposed declared body
count three in worker/TestInfo.project while reporter effective count was two.
The SDK serializes integers/declarations and runs callbacks only. Native code
validates raw values, resolves effective values and issues repeat admissions.
Do not silently truncate fractions, negatives, overflow or nonfinite input.
Use checked integer arithmetic and existing aggregate capacity bounds rather
than inventing a separate arbitrary maximum repeat count. Zero remains a valid declared/effective count but schedules one real iteration
with repeatEachIndex zero, as the preserved public matrix established. Keep
actual iterations `max(1, effectiveCount)` separate from configuration values.
Strict native finite nonnegative integer validation is the supported contract;
stock negative/fraction/NaN coercion quirks are explicitly unsupported. A safety-
bounded stock OOM is not semantic validation evidence.

Collect real declarations once, then apply global focus/forbid-only, config grep
eligibility, explicit IDs and primary CLI grep using their existing distinctions.
Config eligibility still applies to support projects. Native repetition expands
logical executions, not the immutable declaration catalog. Project support and
teardown remain one authoritative graph whose project completion covers every
selected file/repeat; do not independently copy a dependency graph per repeat.
A support project filtered to no executions keeps its existing Ready-gated
no-work completion and creates no source/worker/attempt receipt.

Stable logical order is native project order, repeat index, sorted actual file
order, and declaration order within a selected actor. Concurrent completion order
is not promised. Native repetition expands BEFORE sharding. Ordinary file/repeat
groups remain indivisible and are assigned by their first logical-test position
in the shard interval; fullyParallel allows individual logical tests to split.
For repeat three and A/B, ordinary shard 1/2 selects A0,B0,A1,B1 and 2/2 selects
A2,B2; fullyParallel selects A0,B0,A1 versus B1,A2,B2. Exact selected local IDs
therefore may differ between repeats. Preserve a typed SelectedRepeatSlice per
repeat; coverage keys include file, project, repeat and local ID. Never duplicate
blanket original LocalTests selection or reuse declaration-only sharding. Outer no-tests is decided once from the resolved
unsharded logical execution selection, with the explicit pass-with-no-tests
exception and an empty nonempty-selection shard distinguished as today.

Per-file configuration remains pinned to its actual transcript. An effective
count is resolved for each real file/local project, allowing the same existing
source-qualified declaration model as output/snapshot configuration. If public
semantics require a common project-level count, that consistency check must be
explicitly reviewed rather than inferred from a matching project name.

## Shared budgets and physical lifetime

Keep one original RunState, original suite start/deadline and maxFailures across
all repeats and collection. AccountFailures feeds its existing shared Failure
authority after genuine final retry/serial-group classification. A failed repeat
can stop held sibling work immediately, but neither Stop nor a final report
classification releases actual hosts, writers, callbacks, Sources or credits.

Before callback admission, checked native expansion must fit the existing
selected-entry/segment, source-report, declaration and retained metadata limits.
Specifically preserve the present 128 actual source-report input limit, 16384
segment bound, 4096 original test declarations, 128 project/file bounds and the
existing 64MiB metadata/attempt projection guards. The attachment declaration
budget remains the same shared 128-record ledger; repetition must not reset it.
Physical IO, output preparation, step, soft, publication and Source limits stay
shared, and each is released only by its existing actual receipt. Coordinator
worker reservations stay owned through the original complete task/domain/file/
output/raw/Source fence. StartWorker itself does not enforce the ceiling, and
active_workers can decrease at CaptureWorker before descendant/Source retirement;
that counter alone must never reconstruct a free repeat slot. Do not multiply
any budget by the repeat count. Compact repeat/index admissions and shared
immutable selected-ID/catalog views should avoid copying large catalogs per
repeat. Reject overflow or over-budget work before creating hosts/callbacks.

Keep the concurrent cohort global physical fence before retention/publication.
Retain every original task frame, actual file pin, actor, catalog alias and report
until real host/descendant/file/callback/Source retirement. Emergency paths drain
while these owners still exist. Existing resource locks apply across repeats;
the current conservative whole-invocation lock serialization stays effective.
Do not add a second maxFailures progress ledger merely for tests.

## Output, snapshot and public transport

Output assignment continues to use the original actual local project membership,
globally unique native output/attempt allocation and real inode receipts. Repeats
must receive distinct assigned output children even for the same title/local ID.
Committed attachment bytes/history stay immutable. Preserve-output classification
uses the exact original per-attempt owner, retry, status and expected fact through
ValidateOutputRetentionReports; a later successful retry or repeat does not
rewrite an earlier failed attempt's retention decision.

Snapshot settings remain derived from the same actual module/project catalog.
A repeat does not invent a different baseline path or append an undocumented
repeat suffix. Existing explicit snapshot templates and unavailable encodings
remain unchanged. The actual native repeat context is installed before BODY/CALL
on the same host FIFO, alongside existing output/snapshot/timeout admission. It
is retained for that actor and reset only for a genuinely fresh host/context.
The SDK exposes readonly repeatEachIndex from that accepted native context;
it must not count bodies, inspect worker indexes or infer retries.

Wire callback IDs and fixture/attachment StepOwner IDs remain the original
local declaration IDs. The genuine public oracle establishes that TestInfo.testId is distinct across
repeats and stable through retries. C++ issues the public logical ID from the
actual admitted file/project/repeat/test tuple and sends it explicitly before
BODY/CALL. Preserve separate callback/declaration identity: never substitute the
public logical string into StepOwner, fixture, attachment, test-selection or
callback ownership. Typed source-to-segment membership must include repeat
identity, and no unadmitted repeat receives a synthesized source report.

## Reporting

The current native report groups results by local testId and requires increasing
retry numbers within that group. Do not weaken that invariant to accept retry
reset. Distinct repeat actors naturally produce distinct source namespaces;
append a native repeat component to the source identity when repetition is
active. Each original source report retains real local IDs and its actual retry
history; the outer typed sidecar records the admitted repeat identity/count.
A source exists only for a real admitted actor/report. Selected unrun repeats
have native logical admission facts but no invented source, PID or attempt.

The merged summary counts logical test executions across repeats, while the
immutable collected declaration catalog still counts original declarations.
Retries increase attempts, not logical test count. A failed first repeat and a
passed second repeat are two logical outcomes, not a flaky result; fail-then-pass
inside one repeat may be flaky. Preserve declared skip, runtime skip,
interruption, unexpected pass and infrastructure errors independently. JSON,
JUnit and HTML must carry the repeat identity without rewriting old artifact,
Source, step or file ownership. Typed merger validation must check source-to-
repeat membership and exact actual terminal selected-ID coverage.

## Owned modules and central seams

Proposed Forms-owned leaves: a bounded `repeat_execution.h` policy and controls,
repeat context transport binding, focused repeated-execution process checker and
contract documentation. Extend existing MultiFilePlan/admission/catalog/report
leaves narrowly rather than forking selection/regex/fixture policy. Central
Options/parser/Metadata, Runner admission/Send and SDK public schema/host deltas
remain small reviewed patches against the then-current integration commit.
Report owns independent public observation and reporting/summary review;
Verification owns independent physical lifetime/fixture and final actual checks.
Do not overlap Report's forthcoming terminal-reporter publication seam or
Verification's fixture clock activation.

## Required focused proof before activation claim

1. Pure native identity/config/checked-expansion boundaries, exact admission
   rejection and default-one compatibility, with optimized and executed SAN.
2. Genuine two repeats: original callbacks execute twice with correct public
   index, each retry stays in its repeat and real failed hosts retire first.
3. Worker fixture setup/teardown and genuine global worker ceiling across repeats;
   independent repeat actors share all native credits and original deadline.
4. Focus, config/CLI eligibility, selected support, shard and teardown order using
   the reviewed public contract. Never claim covered IDs from declarations alone.
5. Immediate maxFailures versus a held peer, completed pass versus later stop,
   source/report exact repeat membership and actual process-tree closure.
6. Distinct real outputs, immutable attachments, independent unchanged snapshots,
   retention failure/success per repeat, and overlapping shared declaration cap.
7. Same-binary count-one regression plus a small repeated-run timing observation;
   no full-suite replay or new performance framework without a concrete need.

Implementation starts only after Root reviews the contract and resolves the
public identity/config/ordering questions. No current feature support is claimed.

## Observed public contract and remaining evidence

Report's single stock-public observation is
`/tmp/suitecut-repeat-contract-v1/report.json` (SHA256
`34e635e57cc9199c9e2ed78f502ccf07e0783aef549fae526d1b8448809ee689`).
Declared root/body count three, dependency setup count one and CLI count two
produced one setup execution plus two repeats of each of two body declarations:
five logical cases and six actual callbacks including one retry. Reporter stats
were four expected and one flaky. Setup physically completed before both body
repeats, repeat workers were disjoint, and the retry acquired a fresh worker.
Public IDs differed per repeat and stayed equal across that repeat's retry.
All six actual output paths were distinct. The stock child exited zero and all
observed processes retired; these are reference semantics, not SuiteCut support.

Report's additional public matrix at
`/tmp/suitecut-repeat-contract-matrix-v1/report.json` establishes zero executes
once, count configuration is retained, and the shard memberships specified above.
Root accepted strict nonnegative integer native support while retaining all
unsupported coercion and OOM observations separately. The initial bounded native
leaf is resolution, checked compact candidate arithmetic and post-selection
identity/budget validation; native logical shard selection is a required following
policy before central activation. It admits no physical actor or actual source.
Logical listed/selected identity can exist before execution; only actual actor
admission may transport that identity to callbacks or issue execution receipts.

Single-suite promotion must reuse its genuine initial metadata/transcript and
collection worker stdout/stderr/waitpid receipts when a configured repeat count
is discovered before callbacks. Do not import/collect twice merely to enter the
outer coordinator, drop the initial worker evidence or restart RunState/deadline.
Count one retains its existing initial host and fullyParallel path. Only a bounded
admitted cohort may instantiate Runner/emergency-owner slots; compact expansion
must not construct every repeated actor ahead of worker admission.

The first pure native leaf `repeat_execution.h` exposes an immutable compact
NativeRepeatCatalog plus NativeRepeatSelection of exact post-shard slices. File
handles are the original positive SuiteFileToken IDs; project handles are
explicit global project index plus one, distinct from zero-based report indices.
Original `(file, local ID)` declarations are charged/validated across all units,
not independently per project. Counts retain declared/effective configuration
and actual executions separately. The selected plan commits shared planning
usage only after every validation/allocation; it has no physical admission or
retirement API. Compact catalog container capacity must be charged once by its
existing native metadata owner, while source/actor slots are only planning
reservations until the original actual coordinator admits them.

Report owns `directory_report.h`/controls and merger projections. The agreed
optional `repeat_selection` pointer is null for the legacy path; optional segment
`repeat_slice` indexes its exact authoritative slice. `selection.project_tests`
is the unique original global declaration union across selected slices, while
the logical selected count comes from repeat slices. Graph metadata may retain
selected no-work support with no fake actor/source. Report membership validates
exact `(file, project, repeat, local ID)` coverage. These fields and APIs do not
activate repetition until the central coordinator/host transport is reviewed.

## Activation scope and compatibility boundary

The first activation uses bounded file/project/repeat actors in the existing
single global directory cohort. Each admitted actor executes its selected file
serially. Fully-parallel declarations affect exact expanded logical shard
membership, but this slice does not provide intra-file parallel repeated
execution or its fixture-group lifetime parity. Flattening genuine native group
actors into the same global pool is the following task; nested worker pools are
not used. Count-one single-suite execution keeps its existing fully-parallel
scheduler and initial host/import.

A single-suite initial import opens a provisional native file pin before the
host. When genuine metadata requires repetition, native code validates and
retains that pin, closes the actual collection host, and transfers its exact
protocol, timing and worker retirement rows to the global coordinator. A direct
count-one decision closes the provisional descriptor before callbacks without
adding immutable-version restrictions to the legacy author path. No second
collection or independent RunState/deadline is introduced during promotion.

Repeated callback transport requires a native actor marker followed by the
selected test's immutable logical-ID mapping. Both BODY and CALL preflight that
mapping before entering any author callback error boundary. Missing or foreign
native mappings are Host protocol failures, including for expected-failure
cases; they cannot be reported as authored expected failures. The versioned
legacy callback ABI supplies index zero and the original local public ID when no
repeated actor was admitted. Callback/fixture/step owners retain local IDs.

Explicit zero/one configuration may add the typed optional configuration row;
absent configuration leaves the legacy report field absent. Configured zero is
retained as zero and schedules one execution. Snapshot baselines are shared
across repeats, while genuine native output/attempt owners remain distinct.
Native syntax, SDK emit/type/lint and transport unit validation are prerequisites;
they alone make no production runtime or repeat-parity claim.

## Following group activation (private source, pending validation)

The following implementation derives an immutable NativeRepeatGroupPartition
from the actual collected file's existing NativeTestGroupTree and effective local
project mode. It partitions each selected slice once, preserving native default
and serial groups and parallel singletons. The original MultiFilePlan flattens
those group records into its existing global admission pool. Each group runs the
existing serial/retry/hook machinery; no nested worker pool or RunState is added.
The null-repeat count-one scheduler remains unchanged. Cohort reservations still
stay owned through task results and the original global physical fence before
retention/publication. Existing resource-lock inputs retain conservative whole-
cohort serialization.

Actual source IDs additionally qualify the admitted native group. Logical public
IDs and original callback/fixture/step IDs do not change. Typed report group
partitions must validate exact per-source subsets and whole-slice union coverage;
missing groups are selection-only unrun facts without invented sources or PIDs.
Group/source planning reservations replace the old per-slice reservation under
the same original limits and do not release physical work.

A native PROJECT_REPEAT_CONTEXT sidecar follows the existing named project
context and transports its frozen declared count, including zero/default one.
The Host checks the exact active project and pre-author boundary; it does not
resolve inheritance or CLI precedence. A conflicting later repeat-actor marker
is a protocol error. This preserves declared-three/effective-two behavior while
making named count-one project.repeatEach available without a new roundtrip,
import, report field or owner ID.

This following source has not been compiled or run. Its eventual acceptance
requires genuine same-file group overlap, fixture lifetimes, serial-group retries,
shared global limits, typed report coverage and real physical retirement. Current
activation's already-preserved proof results do not establish this new scope.
