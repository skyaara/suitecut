# Native browser validation status

## Current unsealed callback retry build a238fec

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

## Previous unsealed native build 64cc681

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

Raw native results: `/tmp/suitecut-current-full-native-ctest492-v1/`.
Combined scoped status: `/tmp/suitecut-current-native-integration-composed492-v1.json`.
Final HAR/clock provenance review: `/tmp/suitecut-final-common-har-clock-independent-review-v1.json`.
These receipts retain actual failures, separate intentional-death qualification,
compiled dependency maps and runtime scope limits. No full epoch copy was made.

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

The native source uses sandboxed offscreen CEF capture, private stdio commands,
CPU BGRA/I420 frames and 48 kHz stereo page PCM. C++ owns bounded command framing,
active-operation identities, deadlines, cancellation, CSS locator action retries,
native click/hover input, fill orchestration, navigation waits, and bounded
object-handle ownership/disposal/navigation invalidation, property handles, DOM-node
classification, bounded command/reply backpressure, frame/target routing, DOM contexts, navigation milestones, response bodies, and raw mouse/keyboard sequencing. C++ now owns clip recording, frame resampling, audio scheduling, FFmpeg process
lifetimes, atomic output commit and continuous network publishing. Native
broadcast is a thin control wrapper; narration and final rendering still include
TypeScript orchestration. The full automation/media contract remains incomplete.

## Available checks

- Native C++ command framing/operation registry, frame-rate parsing, audio clock
  and libyuv pixel-conversion tests.
- Native browser/process, protocol, recorder, recording session and broadcast unit tests.
- Decoded frame-ID and audio checks through real FFmpeg, including 120 FPS output.
- Real CEF browser, recording and RTMP verifiers under `scripts/verify-native-*.mjs`.

The current target is Linux x64 with CEF
`156.0.7+g47dbc1d+chromium-156.0.8078.17` beta. The pinned archive's size,
upstream SHA-1 and SHA-256 have been verified. The binary builds and runs with
the sandbox enabled and explicit headless software rendering.

## Current execution evidence

Generated artifacts are under ignored `.suitecut/`; reproduce them with the
commands below. These are scoped checks, not full parity or leak-freedom claims.

| Check                                        | Result                                                                           | Evidence                                                                                                                       |
| -------------------------------------------- | -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| Native CTest                                 | 243 passed in the fcbaec21 epoch                                                                        | `.suitecut/native-build`                                                                                                       |
| Native browser verifier                      | Passed                                                                           | `.suitecut/native-verification/report.json`                                                                                    |
| Pixels, scale, isolation, crash, close       | Passed, including 3840×2160                                                      | Native browser report                                                                                                          |
| Native control waits, deadline, cancellation | Passed                                                                           | `.suitecut/native-control-verification/report.json`                                                                            |
| Evaluation-error retention                   | 10,000 errors; renderer heap returned to within 5,568 bytes of baseline after GC | Native control report                                                                                                          |
| Discarded SDK wrappers                       | 100,000 wrappers; no SDK heap growth after GC                                    | Native control report                                                                                                          |
| Deterministic sanitizer fuzzing              | 10,000,000 framing/registry cases passed; ASan/UBSan/leak detection enabled      | `.suitecut/native-fuzz/ten-million-report.json`, `report.json`, and `output.log`                                               |
| Live 120 FPS video                           | Decoded `120/1`; 243 output frames over ~2.017 seconds                           | `.suitecut/native-capture-120fps/report.json` and `capture.webm`                                                               |
| Distinct live capture                        | 241 distinct callbacks over ~2.020 seconds, ~118.8 FPS at 320×180                | Live capture report                                                                                                            |
| Distinct decoded video                       | 233 distinct frames; 10 held output frames; no native writer drops               | Live capture report                                                                                                            |
| Recording/audio/presentation verifier        | Latest repeat passed; an earlier run failed the page-tone-during-narration check | `.suitecut/native-recording-verification/report.json`; original failure in `.suitecut/native-verification/recording-check.log` |
| Selected upstream core run                 | 595 passed, 1 failed, 0 skipped out of 596; 11 files                             | `.suitecut/upstream-native/runs/2026-10-14T17-30-22.345Z-585036/results.json`                                                  |
| Upstream fill file                           | 46 passed, 0 failed                                                              | Current upstream results                                                                                                       |
| Whole-browser sanitizers                     | Normal CEF-only initialization passes; instrumented control aborts               | `.suitecut/native-verification/asan-startup.json`, `clang-asan-startup.json`, and `cef-only-asan.json`                         |

The fuzz target covers CEF-independent command framing and operation registry
state, including capacity, stale generations, deadlines, fragmentation, boundary
lengths, NUL bytes, truncated input, and rejected delivery. It does not instrument
the prebuilt CEF runtime. Live retention checks cover selected SDK and renderer
allocation paths. They do not prove that every native or Chromium allocation is
leak-free.

Completed operations share one short-lived deadline timer; they no longer retain
one delayed source reference per command. Evaluation object groups are released.
Page wrappers use a source lifecycle signal, recording sessions detach observers,
and process decoders release partial payload storage after failure/closure.

## Preserved pre-repair development evidence

The following `f01357a7` browser passes all 22 unchanged script-inventory,
paused-frame evaluation and same-process/OOPIF debugger groups, with actual helper
exit0, empty native stderr and all 2,000 development inputs plus 230 assets
unchanged. This repairs the earlier debugger-close crash. Independent auditing
still finds six unreaped CEF descendants under PID1 (two per standalone scope);
behavioral success does not establish descendant shutdown. The twelve descendants
from the previous failed `4ba1c9d9` scopes remain separately recorded. Exact audit:
`.suitecut/diagnostics/artifacts-f01357a7-development-independent-audit-v1/index.json`
(SHA256 `da097bc7d1ff15e953cbfcfadf023fddfbd486556b863639a6bf3fa79e77b6d7`).

The combined development build passes 1,048 SDK tests in 139 files and native391/393. Two unchanged
global-timeout checks expose a real afterEach cleanup admission error. Their exact
failed runner/SDK and output are preserved in
`.suitecut/diagnostics/native-post-4ba-r2-timeout-failures-v1/manifest.json`
(SHA256 `c48bb5f9c32516c2ab569fb335187078c4771bb5609848afb4cb35f6aba79e7f`).
The minimal C++ repair subsequently passes the complete fresh 395 native checks,
including both original global-timeout checkers and two new real cleanup scopes.
The later successful seal is recorded below. The earlier source snapshot used for
these 22 browser checks remains explicitly a failed-build development snapshot;
its six unreaped descendants are not erased by the later build success.

## Latest integrated build and scoped oracle evidence

Current sealed Linux candidate is `f01357a7a67b818e56f0432b6ee6fe8012298301fc5cf6f78c923fa814f2b2ac`.
All395 native checks pass in a fresh complete run, including unchanged phase-info12,
live-timeout13, fixture-layers11 and the new original-global-stop cleanup2.
Strict production frontend, native configure/build and diff validation are fresh.
SDK build/typecheck/lint,1,048 tests in 139 files and public API auditing are inherited
from successful R2 invocations only after identical SDK, test, JavaScript checker and configuration
inputs and all 620 emitted artifacts are verified. Native-only C++/Python changes
are separately recompiled and executed. Exact original SDK evidence is bundled.
The immutable inventory records 2,156 files plus 230 runtime assets (manifest SHA256
`4603d4266bf6c77df6bfe3b4895b49945e4546e8179c953c6ef3a80188da5bb3`).
Build report: `.suitecut/diagnostics/native-f01357a7-build-report.json` (SHA256
`3334f0d6c030159fdedb9c99645e997d5a85d1cd37e725ad032d507a0317ab81`).

This candidate integrates the native cleanup repair, real worker reuse/soft
assertion plumbing, fixture override layers, custom and genuine native HTTP
assertions, plus HAR MIME and ZIP projection. Matching sealed HTTP1 passes.
The unchanged soft6 checker records 4 pass, 1 fail and 1 unrun: afterEach receives an
untagged 40 ms native timeout without a real Received observation. It remains a hard
otherFailure; no budget/assertion changes or fabricated mismatch are applied.
Actual supervisor waitpid and independent PID/start checks show all 24 observed
identities gone, including 6 fixture browser helpers and zombies. Full 2,156 + 230
inventories remain unchanged. Runtime index:
`.suitecut/diagnostics/native-f01357a7-runtime-index-v1.json` (SHA256
`760e7e449f68e4f079b92f0ac46262299bfe5deaa478bb7ac94458e395eb156b`).
The exact same browser binary/SDK already passes 22 debugger
groups in the preserved development snapshot above, qualified by six unreaped
CEF descendants. No new sealed clean-descendant claim is inferred from that run.
Route URL callbacks, full mutable attachments, literal/options fixtures, retained
child registration and public worker debugger support remain following work.

Previously sealed Linux candidate is `4ba1c9d9e70a3e0d7ef51bcc7ce6b8eba563300a6bc35e50eaa87dda6f2171a8`.
The complete check set passes: **373/373 native checks and 986/986 SDK tests in 133 files**.
Its immutable manifest contains 2,075 hashes and 230 pinned runtime assets; the
independent audit verifies every entry. R3 runs lint, diff validation, native
configure/build and full CTest afresh. SDK build/typecheck/tests and the public
export audit are inherited from successful R2 invocations after exact complete
source and emitted artifact comparisons. Strict native frontend and code generation
are inherited from successful R2 compilation after all 734 native input hashes
match. The actual original evidence is bundled in the sealed epoch.

This candidate activates native primary-channel HAR byte aggregation, cookie
attributes and receipt-time expiry, root document adoption, frame debugger bindings,
and publication of a genuine previously observed assertion mismatch at its original
deadline. Exact matching cookie6 passes all six cases; gzip proves 175,000 decoded
bytes, 581 encoded bytes/bodySize and 174,419 compression bytes against actual wire
data. HAR10 remains 6 pass / 4 fail at genuine favicon indices. Debugger inventory
passes 5/6, evaluation 5/8 and frame controls 1/8; frame cleanup has an actual
SIGSEGV. All inputs remain unchanged. No complete debugger success is claimed;
12 defunct adopted descendants remain from those failed standalone scopes.

Separately pinned development pairs using this exact browser pass all six original
soft assertion groups and one real HTTP response assertion group. Their runner and
SDK are following implementations, not this sealed runner. Runtime evidence is
indexed in `.suitecut/diagnostics/native-4ba1c9d9-runtime-index-v1.json` (SHA256
`1153cc30bdede1e73140a8c8be1360daf82c64d4c97938b1d994bcac6839174a`).
For that older epoch, the reentrant cleanup repair, worker reuse, soft assertions,
fixture layers and HAR MIME/ZIP were following work; they are integrated in f013 above.

R1 preserves the original included-source harness compile failure and strict JSDoc
warning. The repaired harness retains its original 11 controls and adds two exact
cleanup/retention cases. R2 preserves lint's OS-killed exit 137 during heavy native
code generation. R3 runs lint only after that actual compilation closes; no source
change or assertion weakening is used to obtain a passing result.

Build report: `.suitecut/diagnostics/native-4ba1c9d9-build-report.json` (SHA256
`cc51c06e027811aff6841c13781ee0701ad27be5e38479c26d7690fe8e528af3`).
Manifest SHA256: `b3e67c68c4163d2b0854d8ebbc51d253b5450a1123a67f2c475d24312c27e9d9`.
Independent inventory SHA256:
`d91d5341593e44362b1051be68ebe412eae295595e951b31da0868bef5a44852`.
Full feature parity and the user-deferred custom CEF build remain unfinished.

## Previous 9cffe672 build and scoped evidence

Previously sealed Linux candidate is `9cffe672a149165df228f6596a858103d8759c99ee076d1694904504992f314a`.
All eight checks pass: **361/361 native checks and 984/984 SDK tests in 133 files**.
The immutable manifest records 2,046 hashes plus 230 pinned runtime assets; an
independent audit verified every entry. R2 executes all five SDK gates and the
complete native configure/build/CTest afresh. Strict native compilation is
explicitly reused from R1 after every native C++/CMake input matches exactly.

Combined CTest verifies anonymous-file parallel groups, the existing named-project
paths, native attachment append/read declarations and real output/file/worker
retirement. The append slice accepts authored references separately from actual
published files; it does not claim complete mutable-array behavior. Worker reuse,
soft assertions, cookie attributes/expiry and debugger document/frame adoption are
following changes and are not capabilities of this sealed candidate.

The original R1 result of 360/361 is preserved. One older ownership checker read a
five-field marker after O_CREAT but before the actual newline-terminated write.
Six affected Python readers now wait for complete records, retaining every native
outcome, deadline, inode and process assertion. The focused five-case checker and
complete 361-case rerun pass. Evidence is retained at
`/tmp/suitecut-post-b074-r1-attachment-marker-failure-v1/manifest.json`.

Exact sealed changing-screenshot checks pass both scopes with genuine actual/diff
PNGs, 15 capture samples, original timeout, empty stderr and physical closure.
Unchanged original HAR10 is 6 pass / 4 fail with empty native stderr; all four
remaining assertions encounter genuine favicon entries at the expected index.
The later genuine binary POST also retains a MIME/text projection difference.
No archive entries are filtered or reordered. A declared raw-packet diagnostic
observes gzip decoded body 175,000 bytes and encoded body 581 bytes, matching the
actual server payload. Its first parser failure is preserved; the positive is a
separately hashed offline analysis, not a repeated browser run. Native encoded
compression aggregation remains following work. The unchanged popup response-finished
case fails at its original 30-second deadline with diagnostics disabled and empty
native stderr. Its timing-positive diagnostic run remains separately scoped; it
does not establish a terminal-producer repair.

Build report: `.suitecut/diagnostics/native-9cffe672-build-report.json` (SHA256
`e81d10014d94eada0902b49c591d86fa1a20e45fa5b82e345df9e007ed1ca614`).
Manifest SHA256: `ee130510bc0a34336a8fa06e7b51853efdd41e61e1ba7c30adf0bd5f0558de97`.
Matching runtime proofs are indexed in `.suitecut/diagnostics/native-9cffe672-runtime-index-v1.json`
(SHA256 `196eca2fd073b70837c52b3a93bc91bac75fe8296429e375396a771fd4df169c`).
They include 28 runner cases, two changing-screenshot scopes, HAR10, the preserved
gzip parser failure and separate offline analysis, and both popup runs. The
user-deferred custom CEF build and complete feature parity remain unfinished.

## Previous b074f3e2 build and scoped evidence

Previously sealed Linux candidate is `b074f3e2de5b57a152761c02b8d27d4280ded6adc7100718702981d75a39e9ac`.
Complete fresh native CTest passes **346/346**. SDK evidence is **964/964 tests in
130 files**; the five SDK gates and strict native compilation are explicitly
inherited from R2 after complete input and emitted-artifact hash comparisons.
R3 configure/build and the complete native test run execute afresh. The immutable
manifest records2002 hashes plus 230 pinned runtime assets; an independent full
audit found no mismatches. Native runner SHA256 is
`41f9cadd8fb3fc6dbb48a11f7428112fa4bd83be169230c45819ff0069c8726f`.

Matching browser scopes pass trusted drag5, Page/Context route removal9, debugger
script inventory6 and paused evaluation8 with empty native stderr, full inventories
unchanged and actual helper closure. The separate declared held-Drop checker passes3:
logical cancellation preserves the actual record/callback until genuine late Drop
and disable ACKs settle all16 fields0/false; it observes one trusted drop/dragend and
a healthy later click. This does not prove vendor original-widget delivery. Original
keyboard microtask119 and Alt-modifier72 also pass unchanged; no keyboard fix was
needed. Original drag21 is19pass/2existing skips, preserving its66-byte Fontconfig
strict-stderr failure.

Combined CTest includes declared-project test-level parallel groups/public14,
coordinator emergency fault8 and causal boxed attempt error/public17 checks. Native
C++ retains original budgets, actual worker/file/descendant receipts and raw author
errors while reporting causal step locations. Anonymous file groups, attachment
append/read ingestion and frame debugger adoption are following work. Startup cache
and frame ownership primitives remain inactive until their exact native adapters
are integrated; they do not claim bootstrap activation or child/worker debugging.

Current unchanged request387/popup478 is 0 pass / 2 fail. A later declared
method-entry observer proves request387 selected a genuine favicon request; that
run does not demonstrate missing headers for the intended author request. A
declared SDK936 development combination still fails original478: the real popup
and initial document request/response arrive, but neither agent emits a genuine
Network completion event. The finished wait remains pending until actual cleanup.
No page lifecycle event substitutes for that missing terminal. A separate protocol-logging diagnostic observes both primary/private
agents receiving complete ExtraInfo for the same actual author request; it proves
capability, not a missing-primary repair. Current unchanged HAR10 is5pass/5fail,
with real favicon/order, postData params and compression assertions preserved;
relative redirect now passes. These scopes have empty native stderr and unchanged
inventories. Full feature parity remains unfinished and the custom CEF build is
still deferred by the user.

R1's missing exception flags on two drag control targets and R2's345/346 result
remain preserved. The latter checker observed an O_CREAT marker before its actual
write completed. R3 waits for its complete newline-terminated five-field record;
all original process/inode/rollback/outcome assertions remain. The corrected scope
passes6 independently and in the full346 rerun. A separate mistaken focused
invocation used a nonexistent system Node path; the actual configured runtime was
then used, with that invocation evidence retained.

Build report: `.suitecut/diagnostics/native-b074f3e2-build-report.json` (SHA256
`e9e16e91674b5a09b0b8fee7194a986cf50719de4ee908bc2176159ac5ebf069`).
Runtime index v2: `.suitecut/diagnostics/native-b074f3e2-runtime-index-v2.json` (SHA256
`dd42998abdbd0ca69524d076a610e841f1d852e21e7a1f01213212e0676c792d`).
The original v1 index and all negative evidence remain unchanged.

## Previous ff9863a9 build and scoped evidence

Previously sealed Linux candidate is `ff9863a94ce29f82ab8b8d216afc227322496c158cd021e03bf45a923b4ed2c3`.
All eight gates pass: **331/331 native checks and 946/946 SDK tests in 128 files**.
The immutable manifest records 1951 hashes plus 230 pinned runtime assets; an
independent complete audit found no mismatches. The separate native runner SHA256 is
`aa45458e223e514008583d1113218a52c758cd00635414ce16562ec369c9d134`.

Matching recursive JSON9 and YAML7 pass, including actual cancellation receipt
retirement; the earlier recursive session-mismatch failure remains preserved. The
unchanged accessibility regression passes79/79, checkbox17/select-option33 pass50/50,
and chooser lifetime3, context route removal4 and paused-frame evaluation8 all pass.
Three unchanged chooser timeout cases also pass with the declared fixture mapping
to the genuine native TimeoutError constructor. Original constructor-mismatch
evidence remains preserved; genuine empty-file reset remains vendor-pending.
These exact sealed scopes have empty native stderr, unchanged complete inventories
and confirmed physical helper closure. Evaluation includes real primitive/UTF16
results, exceptions, object rejection/cleanup, local mutation and owner abort; it
does not add child/worker debugging or remote-object handles.

Two sealed checker failures remain separately recorded. The description checker
incorrectly expected Chromium raw AX to expose an equal content/title description;
the owner-signal cancellation checker incorrectly expected an AbortError wrapper.
Reviewed checker-only variants pass5/5 each on unchanged engine/SDK bytes, preserving
all public matcher/count assertions and asserting the actual raw-AX omission and
exact supplied abort reason. These are declared one-checker variants, not the
unchanged sealed checker results.

The first compile attempt's Engine error-sender typo and the second run's 330/331
native result remain immutable. The latter's25ms abort test expired during genuine
output preparation before installing its listener. The corrected checker arms the
listener in the real admitted body, then awaits native setTimeout(25), preserving
the original attempt time origin and all original abort/exit assertions. The third
run passes all331. Its strict native frontend/codegen evidence is explicitly reused
from the byte-identical second build after667 native input and browser/runner hash
comparisons; SDK, configure/build and complete CTest gates ran afresh.

Build report: `.suitecut/diagnostics/native-ff9863a9-build-report.json` (SHA256
`1b0fc4dd932f8661d68ffa7287718cf0d5dc54bb3d7f3e65bf9874249f9a9f46`).
Runtime index: `.suitecut/diagnostics/native-ff9863a9-runtime-index-v2.json` (SHA256
`9ce9978397565c2b1a820065181e616dc471e03c28d12c77e3f404e6af1007fc`); v1 is preserved unchanged.

Following source work adds native test-level work groups, script inventory,
stronger drag-command lifetime handling and boxed attempt attribution. Source review
found an emergency coordinator-exception gap: ordinary child RAII reaps direct
workers, but bypassing the drain can skip artifact rollback and adopted-descendant
retirement. The following repair passes eight injected failures with actual waitpid
and inode-specific unlinkat receipts plus14 public checks. Its combined build is
pending; this evidence does not change the sealed ff9863a9 result.
Anonymous-file parallelism, worker reuse, opaque-child factory restoration and full
feature parity remain unfinished. Custom CEF vendor compilation remains deferred.

### Previous fb2068e8 build and scoped evidence

Previously sealed Linux candidate is `fb2068e8f41b5467d74aed200431951e5ceb75462fed110b58c5a013005c6134`.
All eight fresh gates pass: **320/320 native checks and 903/903 SDK tests in 121
files**. The immutable manifest records 1903 hashes plus 230 pinned runtime assets.
The native runner is independently pinned to
`6e350c1f906b067953657aa31a992003e4eb2e353080de1abf3f17cb83ea7eeb`.
Combined CTest includes actual named-lock scheduling and unique per-attempt output
ownership, with real worker, file, output and descendant retirement fences.

Matching trusted drag checks pass 5/5, context routing 6/6 and additional live
service-worker cases 2/2, and actual root breakpoints 8/8. Each scope reports empty
native stderr, actual helper exit and unchanged complete inventories. These repair
the earlier duplicate drag-over/cancellation, page-less worker routing and missing
breakpoint behavior in their recorded scopes.

Accessibility JSON passes the official 12/12 and unchanged default 8/8 checks.
The full unchanged suite passes 9/11; remaining failures expect Playwright reference
spellings rather than the engine's genuine opaque IDs. Recursive iframe content now
passes. The dedicated recursive-frame checker passes its first eight groups but
fails cancellation cleanup: logical batches reach zero while one physical receipt
remains, followed by a native protocol-session mismatch. That failure is preserved;
physical receipt retirement and complete recursive cancellation parity are not claimed.
Evidence: `/tmp/suitecut-query-fb2068e8-aria32/index.json`.

The unchanged original drag suite passes all 19 runnable tests, preserving its two
original skips and collecting all 21. It emits a genuine 66-byte Fontconfig warning,
so strict stderr cleanliness fails. Matching authored drag5 remains separately clean.
Runtime index: `.suitecut/diagnostics/native-fb2068e8-runtime-index-v1.json`
(SHA256 `99ba4b0a3fbaf7d6185d88943feaefe7d7131fe26dbefaf4f86a6008e7dc253e`).

The first build attempt preserved nine checker failures: eight shared-observer packet
framing defects and one dependency-resolution defect. Two checker-only repairs left
native inputs byte-identical. The second attempt passed all eight fresh gates;
strict native frontend/codegen evidence is explicitly reused from the identical first
attempt, and the configure/build and complete CTest were run again. No failed run
was overwritten. Build report: `.suitecut/diagnostics/native-fb2068e8-build-report.json`
(SHA256 `b5d018bdd9edc3652abfc50561d21ddc79161038351a78b867c3a3db40871e56`).

Following source integrations add YAML frame aggregation, chooser lifetime repair,
accessible-description precedence, explicit global output directories, public context
route removal waiting, primitive paused-frame evaluation and native work-group policy.
Their matching combined build and browser verification remain separate. Empty-file
reset and directory-upload vendor patches remain uncompiled under the user's deferred
custom CEF build instruction. Full feature parity remains unfinished.

### Previous 97916950 build and scoped evidence

Previously sealed Linux candidate is `97916950c775fee46c3a6f361b6b130bf3ac62395d50b85e3d527bc626351f14`.
All eight fresh gates pass: **307/307 native checks and 877/877 SDK tests in 117
files**. The immutable manifest retains 1853 hashes plus 230 pinned runtime assets.
Twelve reviewed merge batches cover 84 changed source files, adding native serial-group
retry waves and reports, real step locations, shared snapshot publication/ACK transport,
context route globs, structured accessibility JSON and debugger owner-abort cleanup.
The full CTest includes actual serial-group public tests and step-location public tests.

Matching ordinary shared-snapshot publication passes all eight groups, with genuine
install/release ACKs, strict empty native stderr, actual helper closure and unchanged
1853+230 inventories. First-author execution still uses the existing synchronous
fallback; opaque-child restoration and new-renderer cache consumption are unverified.
Evidence: `/tmp/suitecut-init-snapshot-97916950-proof-v1/report.json`.

Matching accessibility checks preserve a real failure: ordinary buttons incorrectly
include `pressed: false`. The unchanged default JSON scope passes 6/8; the full original
scope passes 7/11. The other two full-scope failures concern opaque reference spelling
and missing recursive iframe children. All scopes collected their required test counts,
used unchanged inputs, emitted empty native stderr and physically closed. No full JSON
or recursive AI parity is claimed. Evidence: `/tmp/suitecut-query-97916950-aria23/index.json`.
Matching root debugger passes all 12 groups, including idle and paused owner-abort
cleanup before public close. Context routing passes 5/6 groups: existing/future Pages,
global invocation limits, Page priority/fallback and removal pass; a page-less
service-worker fetch reaches the actual wire without invoking its context handler.
All recorded scopes use unchanged sealed inputs and strict empty native stderr.
Runtime index: `.suitecut/diagnostics/native-97916950-runtime-index-v1.json`
(SHA256 `71b95a5a96ec43a3093a1c31561d364d51fe8a3c58bf47b282163f937c1674e9`).

Build report: `.suitecut/diagnostics/native-97916950-build-report.json` (SHA256
`1544cbbad4659021649947a49db9547f1fcdc4ec38206ac1e6934bc6285690a2`).
The interrupted first attempt and the second attempt's two zero-byte object/link
failures remain preserved. Only those reproducible object files were quarantined;
the third attempt rebuilt them and passed all checks with unchanged source inputs.
The custom CEF build remains deferred by user instruction. Full feature parity,
redirect ExtraInfo production, broader route matchers and runner scheduling remain open.

## Previous e38ddd29 build and scoped evidence

Current sealed Linux candidate is `e38ddd293867e4f195d1806ea6185931f6e71121be45897c87a50a8dbdbe99cd`.
All eight fresh gates pass: **294/294 native checks and845/845 SDK tests in114
files**. The manifest retains1804 hashes plus 230 pinned runtime assets. Ten
reviewed merge batches cover70 changed source files, including actual broker-close
waits, explicit BFCache launch policy, bounding-box selection/publication guards,
canonical network query/event ordering, authenticated local-step attachment
cancellation, native parallel projects, locator predicates and root debugger controls.
Shared snapshot receiver components are included; production IPC and factory
restoration remain following work.

Matching exact sealed locator predicates12 and bounding boxes12 pass. Unchanged
original locator-wait-for-function10 also passes10/10 with no skips. Native stderr
is empty, full1804+230 pre/post audits match, and all tracked final ownership
counters are zero. The held asynchronous predicate proof observes logical1/physical1,
then logical0/physical1 after cancellation, and physical0 only after the genuine
release ACK. Evidence: `/tmp/suitecut-query-e38ddd29-feature24`,
`/tmp/suitecut-query-e38ddd29-original10` and the explicit physical-closure receipt.

The full CTest run includes actual parallel-project7 and local-step pending-file
skip public10/ownership7/ACK3 scopes alongside existing runner regressions.
The native pool supports1..8 project workers and shares original deadlines,
failure limits and global identities. It conservatively retires descendant batches
before retry/dependency admission; test-level parallelism and serial-group replay
remain unfinished. Matching exact sealed root debugger10 also passes: genuine author frames/source,
stepOver/Into/Out, coverage start/stop while paused, original deadlines and retained
physical pause intent across caller cancellation. Native stderr is empty, actual
helper closure is confirmed and all1804+230 hashes remain unchanged.
A following SDK fix is required for page-owner abort after a session has already
opened; this candidate does not claim that lifecycle case is complete.

Build report: `.suitecut/diagnostics/native-e38ddd29-build-report.json` (SHA256
`2f28183fd7cc02d8139a13a7d3c87c25166e7a0a77bcce033f8eb45b6b74a009`).
Runtime index: `.suitecut/diagnostics/native-e38ddd29-runtime-index-v1.json`
(SHA256 `e7b9dcaa22be26794e6ea47591b3f848106f9c7852fa47fb40047fae006fc5b8`).
It retains the exact original replacement4/5 negative and separately labels the
corrected independent-observer5 positive, cache-policy3 and disabled-worker-lifetime6.
Exact e38 dedicated-worker coverage9 also passes with strict empty stderr and
complete1804+230 immutable audits.
Same-process child opaque dispatch, the missing redirect ExtraInfo producer,
custom CEF capabilities and full feature parity remain unfinished.

### Previous31a44e69 build and scoped evidence

Current sealed Linux candidate is `31a44e69cf2a630eaf546a21c16b07980386eb8fc0ae1de2ce45f704fae01cd5`.
Its manifest records **1761 hashes plus 230 pinned runtime assets**. All eight fresh
gates pass, including280 native tests and810 SDK tests in110 files. The four
reviewed merge deltas cover55 changed source files: the loading-document CSS
scope guard, shared native locator/retained-handle main-viewport bounding boxes,
marked-hop raw request header association, native StepInfo.attach with real
writer/rollback fences and JSON/JUnit/HTML provenance, and wildcard/no-deps/project
primary-test/shard composition. Native shared snapshot transport components and
a pinned CSS-retention vendor patch are also recorded; they do not restore a
runtime capability by themselves and the vendor patch is not compiled.

Matching exact sealed highlighting12 and bounding-box12 pass with strict empty
native stderr and zero tracked final ownership counters. Highlighting's previous
loading-document null-node failure is repaired. Actual dedicated-worker coverage9
also passes, including the unchanged120ms-startup contract (observed5.878ms), with
real process closure and full1761+230 audits. The full14-scope query set also
passes110/110, and unchanged original bounding-box9 passes9/9 with no skips or
flaky outcomes. Native stderr is empty, tracked final ownership counters are zero,
and exact sealed hashes remain unchanged in these positive scopes.

The new exact sealed request-header wire6 check passes1/6. A separate raw-receipt
diagnostic establishes that pinned CEF emits no post-redirect ExtraInfo for the
failing hops in either observer; FIFO association cannot recover absent wire
headers. Producer/interception behavior remains under investigation.

A separate static bounding-box first-selection group-transfer repair is committed
for the next batch; these functional positives do not claim that pending repair
is already compiled. Same-process child opaque dispatch remains under repair
(the prior c6 unchanged dispatch17 has16pass/1fail). A genuine native private
factory bridge is being tested without a page-visible global. Default pinned
BFCache can preserve the actual old author worker across navigation; the same
unchanged c6 lifetime6 passes6/6 under an explicit diagnostic cache-disabled
launch policy. No default-policy behavior is silently changed. The corresponding
public launch option is a following branch.

Build report: `.suitecut/diagnostics/native-31a44e69-build-report.json` (SHA256
`5d32f197fbc1348660b764a460b941bcd2515e0f86c8e08f134ad92f519cf262`).
Runtime index: `.suitecut/diagnostics/native-31a44e69-runtime-index-v1.json`
(SHA256 `b5224c1ff397350ff8a98d922f71a2a1ba6da282389f3680bbabe9a65d0fdd4e`),
with independent current1761+230 hash verification and separate positive/negative scope links.
The custom CEF build and full feature parity remain unfinished.

### Previous c6fbefa8 build and scoped evidence

Previous sealed Linux candidate is `c6fbefa811d009bb598cd2a46f7121378629cc88bc4e9942eafac392f0177ff7`.
Its manifest records **1731 hashes plus 230 pinned runtime assets**. All eight fresh
gates pass: SDK build/types/lint and798 tests in107 files, strict complete Source
frontend, native build, diff check and270 native tests. No SDK gate inheritance
is used for this batch. The six merged deltas cover47 source files: native
locator highlighting and shared DOM/CSS leases, same-process sibling bootstrap
containment, lazy visibility filtering, context worker cursor retention through
physical broker replacement, serial named project dependencies/teardown, and a
pinned unchanged bounding-box oracle catalog. All49 runtime exports and153 prior
type exports remain; four highlight/project types are added.

Matching actual sibling bootstrap4 and unchanged ownerFrame10 pass with clean
native stderr and full1731+230 input audits. Child utility dispatch is restored;
opaque native factory capture in same-process children remains explicitly
unsupported pending the separate transport/startup-barrier implementation.
The ownerFrame driver initially looked in the wrong output directory; its real
first-run10-check report and actual close/stderr receipts were recovered without
rerunning behavior, and the driver routing failure is retained.

The exact sealed highlight checker passes its first9 actual groups, then fails
before its iframe highlight call: a loading document without documentElement
causes a genuine null-node selector error. That negative is preserved at
`/tmp/suitecut-query-c6fbefa8-proof/highlight/.suitecut/native-locator-highlight/failure.json`.
The independent query110 run passes13 scopes/96 checks; reference scope completes10
checks before the same loading-document error. Pristine10/dispatch12 pass.
All17 unchanged original dispatch tests collect:16 pass and one valid child
DataTransfer case now explicitly rejects, exposing the containment capability
regression; full child native capture remains under repair. Worker ownership,
reattachment and historical console12 pass. A separate lifetime checker passes4
of6 groups, then exposes actual worker closure lagging awaited navigation.
The following loading-document guard is committed; these negatives remain intact.
The exact sealed project CLI11 also passes with full1731+230 unchanged inputs.
A complete build pass is not a full runtime parity claim.

Build report: `.suitecut/diagnostics/native-c6fbefa8-build-report.json` (SHA256
`42ac24e640f6b9df8b8dc7f81a12db7a6fb317e6907d5d5a860e497c34b43341`).
Private Source component/sanitizer proofs on other branches do not substitute for
a compiled matching browser. The custom CEF build remains user-deferred.

### Previous d9b5f817 build and scoped evidence

Previous sealed Linux candidate is `d9b5f81746d049f7cfe0f1bee3ddbe9d7f68e5c6b566733e17b043c3a1b32559`.
Its `.suitecut/native-integrated-d9b5f817-epoch/manifest.json` records **1704 hashes
plus 230 pinned runtime assets**. All **262 native tests and 788 SDK tests in 106
files** pass. The 73-entry integration record preserves the earlier 249/251 and
254/255 native failures and the strict C++ compilation failure. The final native
frontend/build/CTest gates are fresh; four successful SDK gates are inherited
from R3 only after the full input comparison proves that the sole subsequent
change was explicit control-flow formatting in one native C++ include. The SDK
outputs remain hashed and unchanged. All 49 runtime exports and the previous 151
type exports remain; two native attachment types are added.

Matching immutable browser checks pass pristine dispatch10, ordinary dispatch12,
and all17 unchanged pinned original dispatch tests, with strict empty native
stderr and complete before/after input audits. Actual OOPIF factory creation,
DataTransfer/File identities, captured query intrinsics, at-most-once authored
errors, cancellation, and coverage coexistence are covered by these scoped checks.
Actual worker mixed ownership4, same-source reattachment4, and historical console4
also pass without checker overlays. Genuine channel detach is distinct from
actual author target destruction; terminal closure takes precedence only after
its real receipt.

The matching sealed native runner/SDK passes file/body attachments13, actual
file/process ownership9, accepted test skip with pending attachments12/ownership5,
control EOF6, confirmed cleanup kill3, and pending-file EOF fence1. Fourteen
separate legacy runner scopes also pass their own contracts. Expected real
cleanup failures, timeouts and SIGKILL remain visible. Native file publication
requires the actual writer/rollback process and receipt fences; fixture and
afterEach admission waits for the actual callback and file retirement fence.
Step attachments, projects and other broader runner features remain separate
branches, not claims of this snapshot.

Build report: `.suitecut/diagnostics/native-d9b5f817-build-report.json` (SHA256
`499597507b3f3fb69686df6e2cf75ebf513de308a951f5fb9a6986195621db09`).
Browser dispatch report: `/tmp/suitecut-pristine-d9b5f817-proof-v1/report.json`
(SHA256 `99ac1a19464e8df7db5c7cabb5f28721c2592c1fa36d794d5b3a4ab98b8bb60c`).
Worker report: `/tmp/suitecut-worker-generation-d9b5f817-proof-v1/proof-index.json`
(SHA256 `d7fddf97d3324d58623aa5d11b3b5f4f2b9e26d834fb194785c915bc2e522627`).
Runner report: `/tmp/suitecut-runner-d9b5f817-integrated/audit.json`
(SHA256 `bc41176988f92a8dd0af5a9bf81066cb23b5d1f2b5ead789b73d171c16452365`).
All17 unchanged dispatch results are recorded in
`/tmp/suitecut-original-dispatch-d9b5f817-proof-v1/report.json` (SHA256
`1d3c6838978d6d506e132c88bb2f6d6c1771c00346afdb4e01b4b3c28bfb7f66`).
The exact sealed official clock10 and observed runFor exception4 also pass.
The latter covers actual synchronous uncaught main-document exception receipts
with a real final pause acknowledgement; it does not attribute errors exclusively
to timer callbacks. Promise rejections remain separate page errors.
The aggregate runtime index is
`.suitecut/diagnostics/native-d9b5f817-runtime-index-v1.json` (SHA256
`01da594973d9a8e34edc611de974127697af95b93f89ba558368ec83cc9d71c6`),
with an independent unchanged1704+230 audit and seven separately scoped reports.
Other matching browser families remain separately scoped; diagnostic4752
positives do not substitute for sealed evidence. The sibling bootstrap containment,
native highlighting, worker cursor retention and serial project scheduling are
merged in the next frozen integration batch, whose combined checks remain pending.
Full parity is unfinished. The custom CEF build remains deferred by user choice.

### Previous fcbaec21 build and scoped evidence

Previous sealed candidate is `fcbaec21171d438ffcde822f5aa3e18a81823fa0eac7640ed17183962dbbb040`.
Its `.suitecut/native-integrated-fcbaec21-epoch/manifest.json` records1650
source/SDK/test/script/object/evidence hashes plus 230 pinned runtime assets.
All243 native tests and762 SDK tests in104 files pass, with all eight applicable
gates successful. The36-entry record adds the structural native-string dispatch
packet, shared debugger domain ownership, native clock duration/lifetime guards,
locator normalization, actual worker generation DTOs and the trace correction.
The successful SDK build/export audit is inherited only after the full inventory
proved the sole change was an authored fixture's required parentId field; the
initial failed typecheck is preserved, and all remaining seven gates were rerun.

Exact compiled dispatch12 now passes with strict empty stderr and final native
ownership zero, repairing the genuine previous retained-list destructor SIGSEGV.
References14, trace4, normalization9 and83 query regressions also pass, with all
reported counters zero, strict empty stderr and complete unchanged input audits.
The query index is `/tmp/suitecut-query-fcbaec21-proof-index.json` (SHA256
`e22aacf406581590ef6c916ed8c111feb09a78448c6f3e326d37bc8273bca0d4`).
Matching CEF-free runner scopes pass declared-step-skip10/ownership3/report12,
local-step-skip20/ownership3, accepted-test-skip13/ownership3, steps22 and selected
unchanged originals. Actual cleanup errors, timeout and SIGKILL remain retained.

Actual context-network4 fails because the separate native context broker omits
the required control identity on retained console handles; the SDK correctly
rejects the missing identity. This actual failure is preserved, with a native
correction following. Exact worker observation2, coverage9, OOPIF7, genuine crash/reload2 and
same-source reattachment4 pass with strict empty stderr. A separate dedicated
worker console-history checker fails after one group because actual detach is
published as permanent close. Clock official and observability-only checks fail
on crash-waiter closure; a separately labeled diagnostic establishes the first
nine groups completed. The augmented clock helper genuinely exits0/no signal.
Pristine main-realm factories and runner attachments remain
following work and are excluded from this epoch. No complete API228 rerun is
claimed; the last full result remains69d33955's224PASS/4FAIL.

Build report `.suitecut/diagnostics/native-fcbaec21-build-report.json` has SHA256
`f8bf1c7ab281d6e920b32fa2e0b01d3ac5a04e7d1808c1bf2669592459efec93`.
Matching dispatch evidence is `/tmp/suitecut-dispatch-fcbaec21-proof/index.json`;
matching runner evidence is `/tmp/suitecut-runner-fcbaec21-integrated/audit.json`.
Runtime index `.suitecut/diagnostics/native-fcbaec21-integrated-runtime-report.json`
(SHA256 `d6fa6f01603d8b142ba398e6f0f2dbfc4510c7d7e20db115bffc2ea21d3add4c`)
hashes12 scoped reports, retaining all three distinct failed families. Root
independently verified all1650 hashes and 230 runtime assets unchanged.

Previously sealed candidate is `f8778e7b1007d689a2625b10e383ddd4cfc4f0fbc5a47ffee319ab7f5653fe09`.
Its `.suitecut/native-integrated-f8778e7b-epoch/manifest.json` records1628
source/SDK/test/script/object/evidence hashes plus 230 pinned runtime assets.
All237 native tests and748 SDK tests in103 files pass, with all eight applicable
build gates successful and the existing49 runtime exports/150 named declarations
preserved. The38-entry integration record adds native snapshot references,
worker reattachment, predeclared step skipping and SDK recovery. Exact compiled
references14 and83 query regressions pass. Worker observation2, context-network4,
coverage9 and genuine same-source worker reattachment4 pass with strict empty
stderr, actual process-exit receipts and complete unchanged input inventories.
CEF-free matching runner scopes include declared-step-skip10/ownership3,
local-step-skip20/ownership3, accepted-test-skip13/ownership3, public steps22 and
selected unchanged originals; intentional cleanup errors and SIGKILL remain visible.

Two actual browser failures remain explicit. Trace verification completes two
groups before detecting an extra DOM snapshot. Dispatch completes zero groups;
GDB confirms SIGSEGV during destruction of the retained CEF argument list after
the first real dispatch result. The attempted argument-list copy did not fix it.
Following source contains a structural native-string packet repair and a trace
correction; those changes are excluded from this sealed epoch until a subsequent
compiled browser proof. Pristine main-realm event factories, shared debugger
ownership, locator normalization and clock additions are also following work.
The last complete API228 run is still the69d33955 result224PASS/4FAIL; it was not
rerun onf8778e7b.

Build report `.suitecut/diagnostics/native-f8778e7b-build-report.json` has SHA256
`d8cb354d93e778b4cfe0b751096c7e93df5819c42b21dacc0e34991d7de03018`.
Runtime index `.suitecut/diagnostics/native-f8778e7b-integrated-runtime-report.json`
(SHA256 `22f5aca997dd21f832ec028a9c0c7200f2856b97277db270edb7a0e58b062b09`)
hashes12 scoped reports, including both browser failures. Root independently
verified all1628 hashes and 230 runtime assets without mismatches.

Previously sealed candidate is `69d33955f6055776897f05c825c71e5ba0b76d9bc9c4f8cb9e958ef3d03bb19c`.
Its `.suitecut/native-integrated-69d33955-epoch/manifest.json` records1597
source/SDK/test/script/object/evidence hashes plus 230 pinned runtime assets.
All226 native tests and729 SDK tests in102 files pass; all eight applicable gates
pass, preserving49 runtime exports and150 named declarations. The34-entry record
adds native context-specific CA certificates, actual observed-worker lifecycle and
helper-process close receipts, runtime local-step skip, explicit empty-HAR fallback,
and the origin diagnostic checker correction. Two documentation-only lint failures
are retained; successful SDK build/type/unit evidence was reused only after the
complete inventory proved the sole correction was that helper comment.

Exact compiled CA4 and official origins7 pass with native ownership settled,
strict empty stderr and actual helper exit0. Unchanged API228 remains224PASS/4FAIL,
with the same four known failures and no new API failures. The worker wave passes
actual pre-ready observation2, context-network4 and worker-coverage9: genuine
service-worker request metadata arrived before published inventory while real script
bytes remained held; logical shutdown preserved identity until real process exit.
CEF-free runner checks pass local-step-skip20/ownership3, accepted-test-skip13/
ownership3, public steps22 and selected original outcome/skip/hook regressions.
Intentional cleanup faults and SIGKILL remain recorded.

Official dispatch verification genuinely SIGSEGVs after five completed groups.
GDB confirms real Text dispatch completed; the following relatedTarget DOM-handle
adoption crashes in CEF value-controller ownership. This failure is retained in
`/tmp/suitecut-http-69d33955-proof/dispatch/failure.json` and is under diagnosis;
passing build/component checks do not establish dispatch success. Empty-HAR
original60 and publicHAR7 pass; ContextHAR6 behavior completes but strict stderr
fails on the preserved Fontconfig diagnostic. Snapshot references,
worker reattachment recovery and pristine main-realm event factories remain
following work, excluded from this epoch.

Build report `.suitecut/diagnostics/native-69d33955-build-report.json` has SHA256
`c750110420e443fe2da14652976ce22d3576969ef1001f4beded3fa85a6b4335`.
Runtime index `.suitecut/diagnostics/native-69d33955-integrated-runtime-report.json`
(SHA256 `41e07a0a262d410b318b5becb581be233e0acd2e331ac0487631ffd99c71b0cc`)
hashes12 scoped reports, including genuine dispatch and Fontconfig failures.
Root independently verified all1597 hashes and230 assets without mismatches.

Previous sealed candidate is `bd6f06328749db29a30cb3138e19a18c145fd4f01b52b30cac1a7d0442928f29`.
Its `.suitecut/native-integrated-bd6f0632-epoch/manifest.json` records1577
source/SDK/test/script/object/evidence hashes plus 230 pinned runtime assets.
All220 native tests pass. The SDK has697 passing tests in100 files; build,
typecheck, lint and public API checks preserve49 runtime exports and150 named
declarations. The corrected native run reuses successful SDK gates only after
proving its sole source change was an authored C++ test; those SDK inputs are
unchanged. The initial219/220 result and old host-only test expectation remain
recorded separately.

The28-entry record integrates accepted native test skip with pending-step raw
receipts, including the real premature-exit0 cleanup failure; authoritative API
constructor-origin snapshots with actual restore-ACK commit; imported host-only
wire scope and genuinely completed empty encoded responses; screenshot matcher
constructor-lifetime forwarding; and bounded API-option marshaling. Associated
browser state remains genuine mutable capture. Official jar6/regex3 pass on this
exact candidate, including host-only wire exclusion and20 create/close cycles.
Complete unchanged API228 passes 224/228 with no skips or new failures; all five
targeted cases recover. Official extended screenshot/PDF9 and regression20 pass,
including Page/locator matcher constructor abort. Exact runner skip13/ownership3,
steps22 and selected original outcome assertions pass; intentional cleanup errors
and SIGKILL remain visible. Origin7 passes a separately hashed diagnostic-routing
correction, including actual16-origin cancellation with4803036 retained bytes
and all12 final ownership counters zero. The sealed original diagnostic-schema
failure remains recorded. Worker
observed-target lifecycle, snapshot references, local step.skip and extra CA
trust remain following/private work.

Build report `.suitecut/diagnostics/native-bd6f0632-build-report.json`
has SHA256 `b7c6f3cd149d54676ae1209832902b5afaf4ba12e40c64a8cdb7c367a210e79a`.
Runtime index `.suitecut/diagnostics/native-bd6f0632-integrated-runtime-report.json`
(SHA256 `4a1f4c4a173ee1ae7f5359510929420504c1a67c89e01d0e11dba85634484d04`)
hashes16 scoped reports, including failures and explicitly diagnostic-only
favicon observations. Root independently verified all1577 epoch hashes and230
runtime assets with no mismatches. Following source changes are excluded.

Vendor CEF compilation remains user-deferred and full feature parity is unfinished.

Previously sealed candidate is `b4ec7e897cdf7412e2c3b089329030959429879b0d5949b046b94225bc549594`.
Its `.suitecut/native-integrated-b4ec7e89-epoch/manifest.json` records 1562
source/SDK/test/script/object/evidence hashes plus 230 pinned runtime assets.
All eight build/check commands pass: 213 native tests and 686 SDK tests in 99
files, with the complete source inventory unchanged. Automatic public API audit
preserves all 49 runtime exports and all 145 prior named declarations, adding
five types for dispatch options and native steps.

The 62-entry integration record includes genuine Page/Locator/fixed-node event
dispatch, bounded real-node strict diagnostics, the cookie manager typed-ingress
and visibility reply repairs, authoritative independent API cookies with actual
header/auth receipts, native bounded content decoding and HTTP framing checks,
explicit bounded EOF replay under maxRetries, chunked large screenshot/PDF
configuration with actual receipt ownership and original-clock parser checks,
and native test steps with raw-callback acknowledgment fences. Associated browser
cookie/state policies remain separate from independent API state.

Matching exact-binary proofs pass query/snapshot83, unchanged strict/query27
and getter/state52, network metadata/preflight/image/collector23, and screenshot
regression20. Large screenshot/PDF configuration9, cookie-jar6, cookie-regex3
and dispatch12 pass separately hashed checker variants; the original authored
checker setup failures remain recorded. Unchanged dispatch17 is13 pass/4 fail:
atomic custom selectors and opaque DataTransfer arguments remain unsupported.
The complete unchanged request/response57 has48 assertion passes, one original
expected Chromium failure and8 unexpected failures, with0 skips. Request-event
attribution for the remaining failures is under investigation. Runner steps22,
original step427/modifiers568 and skip/hook regressions pass; intentional cleanup
errors and SIGKILL remain visible. Complete unchanged API228 is219 pass/9 fail,
0 skips. Five earlier failures recover; two regressions concern imported host-only
cookie scope and empty Brotli bodies. Their repairs remain following work.
These overlapping scopes are not a parity total. Vendor CEF compilation remains user-deferred and
full feature parity remains unfinished.

The immutable runtime index is
`.suitecut/diagnostics/native-b4ec7e89-integrated-runtime-report.json`
(SHA256 `6d25351c14d86bd89403b5586b7fd066d693bca88fd4065d24b7b50a6d733b2a`).
It hashes22 scoped reports, including preserved checker failures, corrected
checker variants and full original-suite failures. Root independently rechecked
all1562 epoch files and 230 runtime assets with no mismatches. Following normal
source changes are excluded from this binary and its claims.

Previously sealed candidate is `5d9a79741ab4e5491bfec48f1747afe6449d498004e606473af49d3fc02931f5`.
Its `.suitecut/native-integrated-5d9a7974-epoch/manifest.json` records 1510
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All 198 native
checks and 659 SDK tests in 95 files pass; all eight build/check commands succeed
and the complete source inventory remains unchanged. The 34-entry integration
record adds native regex cookie filtering, Page.fill first/strict options,
shared response projection for navigation and network events, native-delivered
expectedStatus/runtime annotations and actual V8 source locations, response
vector-capacity accounting, and grammar-validated missing-frame visibility
snapshots with XPath/escaped-colon parser corrections.

Existing public exports are preserved: 49 runtime exports and 142 existing named
declarations, with three new public types. Earlier build negatives are retained:
an obsolete unsupported-RegExp unit assertion and a stale private patch's removed
HTTP error export were corrected before this passing build. Native 198 includes
actual runner metadata process/ownership/report checks. Exact matching runtime
proofs pass query regressions56, Page.fill first/strict5, HTTP capacity3 and typed
transport errors4, HTTP/TLS/HAR metadata6 plus independent network17, screenshot8,
unchanged response363/3742, unchanged getter/state52, and selected original runner
skip/hooks/annotation cases. Each recorded scope audits all1510+230 epoch inputs;
intentional runner timeout and real cleanup SIGKILL remain visible.

The unchanged strict/query27 scope reports20 pass/7 fail/0 skip: all17 query cases
pass, while strict diagnostics remain3/10. Cookie regex still genuinely fails the
manager's older strings-only ingress; the unchanged original124 also fails.
The official snapshot checker first has a malformed-CSS expectation mistake;
a separately hashed syntax-only correction reaches9 checks then exposes the real
`{type:"object",value:false}` native reply. Both failures remain recorded.
Following source fixes share typed cookie admission and canonicalize only actual
native visibility booleans. They do not change this sealed epoch's results.

The completed scope index is
`.suitecut/diagnostics/native-5d9a7974-integrated-runtime-report.json`
(SHA256 `1e5e8b550daa9ae2a1e654ec832f1422d00279c97d542bcc9b95705c1c188e23`).
Overlapping scopes are not summed as a parity total. The custom CEF build remains
user-deferred and full feature parity remains unfinished.

A subsequent complete unchanged API228 rerun on this exact5d9a build also reports
216 pass/12 fail/0 skip in44.5 seconds, all228 collected with no collection errors,
empty native stderr and all1510+230 epoch hashes unchanged. Its12 failure identities
match the preserved81c localhost run exactly. This completed run is recorded in
`.suitecut/diagnostics/native-5d9a7974-api228-addendum.json`
(SHA256 `c06b03eb17ea139fa7cdb658b299eb2ff19076752d6c3fa550d154cbbd010c91`);
the earlier scope index remains unchanged.

The following81c record remains historical evidence, including its TLS metadata
failure. Neither fullAPI228 result is a full browser/runner parity total.

Previously sealed candidate is `81c1e25bb11d22afbd64ce717291f50df12348b122a9bff660afcc72f4344b96`.
Its `.suitecut/native-integrated-81c1e25b-epoch/manifest.json` records1481
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All193 native
checks and651 SDK tests in93 files pass, with all eight commands successful and
complete source inputs unchanged. This adds native beforeAll runtime skip,
structured actual transport-error metadata, response/HAR endpoint and certificate
projection, prompt screenshot-failure publication with cleanup ownership, improved
validation diagnostics and posted selector-wait fatal teardown.

Actual query56, screenshot20, nativeHTTPerror4, unchanged targeted API5 and
unchanged response363/3742 pass. CEF-free original runner skip3/hooks311/576 also
pass with1714 pre/post hashes (1481+230+3checkers);576 preserves intentional
failed/timedOut outcomes and real cleanup SIGKILL. The complete unchanged API228
on actual localhost reports216pass/12fail/0skip, all228 collected without errors
and native stderr empty. An earlier127.0.0.1 fixture run reports213/15; its literal
localhost assertions caused three failures and the first cookie83 assertion.
Both runs are preserved. The later cookie83 subdomain-filter assertion still
fails genuinely; no browser cookie metadata is relabeled to pass it.

The actual HTTP/TLS/HAR6 checker fails after two genuine HTTP assertions: the
legacy navigation path emits raw CDP certificate extras, which the strict public
schema rejects. Native preflight6/image6/privateCollector5 independently pass17.
The following source batch uses one fresh native response projection for both
controller events and navigation replies; this is not yet a compiled positive.
A separately hashed new catalog checker collects untouched strict10/query17 and
reports19pass/8fail/0skip on81c: parenthesized XPath detection, strict diagnostics,
Page.fill strict options and dispatchEvent remain observable gaps in that binary.

The historical 34-entry following integration batch includes the TLS projection repair,
native regex cookie filtering, Page.fill first/strict options, native-delivered
expectedStatus/runtime annotations and genuine source locations, actual response
vector-capacity accounting, and validated missing-frame visibility snapshots with
XPath/escaped-colon parser corrections. Its serial build/check cycle subsequently passed as 5d9a above;
private helper/component proofs do not establish this combined browser behavior.
Vendor CEF compilation remains user-deferred; full feature parity is unfinished.

The hashed scope index is `.suitecut/diagnostics/native-81c1e25b-integrated-runtime-report.json`
(SHA256 `04453d39bf6834f9a0df69b51b8b29c48298b4b2f6a63c59f734f98000250de1`).
Overlapping scopes are not summed as a parity total. The following d0/c8/ed
records are historical and do not replace the current results above.

Previously sealed candidate was `d0def75c7e2dc67cea047875075f93e61916b28ef35f80fb10773f52d4e4e90d`.
Its `.suitecut/native-integrated-d0def75c-epoch/manifest.json` records1460
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All187 native
checks and642 SDK tests in90 files pass; all eight build/check commands succeed
with the complete source inventory unchanged. It integrates adopted stream
multipart, bounded chunked screenshot input, exact preflight authorization,
private image-body collection, focus-owned fill/clear, editable cancellation,
Page first-match/strict getters, corrected contenteditable state, native dynamic
expected-failure amendments and static expected-failure cleanup precedence.

Untouched getter/state/retarget52 now passes52/0fail/0skip with all52 collected.
Authored getter14/strictPage10/ownerFrame10/frameMetadata8, focus5/signal3/preview3,
ReadStream5/auth4/default2/multipart5/header3 and receipt-deadline checks pass.
Untouched HTTP1197/1000 pass2; preflight6/image6/privateCollector5 and untouched
response363/374 pass2. The prior c8 empty-GIF failure is historical, not erased.
The contenteditable checker initially saw a still-pending real release ACK; all3
behavior assertions pass when the checker waits for actual zero ownership. Its
original immediate-counter negative is retained with the1.6→10.4ms ACK trace.

Actual screenshotInput7 and screenshot8 pass, including232989-byte PNG input from
a Buffer and real readonly path,130KiB CSS,29 native append commands and a33066-byte
largest wire command. Public held-filesystem4 and joint-capacity checks also pass
on this binary with a test-only exact-path pass-through syscall gate: real read
and close receipts establish timeout/cancel retention, same-byte mutation rejection,
context-close waiting, the actual shared2-worker cap and queued original timeout.
The first input checker observed the wrong transport's counter; its authored
failure remains separate from the corrected passing checker. Strict native stderr
is empty and full1460+230 pre/post inventories remain unchanged for these scopes.

The current full API228 result remains c8's209pass/19fail; focused d0 positives do
not rewrite that total. Actual d0 negatives remain for Page.fill default/strict
options and missing-lazy-frame visibility snapshots. Following-batch beforeAll
skip, response/HAR endpoint/TLS metadata, structured transport errors, validation
messages, posted selector-wait teardown and queued screenshot-cleanup publication
are being checked separately. Controlled128-lane SDK sender proofs do not establish
128 actual CEF controls: real ordinary event waits cap64 and screenshot leases16.
Vendor CEF compilation remains deferred by user choice; full parity is unfinished.

The hashed scope index is `.suitecut/diagnostics/native-d0def75c-integrated-runtime-report.json`
(SHA256 `138c84b73740862bdd8a85622e7f7382d546afb12c65927725727bdf6605a813`).
Overlapping scopes are not summed as a parity total.

Previously sealed candidate was `c8c823f57e82213fb6cc039f3b72296216b32d7f9a756f900840c7ee389e2ee9`.
Its `.suitecut/native-integrated-c8c823f5-epoch/manifest.json` records1418
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All177 native
checks and610 SDK tests in85 files pass, with all eight check commands successful
and complete build inputs unchanged. It integrates the matching authentication
receipt clock, required explicit controller clock, ACK-owned utility-world DOM
getters, genuine label/ancestor state observations, page state aliases, frame
locator page ownership, locator descriptions, native DOM handle captions and
label checked assertions. Runtime body/beforeEach skip is also native-owned,
including actual cleanup/receipt fences and exact bounded UTF16 reasons.

Actual getter14/frameMetadata8/ownerless3, auth4/default2/multipart5/header3,
receipt deadlines, workers9/ownership6/OOPIF7/nested2/recovery2, smallScreenshot8
and inputValue8 pass on this exact epoch with strict empty native stderr. The
untouched getter selection collects52 and reports51pass/1fail/0skip; the remaining
contenteditable inside a button is incorrectly retargeted for isEditable. Full
untouched API228 reports209pass/19fail/0skip. All failures remain recorded.

Original ownerFrame checker fails by reading text still present in the old child
document before navigation commits. A real held-response trace establishes this
ordering; waiting for genuine Frame.waitForURL makes the checker pass10. The
original preview checker passes2 then its setup returns Node.prototype, causing
an unrelated codec error before the next assertion. Returning void0 only from
that setup makes the original caption assertions pass3. Both original failures
and these checker-only corrections are retained separately.

Private body collector5 passes; unchanged response363 passes and374 fails with
an empty evicted GIF. A separate real-agent capability trace obtains the exact14
GIF bytes from the existing secondary session while direct primary data is empty;
it does not relabel the failing original374. Selected unchanged runner skip3 and
hooks311/576 pass their narrow adapters with before/after1418 input hashes; this
CEF-free run does not claim an asset audit or full hook-file parity.

Raw native forced fill/clear of a disabled target can edit a previously focused
outside input; this actual negative is retained. The following build integrates
the focus-ownership guard, public fill/clear cancellation, contenteditable state
correction, Page first-match/strict semantics, adopted stream multipart input,
chunked screenshot input, exact preflight route authorization, image admission,
and native dynamic expected-failure amendments. Their combined checks and live
positives remain pending. The c8 screenshot proof uses only small PNGs and retains
its inline64KiB limitation. Vendor CEF compilation remains user-deferred.

The exact hashed scope index is
`.suitecut/diagnostics/native-c8c823f5-integrated-runtime-report.json`
(SHA256 `9d38317a9fe2d45c108fa785441ece67e6ac9e807591c06f6d974a91451cc220`).
Overlapping scopes are not summed as a parity total.

The following ed097 evidence is historical. In particular, its authentication
clock failure and getter52 negatives are preserved rather than claimed as the
outcome of the new candidate.

The earlier sealed candidate was `ed09748016e03b904ccf3e93fe0ee2f26f4dcbff35cfe3f9e4b72eb2f2d35af6`.
Its `.suitecut/native-integrated-ed097480-epoch/manifest.json` records1405
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All173 native
checks and599 SDK tests in83 files pass; all eight build/check commands succeed
with unchanged source inputs. This integrates native inputValue, frame-scoped
Inspector commands and lossless frame metadata, private prefetch-body collection,
small-PNG screenshot comparison, byte-correct HTTP headers, and native runner
phase snapshots, timeout amendments and nested-suite skip/focus declarations.

Actual frameMetadata8/ownerFrame10/ownerless3, worker9/ownership6/OOPIF7/nested2/
recovery2, defaultAuth2/multipart5/headerWire3 pass on the exact binary. Untouched
original response363/374 pass2/2, including formerly empty prefetch bodies;
original header834 passes1/1. Original runner hooks311/576 pass their narrow
adapters; the latter preserves intentional timeout/cleanup errors and worker kill.
Its inventory audit is post-run1405 inputs only, not an independent pre-run audit.
Corrected checker-only inputValue8, smallScreenshot8 and privateCollector5 also
pass; their original authored checker failures remain retained separately. The
screenshots use small PNGs: inline input above64KiB is still an explicit gap.

The unchanged original getter/state/label selection collects all52 tests and
reports27pass/25fail/0skip with empty native stderr. Remaining gaps include public
page state aliases, protected getters and label/ancestor state retargeting,
previews/descriptions, frame locator page ownership and label checked assertions.
These failures guide the following batch; staged fixes are not claimed here.
Credential update/auth4 fails because the new receipt check uses a different
clock origin from Engine deadlines. The matching-clock injection and mandatory
constructor clock are applied for the following build, whose actual runtime proof
is pending. Controlled component tests alone do not establish the repaired browser.
The hashed scope index is `.suitecut/diagnostics/native-ed097-integrated-runtime-report.json`.
Overlapping scopes are not summed as a parity total. The full API228/network57
have not been rerun on this epoch, and the vendor CEF build remains user-deferred.

The following092 evidence is historical; its prefetch and frame-access gaps are
repaired above, while the auth receipt regression was introduced after092.

The earlier sealed candidate was `0920242b360f1a5f8615abfb6bf10469738176d2e45d268ba99cc04932fa490f`.
Its `.suitecut/native-integrated-0920242b-epoch/manifest.json` records1367
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All164 native
checks and583 SDK tests in80 files pass; all eight build/check commands succeed
and the complete source inventory stayed unchanged. This integrates bounded
literal nested suites, global runner timeout and all-hook metadata, native
multipart framing, credential-clear policy, ownerless-node and UTF16 frame-name
repairs, associated-label fill/clear, typing focus/caret behavior, response receipt
aliases and bounded image-body streaming.

Exact092 runtime proofs pass ownerFrame10/ownerless3, original ownerFrame7/7,
auth4/default-auth2/multipart5, label fill/clear6, type6, worker9/ownership6/OOPIF7,
nested closure2 and worker recovery2. All native helpers close cleanly with empty
stderr, final tracked ownership0 and1367+230 unchanged hashes. Original API
credentials593/603 and ordered form1123 pass3/3. Original type5 passes5/5 with an
explicit checker-only collection update and the exact sealed native/SDK. These
scopes overlap and are not summed as a full parity total.

Redirect functional2 and image-body wire5 pass; the pinned original image374
also passes. Original prefetch363 still returns an empty body. Both actual image
hold attempts completed their stream receipt before observation, so all3 authored
physical-ownership gates remain failed/unexercised, despite functional body
success. No live late-after-finished redirect ordering was observed.

Frame metadata passes its first5 transport groups, including isolated UTF16 units
at the1024-unit bound, then fails an authored navigation assertion that assumed
the browser preserves an isolated-unit window name. Actual raw Inspector/DOM
probes on092 and5313 both show the name resets and the Inspector name falls back
to the iframe ID. No SDK name is invented to restore the lost value. Remaining
navigation/OOPIF/binding groups need the separately staged corrected verifier and
frame-scoped native Inspector access. The complete hashed runtime index is
`.suitecut/diagnostics/native-092-integrated-runtime-report.json`.

The following5313 results are historical, including the credential crash and
ownerless-node failures repaired and verified on092.

The previous sealed candidate was `5313fc847b0b7b330a25df7a1f7db02dbad452b25156bdbca4155ec767b34bc4`.
Its `.suitecut/native-integrated-5313fc84-epoch/manifest.json` records1328
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. All151 native
checks and575 SDK tests in78 files pass, along with both builds, typecheck, lint
and diff checks. Complete source inputs stayed unchanged during the final build.
The first native run retained a failing controlled session-handoff fixture that
omitted required actual protocol hasExtraInfo flags; adding those flags preserved
the original header/migration assertions and made the complete rerun pass.

Strict actual worker coverage9, ownership6, OOPIF7, nested iframe/worker closure2,
worker renderer recovery2 and native blur8 all pass with empty native stderr,
actual helpers closed, scoped ownership drained and1328+230 unchanged hashes.
Original ownerFrame7/7 and separately selected original CORS129 also pass. The
broader authored ownerFrame10 fails on genuine unattached HTMLDocument adoption;
additional ownerless-node probes confirm the related detached-document boundary.
Native frame names containing isolated UTF16 units also lose their metadata;
these exact failures and their next-batch repairs remain separate from original7.

Credential updates pass their first2 actual groups, then null clear leaves a real
browser fetch waiting; the unchanged30s verification ends with SIGSEGV during
shutdown. A separate1s diagnostic and genuine nonmatching-provider comparison
show Fetch CancelAuth returns401 while empty policy enters Chromium fallback UI.
GDB records actual inferior SIGSEGV in modal-dialog widget destruction and no
Source GetAuthCredentials entry. The wrapper exits0 and is explicitly not a clean
native-exit proof. That5313 empty-policy defect is repaired and verified by the092 auth4 rerun.

Real direct/intercepted redirect headers and canonical identities pass, followed
by24 genuine three-hop chains/72 wire hops. Their authored late-after-finished
observation gate remains unexercised. Causal review corrects the earlier inference:
the original captured defect proves stale receipt reuse, not an observed exact
raw-after-finished20f ordering. Legal late receipt/migration behavior has native
component coverage; no extra live late-order pass is claimed. All old failures,
original gate and raw evidence are retained. Offscreen OOPIF full-page pixels and
the user-deferred custom CEF build remain open.

Previous896ed617 passed multipart wire/HAR/offline6 and held-body ownership3,
credential constructor3, independent request fixtures3, browser fixtures4 and OSR
window guard3. Its unchanged original request/response57 has47 expected outcomes
(including one expected Chromium failure) and10 unexpected; original API228 has
205pass/23fail/0skip with all25 native stderr logs empty. These results belong to
that exact1304+230 epoch. The worker cleanup and TimeoutError defects found there
are fixed and verified on5313; the broader suites have not been rerun wholesale.

The previous20f979c1 epoch passed all15 authored query scopes and unchanged original
selectors70/70 and evaluation100/100, with empty native stderr, final scoped
ownership0 and1291+230 unchanged hashes. Actual25000-node transfer is4680.60ms
versus8767.33ms on e078; the original120s budget and separate1ms cancellation
check are unchanged. The largeDOM original passes its existing30s budget;
eval814 author intrinsic replacement passes. Common browser fixtures4 and HTTP
observation3 also pass. These remain evidence for that exact earlier epoch.

The following evidence belongs to the previous sealed `e4026569` epoch:

All131 native checks and556 SDK tests in74 files pass, with1271 hashes+230 assets.
Combined OOPIF7, isolated UTF16/utility2, genuine two-cycle OOPIF crash/reload
recovery, Text-node pointer7, selectText8, custom-selector action isolation8 and
HTTP redirect/CN/ancient-expiry5 pass with empty native stderr and final scoped
ownership0. Original click14/14, scroll9/9 and separately collected selectText5/5
pass; the initial selectText collection omission remains retained. The authored
adapter now rejects unknown requested filenames and checks actual declarations.

It adds flat native runner hooks and native exact-ID selection/sharding,
Text-node pointer geometry/scrolling, selectText, isolated custom-selector action
readiness, HTTP UTF8 Location/redirect Host/first certificate CN handling, and
reconciliation of genuinely completed coverage cleanup before immediate restart.
The common CTest run includes18 real hook groups and9 real native/CLI/API
selection groups. No full upstream runner parity is claimed. Native HTML and
explicit unrun declaration reporting were already compiled in e0780f2c.
The sealed manifest's descriptive scope and one hooks gap sentence were copied
from an older template; immutable hashes/logs/counts are correct. The correction
is recorded without changing the epoch in
`.suitecut/diagnostics/native-e402-seal-metadata-addendum.json`.

The previous sealed `e0780f2c` has1249 hashes+230 assets,123 native checks and552
SDK tests. All12 authored query scopes pass, including prototype isolation10,
actual25000-node transfer3, retained-handle waits10 and grammar99. DNS5 (including8
simultaneous distinct destinations), HTTP lifecycle5, managedHTTP10, HTTP8,
scrolling7, raw headers3, core network13 and body/session4 pass with empty native
stderr and zero final scoped ownership. Inputs remained unchanged.

Original selector70 is69pass/1fail: nested :scope now passes, while large DOM
selection times out. Original evaluation100 is99pass/1fail after author replacement
of Object.getOwnPropertyDescriptor. Both regressions remain under repair.
Original request/response57 is45pass/12fail/0skip. Original HTTP228 is195pass/33fail/0skip with corrected localhost, dual-stack and404
fixtures plus explicit test-only mapping of the private lookup hook to the genuine
public DNS API. Engine and fixture changes make this a separate baseline from811.
OOPIF coverage passes its first5 groups, isolated UTF16/utility2 and genuine
two-cycle crash/reload recovery, but immediate restart exposes a readiness race.
That concrete fix is compiled in e402; its full7-group live proof now passes.
The initial pre-start diagnostic helper required fields before lazy controller
creation; the corrected authored input and initial failure are separately retained.

The following evidence belongs to the previous sealed `811785a9` epoch, whose
1210 hashes and 230 assets remained unchanged during its scoped proofs:

- Nine authored query scopes pass: grammar91, shadow4, frame-owner8, queries53,
  worlds9, mixed11, semantic14, wait13 and retained13. Native stderr is empty and
  reported scoped counters finish at zero. Original selector70 is69pass/1fail;
  the remaining nested :scope case motivated the new build.
- Managed HTTP10 and HTTP8 pass, including the repaired actual storage-export
  deadlock. Separate real filesystem-fault trace-disposal and held31000ms
  zero-deadline proofs pass. Original HTTP228 is170pass/58fail/0skip, with all60
  native stderr logs empty; its older fixture used127.0.0.1. New runs will record
  corrected pinned localhost/dual-stack/404 fixture contracts separately.
- Hover8 and unchanged mouse/handle-misc26 pass with empty stderr. The initial
  authored bare :hover failure was a BackCompat fixture issue; standards-mode
  evidence and the original failed fixture are both retained. Original handle
  click14 is12pass/2fail, exposing real Text-node and zero-area diagnostic gaps.
- Coverage9/ownership6/init-registry3 and active-trace4/API-trace5/rawdirs5 pass.
  Real active-recording crash/reload3 and pending-start crash6 proofs also pass;
  they do not establish OOPIF coverage on811.
- HAR metadata4/capacity3/held-ownership3/public7 pass with empty stderr.
  Context HAR6 passes functionally but FAILS the strict diagnostic check because
  of an actual Fontconfig warning. That failure remains open.
- Original request/response57 is43pass/14fail/0skip with the corrected server
  fixture. An earlier ENOSPC-interrupted attempt has no aggregate result and is
  retained separately. An actual favicon race and a CORS page-close failure
  remain distinguishable from observed-header correctness.

These scopes overlap and are not a grand parity total. Full feature parity is
unfinished; the user-deferred custom CEF build remains pending.

The following67 evidence is historical and is not a rerun on either later epoch.

Historical sealed candidate is `67c1077e1d12f0da8cc33bd4825b4f1c1743f891bd18c16809eaa5571ff88928`.
Its `.suitecut/native-integrated-67c1077e-epoch/manifest.json` records1199
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. Native112/112,
SDK527/527 in68 files, both builds, typecheck, lint and diff checks pass. Actual
command exit statuses and the complete source inventory were checked; no source
mutated during the build. Dependencies remain a workspace node_modules link.

This candidate adds selector grammar, independent managed HTTP profiles,
context-wide HAR replay, bounded native publisher reconnect, native runner
interruption, active browser/API raw trace flushing and actual decompression
error diagnostics. Full feature parity remains unfinished.

Exact combined selector grammar76, query53/world9/mixed11/semantic14/wait13/
retained13 pass with stderr0 and final scoped ownership0. Native reconnect9,
Source11 and public wrapper proofs pass with observed stderr0, actual encoder
exit/replacement and independently decoded output. Browser active trace4,
APItrace5 and raw directories5 pass with independent archive/resource decoding.
Active trace checks observe timed console/network publication without a following
browser command. The first authored verifier matched a before-row parameter;
its failed run is preserved alongside the corrected event-type check. All these
families audit1199+230 sealed hashes unchanged.

Public HAR7 and actual held-install ownership3 pass without native diagnostics.
Context HAR6 functional groups pass, but the overall strict proof FAILS because
of the real sandbox Fontconfig warning; this is not a clean pass. The genuine
managed HTTP profile proof FAILS at associated request.storageState: reinserting
an owned cookie list into its own CEF dictionary deadlocks. Its timed-out command
and forced shutdown remain preserved. A detached-publication repair is staged
for the next shared build, with a real CEF component regression. The separate
HTTP8 proof passes with stderr0, helper exit0 and actual scope counters0; its
initial wrong-diagnostic-channel verifier failure is also preserved.

Unchanged selector70 improves to65pass/5fail/0skip; tracing43 remains37pass/3fail/
3originalskips; coverage17 remains16pass/1fail/0skip. Runs:
`.suitecut/upstream-native/runs/2026-10-15T03-06-53.137Z-1215864/summary.json`,
`2026-10-15T03-09-27.790Z-1219674/summary.json` and
`2026-10-15T03-10-44.767Z-1222364/summary.json`. HTTP228 has not yet rerun on67:
the genuine associated export deadlock is repaired before repeating that scope.
Unchanged pointer90 completes89pass/0fail/1originalskip, with all1199+230 sealed hashes unchanged. Evidence: `.suitecut/native-pointer-67c1077e-proof-inputs/report.json`. No earlier epoch result is presented as a67 rerun.

Current proof inputs live under `.suitecut/diagnostics/query-67c1077e-proof/`,
`.suitecut/native-stream-reconnect-67c1077e-proof-inputs/`,
`.suitecut/native-trace-active-67c1077e-corrected/`,
`.suitecut/native-context-har-67c1077e/`,
`.suitecut/native-context-har-ownership-67c1077e/`,
`.suitecut/native-public-har-67c1077e/` and
`.suitecut/diagnostics/context-http-67c1077e-proof/`.

The following ca7 evidence is historical and is not a rerun on67c1077e.

Historical sealed candidate is `ca7ebe32d0042c0ed117b7c1e8ac0e6a38e1174cecd5fecc47ea9591f0e85a43`.
Its `.suitecut/native-integrated-ca7ebe32-epoch/manifest.json` records1174
source/SDK/test/script/object/evidence hashes plus 230 CEF assets. Native102/102,
SDK522/522 in68 files, both builds, typecheck, lint and diff checks pass. The build
record captures actual command exit statuses and an unchanged complete source
inventory. The integration record is copied into the immutable epoch, together
with build logs and the standalone public runner executable. Dependencies remain
a workspace node_modules link with a recorded lockfile.

This build integrates expanded native HTTP TLS/proxy/authentication/metadata,
public HAR recording and page replay, native current-document JS/CSS coverage,
trace POST resources/MIME/action-label fixes, retained-root selectors and grouped
ACK-owned disposal, the speculative-empty-renderer bootstrap guard, and the first
public Linux serial native runner with JSON/JUnit output. The public runner12
actual-process groups pass as part of the102 native CTests. Reporter component
files are independently decoded and include actual atomic-write fault checks;
a separate strict ASan/UBSan/LSan component proof passes. This does not establish
unchanged upstream runner parity.

Combined selector proofs pass on these exact sealed inputs: wait13, retained
selectors/grouped cleanup13, query53, worlds9, mixed11, semantic14, raw AX9 and
an independent actual-DOM CSS prefix9. Final reported ownership counters are0,
captured stderr is0 and all 1174+230 hashes remain unchanged. The actual101-group
cleanup remains owned after the public abort grace (1002ms), then drains only on
real browser ACKs. Evidence: `.suitecut/diagnostics/query-ca7ebe32-proof/integrated-report.json`.

All five official recovery/crash verifiers and the unchanged broader first-author
settings script pass on ca7 with stderr0. The latter observes initial state plus
two genuine crashes, correct configured behavior, root42 and actual close. This
repairs the native speculative-loader diagnostic failures preserved in d9; it
does not address the separate first-author popup locale/vendor gap. All1174+230
hashes remain unchanged. Evidence: `.suitecut/native-recovery-ca7ebe32-proof-inputs/report.json`.
Focused public runner export and declared package CLI proofs also pass2/2, using
the actual sealed native executable, real user fixture objects, JSON/XML output
and exited worker PIDs: `.suitecut/native-runner-ca7ebe32-public-export/report.json`.

Combined coverage9, ownership6 and init-registry3 pass with stderr0, actual clean
shutdown and zero coverage physical/recording/source counters. Proofs use declared
hashed authored scripts with the exact sealed SDK; all1174+230 hashes remain
unchanged. Reports are under `.suitecut/native-coverage-ca7ebe32/{current,ownership,registry}/`.
Independent API tracing5 and raw exports5 also pass with stderr0 and independent
ZIP/raw decoding, including exact POST resource bytes. Report directories are
`.suitecut/native-http-trace-ca7ebe32/` and `.suitecut/native-trace-rawdirs-ca7ebe32/`.
The first decoder report stdout-redirection collision is preserved separately;
all decoder assertions passed, and the corrected JSON reports are retained.
Raw trace console handles can remain intentionally retained by the bounded console
journal; this is not a claim that every global diagnostic counter is0.

Public HAR7 passes with no diagnostics on ca7, including exact binary wire bytes,
atomic native context-close commit, duplicate cookies, attached ZIP/update,
offline replay and miss fallback/abort. Unchanged HAR10 remains4pass/6fail/0skip:
`.suitecut/upstream-native/runs/2026-10-15T02-31-52.547Z-1176119/summary.json`.
Remaining failures cover pages/pageref, relative redirect URL, compression and
favicon-indexed POST assertions; actual POST bodies remain present in later
entries and must not be described as missing. No upstream assertion was weakened.

Combined native HTTP feature8, permission feature8 and twenty first-author
context lifecycles plus cross-origin storage clear/regrant pass on ca7 with empty
stderr and helper exit0. Reports are under
`.suitecut/diagnostics/context-http-ca7ebe32-proof/`. The unchanged original
selector70 improves to37pass/33fail/0skip, with inputs/binary unchanged:
`.suitecut/upstream-native/runs/2026-10-15T02-32-54.032Z-1177313/summary.json`.
Full unchanged tracing43 improves to37pass/3fail/3originalskips on ca7,
with actual native HTTP fixtures and unchanged inputs:
`.suitecut/upstream-native/runs/2026-10-15T02-35-28.578Z-1185791/summary.json`.
Cases272 (MIME resource),816 (native POST file reference) and901 (ordinary action
labels while explicit wait-event titles remain) now pass. Remaining443/471/837
involve private stack/default-trace/artifact-path contracts and active flush.
Do not fabricate external-driver brands or IDs to turn these green.
Full unchanged coverage17 is16pass/1fail/0skip with unchanged inputs:
`.suitecut/upstream-native/runs/2026-10-15T02-38-03.600Z-1189333/summary.json`.
The sole failure is CSS112, the explicit resetOnNavigation:false rejection.
Full unchanged HTTP228 confirms151pass/77fail/0skip on ca7 with empty native
stderr and unchanged executable/fixture inputs:
`.suitecut/upstream-native/runs/2026-10-15T02-40-01.568Z-1191560/summary.json`.
The expanded HTTP result matches its earlier private scope. Independent managed
profile/cookie/storage and improved decompression diagnostics are next working
source, not evidence that the77 current failures have passed. Individual prior
private proofs remain separately scoped. The HTTP/permissions aggregate additionally verifies all1174
sealed hashes and 230 runtime assets after the runs; it records the immutable seal
as its initial baseline rather than inventing a separate pre-run scan. Three integrated SDK attempts failed before native
compilation: runner tuple narrowing, coverage import/this-alias lint, and an
unrelated getter touched by that lint correction. All failed inputs/logs remain
preserved; the final full build and all checks pass after the recorded fixes.

Coverage explicitly rejects resetOnNavigation:false and OOPIF scope. Context-wide
HAR/future targets, independent managed HTTP profiles, stream reconnect and runner
signal handling remain staged for a later build. Raw trace active flushing,
remaining original-suite failures, vendor CEF capabilities and full feature parity
remain unfinished. The custom CEF build stays deferred by user choice.

### Previous combined d9 evidence

Previous sealed candidate is `d9a25e6cff08268c1f245fe8ade3a3b48a60dc416621b2c57b1270dcae152149`.
Its `.suitecut/native-integrated-d9a25e6c-epoch/manifest.json` records1095 source,
SDK, test, script and object hashes plus 230 static CEF assets. Native92/92 and
full SDK522/522 in68 files pass, with native/SDK builds, typecheck, lint and diff
checks. The source snapshot includes native runner components; dependencies are
a workspace node_modules link with a recorded lockfile, not an independent
installed dependency snapshot.

This build adds native child crash/reload recovery, atomic selector waits with
ACK-owned cleanup, raw trace exports, automatic-preflight public-event filtering,
actual storage-access origin pairs with fail-closed scope accounting, and initial
native runner components. Live independent API tracing5, raw trace export5,
automatic-preflight4 and iframe session migration4 pass on the same sealed files.
API/raw proofs independently decode archives and recheck all1095 hashes unchanged;
all these runs have stderr0. Raw exports occur at stop/chunk boundaries;
continuous active flush and early automatic-preflight routing remain open.

The broader first-author recovery settings probe preserves locale, timezone,
UA/language, touch/mobile viewport, screen/DPR, media preferences, actual init
script, geolocation, headers and authenticated HTTP across two real renderer
crashes. It also preserves root42 and clean physical close, but fails strict
stderr: each recovered renderer emits native Blink/network URL-loader NOTREACHED
errors. The initial no-crash control passes without diagnostics. These errors
remain visible in `.suitecut/diagnostics/native-recovery-settings-d9a25e6c/`
and the subsequent phase/control evidence. Do not count this as a clean pass.
The same loader errors reproduce across eight actual controls, including default
contexts, and occur during reload. Pinned Chromium source attributes them to a
subresource request in a speculative frame with no bound default URL-loader
factory; the ca7 native speculative-empty-frame guard repairs this path, with strict-stderr combined evidence above.
Combined recovery3 behavior scopes pass, but diagnostic instrumentation captures
the same errors, so they are not clean stderr passes. Public crash-only and
throwing-listener cleanup controls are clean. Permissions8, first-author lifecycle20
and cross-origin storage-access clear/regrant pass on d9 with stderr0 and all1095
hashes unchanged. Original selector70 reports35pass/35fail/0skip, improving the
previous24/46 result. Exact summary is
`.suitecut/upstream-native/runs/2026-10-15T01-24-51.489Z-1110519/summary.json`.
Complete unchanged tracing43 on d9 is34pass/6fail/3originalskips:
`.suitecut/upstream-native/runs/2026-10-15T01-28-26.605Z-1124170/summary.json`.
Every global API request now uses NativeAPIRequestContext. The previous35035
fixture generator did not override the global request fixture, so its global
request trace cases753/779/816 used the oracle HTTP backend and are not native
HTTP evidence. Retain that older35/5/3 result only with this qualification;
it does not establish a native regression. Current native failures include the
missing POST-body file reference, resource extension/action-title format, and
private stack/brand/runner-artifact contracts. Permission original-suite rerun
is in progress. Public HAR, native coverage, broader HTTP options, runner declarations/
CLI/reporters and publisher reconnect remain under implementation. Full parity
is unfinished; the custom CEF build remains deferred by user choice.

### Previous combinedf0 evidence

Previous sealed candidate is `f0ce508d08ba8ebf3ad1eb791069dfb97f1db1086d2d8abd6a047cf8240e48e4`.
Its `.suitecut/native-integrated-f0ce508d-epoch/manifest.json` records993 source,
SDK, script and object hashes plus 230 static CEF assets. Native83/83 and full
SDK520/520 in67 files pass, with native/SDK builds, typecheck, lint and diff
checks. This build corrects actual registered CEF permission settings and
clipboard grants, native selector metadata adoption from the author realm,
and selector-aware Page/Frame `$eval`/`$$eval`.

Actual native permissions8 and first-author permission lifecycle20 pass.
Selector metadata5, selector-aware eval6, queries52, declared worlds9,
mixed engines11 and semantic14 pass with all993 hashes and230 assets unchanged.
Complete unchanged geolocation/permissions32 reports26pass/2fail/4originalskips;
all nine geolocation cases and both clipboard cases pass. The remaining failures
are opaque-document storage permission and additional actual native request
events. Original selectors70 reports24pass/46fail; native Page.waitForSelector
and scoped ElementHandle selector methods are the next concrete gaps.

The expanded unchanged HTTP API scope runs228 tests through NativeAPIRequestContext
and reports99pass/129fail/0skip. Browser-context fetch is53/120, global fetch46/77,
and cookie tests0/31. Cookie tests are currently blocked by the unsupported
ignoreHTTPSErrors option, so this does not establish31 cookie algorithm failures.
Reports retain exact identities and original assertions:
`.suitecut/upstream-native/runs/2026-10-15T01-09-12.748Z-1070314/summary.json`.

The next working-source batch includes raw trace directories, genuine automatic
preflight filtering in public events, storage-access origin pairs, native child
renderer recovery, and native runner fixture/worker components. These have private
proofs but no combined native build yet. Standalone integrated runner components
pass five real-process CTests; public declaration/CLI and full runner parity remain
unfinished. Recovery preserves the Page identity across real crash/reload, while
old handles and media remain failed. Initial root crashes remain helper-fatal.
Public HAR recording/replay, coverage, expanded HTTP options and publisher
reconnection remain under implementation. The deferred custom CEF build remains
pending by user choice.

### Previous combined35035 evidence

Previous sealed candidate is `35035f7a616a0709dcc8791f1dce289be7ef9a352934b4205e2ca4a2379cdd18`.
Its `.suitecut/native-integrated-35035f7a-epoch/manifest.json` records984 source,
SDK, script and object hashes plus 230 static CEF assets. Native82/82 and full
SDK520/520 in67 files pass, along with native/SDK builds, typecheck, lint and diff
checks. The first native compile failed in the new HTTP trace test because CEF
appended its no-exceptions default after the target's exception override; placing
the override after CEF target setup fixes the build. Both logs are retained.
This build adds actual public child-page crash events, cleanup before surfaced
listener exceptions, canonical iframe request identity/body migration across
renderer swaps, ARIA searchbox readonly support, independently owned API-request
traces and current-document native virtual-time budgets. Live checks on the same sealed build pass native virtual-time9, independent
API-request trace5, iframe request migration4, worker network4 plus held-ACK3,
public crash waits and throwing-listener/active-publisher physical cleanup.
Each retains its own report and scope. Full unchanged tracing43 is35pass/5fail
with three original skips; API212 passes, while raw directories and private
runner fixtures remain open. Exact summary is
`.suitecut/upstream-native/runs/2026-10-15T00-46-33.646Z-1030447/summary.json`. Current native time budgets do not provide full browser clock parity:
RAF/idle pause, rebasing, running fixed Date, resume/fast-forward, navigation and
context/OOPIF/worker propagation remain unsupported.

### Previous combined46 evidence

Previous sealed candidate is `46d65c61e0d95904b98d13f5e1086740cbc7c032f9762a00259d6b3f8a45aa01`.
Its `.suitecut/native-integrated-46d65c61-epoch/manifest.json` records960 source,
SDK, script and object hashes plus 230 static CEF assets. Native79/79 and full
SDK518/518 in67 files pass. Final SDK build/typecheck/lint pass after adding the
trivial `Page.isHidden` delegate; the full unit run preceded only that delegate.
This build includes the sealed72 features below plus genuine composed visibility,
fixed-element visible/hidden/stable/enabled/disabled/editable waits, immediate
fixed-element boolean states, root-session frame navigation with an actual ACK,
empty uncommitted-frame URL handling, bounded WebSocket trace records, genuine
HAR failure/timing fields and filesystem errno recovery. The complete unchanged state/visibility files pass23/23 on this sealed build.
Actual element-state8, fixed-handle4, query52, semantic14 and URL-wait13 scopes
also pass; query scopes preserve all960 source/object/SDK and230 asset hashes.
Full unchanged tracing43 improves to34pass/6fail/3originalskips; HAR411, errno424
and WebSocket980 now pass. The exact report is
`.suitecut/upstream-native/runs/2026-10-15T00-37-44.277Z-1006420/summary.json`. Child public crash events, ARIA searchbox readonly support, network request
session migration, independent API-request tracing and current-document native
virtual-time budgets are applied for the next build only. Its first compile
failed because the HTTP trace test placed its exception override before CEF
target defaults; the target ordering is corrected and the new build is pending.

The strict aborted-navigation trace check retains a real Fontconfig warning.
A pass-through libc observer proves sandboxed `access(/etc/fonts/fonts.conf)`
returns `EPERM` immediately before the warning; the browser process reads the
same existing file normally. CEF embeds static Fontconfig, so early initialization
of the system Fontconfig library did not fix it. Actual original warning, failed
control, native stack and I/O results are preserved in
`.suitecut/diagnostics/fontconfig-init/causal-evidence.json`. No sandbox relaxation,
warning filtering or production workaround was added.

Broader unchanged geolocation/permissions32 baseline on46 is15pass/13fail with
four original skips. Geolocation6/9 fails only three validation-message contracts;
permissions expose genuine deny/onchange/isolation/capability gaps. Both clipboard
cases terminate the native helper with SIGTRAP during grant configuration, before
actual clipboard I/O. The candidate passes an unregistered Chromium content-setting
type through CEF; a repair is being independently verified. The initial local-fonts
failure came from the authored fixture omitting its requested TLS-ignore option;
forwarding that option produces a genuine original pass. Initial/focused scopes
are retained separately, without replacing or summing their results.

### Previous combined72 evidence

Previous sealed candidate is `72e20fda160cec8f3b81e813c27541a3b60d04d847dfcb263404d768d51098c3`.
Its immutable `.suitecut/native-integrated-72e20fda-epoch/manifest.json` records
952 source, SDK, script and object hashes plus 230 static CEF assets. Native
checks pass 78/78; SDK checks pass 509/509 in 65 files. Native/SDK builds,
typecheck, full lint after the SDK build and diff checks pass. An earlier lint
attempt overlapped the SDK's dist cleanup and failed on missing generated
imports; its log is retained separately from the successful sequential check.

This build integrates profile-scoped TLS bootstrap, dynamic context geolocation,
frame URL waits, native WebDriver reserved-key sequences, child-renderer failure
isolation, actual PNG/accessibility trace phases, exact console trace capture,
caller stacks for route/dialog operations, and pageless worker network events.
The complete automation contract remains unfinished. URL-wait empty-frame and
cross-process navigation repairs, element-state waits and further trace records
are applied for the next build; they are not included in this sealed candidate.

Live inbound8, pageerror5, query52 and semantic14 checks pass with all reported
ownership counters zero and all 952 hashes unchanged. Inbound tests retain the
genuine Chromium isolated-ID protobuf warning; the other three scopes have empty
stderr. Context checks pass geolocation11, core10, authentication8, extras6,
history8, overlap10, owned/popup20, proxy/TLS isolation and 20 real geolocation
creation/update overlaps. Worker network4 and held-real-ACK3 groups pass with
empty stderr, including body delivery after the initiating page closes.

The same sealed binary passes actual HTTPS partitioned-cookie delivery,
both ancestor flags, isolated profiles, storage export/restore, filtered
removal and strict TLS isolation in eight groups, with empty stderr. A separate
strict context rejects the same untrusted certificate before reaching the origin.
The four console trace groups preserve all 100 actual asynchronous messages,
exact UTF16 and primitive arguments, chunk isolation and dialog caller stacks.
Actual PNG/AX phase checks cover OOPIF pixels, changed accessibility trees,
original-deadline expiry and retained physical ownership until the actual ACK.

The media Source lifecycle passes all 11 groups and child-crash isolation passes
both selected/owner cases with the root still usable. Original continuous-tone
and same-AudioContext pulse checks pass at 120, 59.94 and 29.97 FPS when this
four-CPU workspace is not throttled: minimum measured RMS is 3381/3369/3362 and
pulse offsets are 43.33/39.55/42.87 ms. Under concurrent heavy workloads, actual
zero PCM is already present at CEF ingress. The six capture-rate/buffer controls
all fail while every CPU quota period is throttled and all pass unchanged during
a quiet window with no throttled periods. Raw ingress and loaded failures remain
recorded in `.suitecut/native-audio-capture-probe/causal-evidence.json`; this is
a resource-dependent result, not a zero-gap guarantee. Capture buffering remains
explicitly 120 ms by default, with genuine latency and stop tails reported.

Full unchanged tracing on this build passes 31 cases, fails nine and retains
three original skips out of 43. PNG/ARIA, 100-message console flushing and dialog
stacks now pass. Remaining failures cover API tracing, raw trace directories,
HAR failures, filesystem errno, WebSocket records and private harness fixtures.
Results are `.suitecut/upstream-native/runs/2026-10-15T00-24-20.019Z-988083/summary.json`;
all original input hashes are unchanged. WebSocket/HAR/errno patches are applied
for the next build, with their private proof and existing Fontconfig warning
retained separately. Scoped native keys9 and original WebDriver reserved keys23
pass; behavior20/21 still fails genuine HTML-root focus, and WPT4/5 retains a
prior-command keyup ordering failure. These scopes must not be summed as one suite.

The previous e505 build passed native74/74 and SDK503/503. Its full unchanged
tracing result was26/14/3; request/response57 was44/13/0. Earlier media failures
and the initial trace-source test failure remain in their original logs.
Custom CEF folder-upload, shared virtual-authenticator, first-author popup and
favicon-initiation hooks still require the deferred vendor build.

### Previous combined f844 evidence

Previous combined candidate is `f8441c343434c915fa21af228898091babd5a57a37e4e0cf00193adb27ac1089`.
Shared immutable snapshot `.suitecut/native-integrated-f8441c34-epoch/manifest.json`
records840 source/SDK/object hashes plus 230 static CEF assets; mutable documentation
is excluded. Native/SDK builds, strict Source syntax, typecheck, full corrected
lint and500 SDK tests pass. Initial72-test CTest run is **71/72**: the existing
streaming pressure fixture fails before its real connection; an unchanged targeted
repeat passes in2.95s. Both runs remain recorded; this is not a clean single-run
72/72 claim. Initial owned close-command unit expectations and three narrow lint
errors were corrected with actual caller metadata preserved.

The compiled PaintHolding policy and history/BFCache repairs independently verify
native input **7/7**, unchanged WebDriver behavior **20/21**, scroll **9/9** and
new-window **23/24** on this exact candidate. Remaining failures are actual
HTML-root focus and headless desktop-window type. Full tracing43 is **19 passed,21 failed,3 original skips** with no shutdown SIGKILL; fixture-only source wrapper/private-helper gaps are being repaired separately from native route/dialog/title/options gaps. Exact report: `.suitecut/upstream-native/runs/2026-10-14T23-31-48.001Z-903230/summary.json`. Network57, context/header/history composition and Unicode scopes are running independently against the same snapshot.

The cross-realm Window codec repair uses the actual native `Window.window`
getter's receiver check, not author tags or a forged prototype. Six actual groups
pass for opaque/nested/cross-origin Window values, handle disposal, rejected
Window lookalikes and preserved author getter errors:
`.suitecut/native-window-values-f8441c34-v2/report.json` (stderr empty).
`pnpm test:native:values` reproduces the checked-in verifier.
The complete unchanged Puppeteer evaluation52 remains **36/52** on this candidate,
inputs unchanged: `.suitecut/puppeteer-native/runs/2026-10-14T23-34-50.303Z-910357/summary.json`.
Its variadic arguments, primitive thrown-value, non-serializable object and
Puppeteer-specific API contracts remain explicit differences/failures. The first
rerun stopped before browser launch because generic runner installation timed out;
the second explicitly verifies the prior pinned revision/original SHA256 values,
runner lockfile and exact installed generic runner versions before reuse. Earlier pre-fix
SecurityError and authored verifier's unavailable waitForURL assumption are retained
separately; the final proof waits on actual cross-origin document URL and verifies
that cross-origin document access still throws SecurityError.


Latest completed epoch: `d04ab160ab2e96fe5600cd02ccb38613ed3bf52cab2d4b9173a99153243709fd`.
Manifest `.suitecut/diagnostics/native-headers-trace-node-manifest.json` contains
889 hashes. All **67 native CTests**, **500 SDK tests in 63 files**, native/SDK
build, lint, typecheck, strict Source syntax and diff checks pass.

Integrated context headers pass **7 actual wire groups plus 3 natural timeout
groups** with empty stderr:
`.suitecut/native-context-headers-integrated-d04/{proof,timeout-proof}/report.json`.
The original100 ms deadline expires while one real renderer command remains
physically pending; the actual late acknowledgement retires it to zero. Main and
future pages, actual OOPIFs, dedicated/service workers and Page-over-context header
precedence reach the real origin. First-author popup and associated Curl dynamic
headers remain gaps.

Expanded inbound Unicode passes **8 actual groups with no exclusions**, including
isolated DOM IDs, mixed realms, element/mask PNGs, checkbox retargeting, iframe and
first-author binding names, disposal and cancellation:
`.suitecut/native-inbound-epoch-d04ab160/.suitecut/native-inbound-utf16/report.json`.
All reported tracked owners return to zero and31,668 snapshot hashes remain
unchanged. Chromium emits a genuine isolated-ID protobuf UTF8 warning, retained
in `native-stderr.log`; this is not an empty-stderr claim. Queries52, semantic14
and rawAX9 additional scopes pass.

Active traces now close cleanly after a genuine invalid-CSS action, both default
and isolated, with empty stderr:
`.suitecut/native-trace-active-close-d04ab160/report.json`.
Actual OOPIF capture/action, the original80 ms stop deadline retaining its real
1200 ms renderer callback, and a closed-page ZIP also pass:
`.suitecut/native-trace-frames-d04ab160/report.json`.
The same unchanged upstream tracing subset improves to **9/12** with no shutdown
SIGKILL; remaining Page.close identity and mapped SDK stack metadata are repaired
in the next build, not relabeled as successes in this epoch.

A real standalone CEF control proves PaintHolding input gating independently of
SuiteCut. Disabling the native headless feature policy makes transparent HTTP
inputs deliver genuine trusted keys without screenshots or delays. The unchanged
WebDriver behavior scope is **20/21**, scroll **9/9**, and focus WPT remains
**20/25** (same five failures) on a transparently recorded policy wrapper over
0ede; the native compiled policy is pending in the next epoch. The history private
candidate independently passes unchanged new-window **23/24**, preserving the
headless desktop-window type gap. Exact reports remain under their native harness
run directories.


Current candidate: `6073210fde3bfd6fa0858528bf58a3bae1a4dd04a9e70430ed3314ccd2992d50`.
Manifest: `.suitecut/diagnostics/native-pcm-metadata-socket-manifest.json`
(878 hashes, explicitly including uncompiled staging inventory). All **64 native
CTests**, **496 SDK tests in 63 files**, strict Source syntax, SDK build,
typechecking, full lint and diff checks pass. The initial authored network
verifier lint error and its narrow real-type correction remain recorded.

The repaired native publisher passes **11 actual Source groups**:
`.suitecut/native-streaming-source-6073210f-v3-proof/report.json`.
Successful native FFmpeg diagnostics are empty; **129600 submitted stereo PCM
frames equal 129600 decoded Opus frames**, and **324 submitted/encoded/decoded
video frames** have actual 120/1 cadence. Capture callbacks, held and superseded
frames are recorded separately. Pause interior RMS is 2.25, and real same-Engine
switching, RTMP H264/AAC, saturated stop, lifecycle finalization, cancel,
40 ms start deadline and unread-peer pressure checks pass. AAC priming/padding
remains explicit (64800 submitted versus 66560 decoded). Physical encoders end at
zero. This supersedes the audio-defective 0ede streaming proof only.

The existing `createNativeBroadcast` wrapper is migrated to native publishing.
Private real RTMP proof on the identical 607 native binary passes actual switching,
pause/resume, readiness/status/async health, idempotent stop and forbidden host
frame/audio subscriptions: `.suitecut/native-broadcast-wrapper-6073210f-v3-proof/report.json`.
The encoded H264 VUI gives exact 29.97 FPS and all FLV packet timestamps stay
within 0.5 ms of that cadence; FFprobe's inferred 30000/1001 label is not used as
proof of the requested rate. Native diagnostics are empty and the physical encoder
ends at zero. AAC priming/padding is recorded separately.
A subsequent independent decoded 10 ms RMS-window audit finds genuine tone
dropouts outside the authored pause (16/190 eligible windows below RMS100,
minimum2.37 versus median3212):
`.suitecut/diagnostics/native-stream-independent-audio-window-audit.json`.
Exact decoded sample counts and empty decoder diagnostics therefore do not
establish continuous audio or A/V alignment. Native output watermark/latency and
low-rate Opus admission are under investigation; the complete media contract
remains open. Native reconnect,
adaptive/runtime bitrate and cross-helper relaying remain unsupported, with no
fallback to the old Node publisher. Integrated SDK regression/live wrapper
verification is running after applying the proven migration.

The native WebSocket delivery fence passes **7 actual public groups**, with empty
stderr: `.suitecut/native-public-websocket-6073210f/report.json`. Actual event
acceptance into the native Writer FIFO now precedes close-wait completion;
backpressured waits retain their original deadlines. Selected DOM-node Unicode
metadata and quoted-text grammar are compiled; their browser reruns remain
pending. Context history and active-trace teardown repairs remain private work.

The complete unchanged request/response scope on 6073210f is **39/57**, not a
network parity claim: `.suitecut/upstream-native/runs/2026-10-14T23-03-09.333Z-850741/summary.json`.
Compared with the older 52121865 41/57 run, four cases newly fail (binary POST,
intercepted binary POST, JSON POST and response availability), while completion
and multiple Set-Cookie cases recover. Causal diagnosis remains open; genuine
native favicon request events are a known interference source. Inputs and binary
remain unchanged throughout the complete run. The first root invocation used a
wrong snapshot path and failed before starting a browser; that ENOENT log is
preserved separately from the actual v2 run.


Previous combined candidate:
`0ede17098b3e92cb1a6dfe2c360888fa69266414ce5f827d99084eefec9f9c5b`.
Manifest: `.suitecut/diagnostics/native-full-context-stream-trace-manifest.json`
(**870 hashes**, including explicitly uncompiled staging inventory).
All **64 native CTests**, strict Source syntax, SDK build/lint/typecheck and
**496 SDK tests in 63 files** pass. The initial link failure exposed an unrelated
settings reversion in a stale inbound-string patch; only those inverse hunks were
restored, preserving both the string contract and actual closure metadata. Initial
link, SDK error-message and lint failures remain in diagnostic logs. This build
compiles native settings/authentication, native publisher control/correlation,
trace journals/DOM/JPEG/ZIP/source capture, native wait scheduling, standards
send-keys preparation, selected lossless inbound strings and click settlement.
Integrated immutable proofs now pass constructor settings/core 10 groups,
authentication 8 groups, proxy/TLS profile isolation and 6 extra lifecycle groups:
`.suitecut/context-integrated-epoch-0ede/proofs/{core,auth,request,extra}/report.json`.
Initial configured-document history and first-author popup settings remain gaps.

Tracing passes 9 public lifecycle/archive groups plus one genuine OOPIF/deadline
run, with independently decoded ZIPs, actual DOM/JPEG/source hashes, and clean
process exits: `.suitecut/diagnostics/native-trace-integrated-0ede1709-report.json`.
Selected unchanged upstream tracing is **8/12**; quoted selectors, action metadata
and active-trace error shutdown remain failures. An independent active-trace
close reproduces a forced SIGKILL after 5 seconds:
`.suitecut/native-trace-active-close-0ede1709/report.json`.

Streaming Source v2 satisfies its 11 functional groups, but **is not a clean
result**: FFmpeg reports invalid PCM packets despite exit zero and loses three
stereo samples. `.suitecut/native-streaming-source-0ede1709-v2-proof/report.json`
preserves this defect. A buffered-audio/strict-decoding repair is staged and
passes standalone and sanitizer scopes; integrated proof remains required.
The public WebSocket rerun is **6/7**, exposing close-wait completion before its
actual event listener. Native event-delivery fencing is being integrated.
Selected inbound Unicode query tests expose additional node-metadata parsing;
the follow-up has not yet received a clean integrated browser proof.
Actual network body/current-limit checks pass **13 groups**, including exact
1.1 MB original uploads and bounded 2 MiB observation:
`.suitecut/native-network-currentlimits-0ede1709/report.json`.

The selected complete WebDriver form/caret/interactability scope improves from
**9/21** to **19/21** on this build, preserving original assertions and inputs:
`webdriver-native/runs/2026-10-14T22-44-07.378Z-827403/summary.json`.
Remaining failures are retained; this does not establish full standards send-keys
behavior. Special WebDriver private-use key interpretation remains unimplemented.

Private settings/authentication evidence includes a genuine held Fetch challenge
and the original native 30-second deadline. The final clean fault proof retains
challenge and callback ownership through the fatal request until actual
`OnBeforeClose`, preserves the original failure and keeps another page functional.
`.suitecut/diagnostics/context-settings-stage/auth-deadline-proof/report.json` and
`.suitecut/diagnostics/context-settings-handoff/manifest.json` record the scope.
The first SIGKILL report was overwritten by the authored verifier; its observed
summary remains in the manifest, while later aborted-navigation/fixture failures
are preserved. This limitation is recorded explicitly.

Previous compiled handle/color candidate:
`32d9a3d0c9a6db78304305d3ffedcae07ce57313a30da3a59c0db6b96f16c709`.
Manifest: `.suitecut/diagnostics/native-main-handle-i420-manifest.json`
(742 source/SDK/runtime hashes, including uncompiled staging modules).
All **54 native CTests** and strict Source syntax pass; SDK build/lint/typecheck
and **483 tests in 60 files** passed for the unchanged SDK. A subsequent test-only
I420 regression was compiled and passed separately without changing the browser
binary. Actual Source I420 recording passes **five groups**, including public
29.97 FPS output from integer 120 FPS capture, real page/context close while the
helper remains running, decoded colors, and zero physical recording ownership:
`.suitecut/native-recording-lifetimes-32d9a3d0-proof/report.json`.
Raw I420 input now declares its actual BT709 limited-range interpretation before
FFmpeg input conversion as well as on output. The preserved negative proof decoded
CSS red to RGB 215/10/64; the repaired real capture decodes to 235/34/67 and
retains BT709/TV/BT709/sRGB metadata. This does not measure distinct 120 FPS capture.

Actual main-handle UTF16 **5**, console **8**, scalar ownership **4**, query **52**
and binding-handle **10** groups pass on an immutable 32d9 runtime/SDK snapshot,
with zero captured stderr and final tracked ownership. Reports are under
`.suitecut/native-semantic-epoch-32d9a3d0/.suitecut/`.
The initial handle verifier retained its host-owned binding callback handle;
the corrected verifier disposes it in `finally` and preserves all resource
assertions. Original failure, state probe and corrected proofs remain available;
supplemental script hashes are in
`.suitecut/diagnostics/handle-32d9-supplemental-manifest.json`.
Broader isolated-UTF16 command selectors and binding names remain pending.

On the preceding immutable 3ed candidate, complete unchanged upstream fill
**46/46**, console **20/20**, WebDriver clear **73/73**, keyboard **23/23** and
frame switching **17/17** pass. WebDriver reports respectively:
`webdriver-native/runs/2026-10-14T21-52-06.184Z-782129/summary.json`,
`2026-10-14T21-55-57.501Z-787957/summary.json`, and
`2026-10-14T22-00-37.972Z-791035/summary.json` under that runs directory.
Real public WebSocket observation/listener checks pass **7 groups** with empty
stderr: `.suitecut/native-public-websocket-3ed3/report.json`.
The newly selected complete WebDriver lifecycle files remain **9/10**, because
successful click-triggered ancestor removal is incorrectly canceled as detachment.
A subsequent native click-settlement candidate (`e3a0a5d4`) passes the same
complete unchanged lifecycle files **10/10**:
`webdriver-native/runs/2026-10-14T22-14-10.315Z-808378/summary.json`.
It retains a click through frame removal only after the final mouse release has
physically entered Chromium, then awaits actual input acknowledgement and the
renderer barrier. Additional negative timing proofs and a stricter barrier-error
check are pending. New-window files remain **21/24**, exposing genuine initial-document history and
unimplemented native UI-window creation. These failures are retained rather than
projected as success by the wire adapter.

Previous compiled clear/listener candidate:
`3ed39576ec5aea5d13478e91a4857c4aae71817f5f4cd5085d5e4f11acec28b1`.
Manifest: `.suitecut/diagnostics/native-clear-console-stream-listeners-manifest.json`
(739 source/SDK/runtime hashes; inventory includes uncompiled staging modules).
All **54 native CTests**, strict Source syntax, SDK build, lint, typecheck and
**483 SDK tests in 60 files** pass. The complete unchanged WebDriver clear file passes **73/73**, no errors or
skips, on an immutable runtime/SDK copy:
`webdriver-native/runs/2026-10-14T21-52-06.184Z-782129/summary.json`.
Browser reset defaults, contenteditable deletion and bad-input state are covered.
Programmatic DOM input/change events retain their actual `isTrusted=false`;
ordinary text deletion uses native input. Listener and additional ownership
regressions remain independently assessed.

The previous `4cbe8700` console/fractional/deadline build passes eight live
UTF16 console groups and four raw scalar/ownership groups with empty stderr and
zero tracked native counters. Exact scalar values, locations, real DOM argument
identity and main/frame/dedicated/shared/service worker ownership are checked.
The native streaming core passes eleven independent generated-input publisher
and sanitizer groups, including a real stalled child with no encoded readiness.
The old implementation exceeded an original 40 ms start deadline by 2.002 s;
the repaired core settles in 41.678 ms with child waitpid complete. Reproduction:
`.suitecut/diagnostics/native-stream-start-deadline-reproduction.json`.
Streaming Source hooks/public activation remain pending.

On `c734e7c4`, complete unchanged fill passes **46/46**, and the complete worker
file passes **23 with one original Chromium skip / 24**. The complete storage
file remains **22/23** because typed cancellation of a native tab-icon request
still publishes genuine request/aborted-request events. Focused earlier passes
were timing-specific and do not supersede that complete scope. The favicon
policy stops native UI download/404 side effects while authored fetch to the
identical URL still reaches the origin. Public network journals are not filtered.


Previous repaired, live-verified binary:
`e854a69e7a4d111957fa4330b12419997683f6a1bd536ec44e6d4e1ac84e2679`.
The 730 source/SDK/runtime hashes are recorded in
`.suitecut/diagnostics/native-broker-dispatch-socket-ownership-manifest.json`.
All **52 native CTests**, strict Source syntax, SDK build/typecheck and full lint
pass. SDK units pass **456/456** before final type-only imports/error formatting;
the final closure adjustment passes both focused files, **12/12** afterward.
The broker uses a bounded/coalesced UI dispatch queue with original deadlines.
Actual semantic14/rawAX9/scalar-ownership3 and a genuine PNG readback pass with
zero-byte stderr. Real ephemeral and persistent broker lifecycle/console/
handle/cancellation proofs also pass with zero-byte native diagnostics:
`native-worker-broker-e854/report.json` and
`native-worker-broker-profile-e854/report.json`.
Queue components independently pass ASan/UBSan/LSan.

The public WebSocket API passes five actual groups with empty stderr:
`native-public-websocket-e854/report.json`. These verify UTF8 and exact binary
wire bytes, canonical event/wait identity and listener order, original native
timeout/AbortSignal cleanup, real closure and late lazy subscription. Routing
and predicate waits remain unfinished. The previous 22f763ec live run genuinely
crashed; its logs are preserved. The repaired build changes both broker dispatch
and defensive dictionary copies, so this result does not isolate the crash cause.

Previous recording/offline baseline:
`5212186529b1bfb728ad9cb674440b7dcfb42ec2ec50114c6f4603a75748e852`.
The 714 source/SDK/runtime hashes and actual native dependency inventory are in
`.suitecut/diagnostics/native-recording-broker-offline-manifest.json`.
The source inventory also includes explicitly uncompiled staging modules.
All **50 native CTests** pass. The SDK build, lint and typecheck pass with
**451 tests in 57 files** before the final graceful-browser-close SDK adjustment;
that adjustment passes both relevant files, **12/12 tests**, and its SDK build.
The build integrates native recording, the context-owned shared/service worker
broker, offline policy ACK barriers, genuine UA accessibility metadata, DOM
outerHTML handle previews and explicit unsupported raw UTF16 reply settlement.
Full live scopes are being evaluated independently from immutable copies.
The unchanged focused accessibility and handle-preview regression scope passes
**8/8** in `upstream-native/runs/2026-10-14T20-54-14.432Z-721353`.
Completed current scopes are independently recorded:

| Scope on 52121865 | Actual result | Evidence under `.suitecut/` |
| --- | --- | --- |
| Complete unchanged ARIA file | 79 passed / 79, no skips | `upstream-native/runs/2026-10-14T20-54-38.124Z-722192/summary.json` |
| Six unchanged handle files | 34 passed / 34 | `upstream-native/runs/2026-10-14T20-55-36.946Z-726505/summary.json` |
| Native Source recording | 10 live groups passed | `native-recording-pipeline-proof-52121865-v2/report.json` |

The recording proof decodes 62 actual CFR frames at 120 FPS with genuine
48 kHz stereo Opus audio, from 63 accepted capture callbacks. Native counters
record 11 holds and 12 superseded inputs. It also verifies pause exclusion,
59.94 FPS resampling, stop under 64 pending evaluations, cancellation with no
commit, encoder failure, zero physical recording ownership after settlement,
graceful page/browser EOF commit and preserved forced-SIGKILL failure. These
short recordings verify scheduling and lifecycle behavior; they do not establish
sustained 120 distinct frames per second for arbitrary sites or resolutions.
The preserved first authored verifier failure assumed immediate /bin/false
start rejection; the corrected verifier waits for actual process settlement.

The 52121865 query and broader network proofs also record genuine Chromium
`observer_list.h:192` reentrancy diagnostics at bootstrap. Their assertions and
tracked ownership counters pass; the startup diagnostics are resolved in the
e854a69e proofs above. The earlier diagnostics remain
preserved in their reports and stderr logs.

Previous sealed binary:
`9789b86130e012474f7f23f98258b524f03e81c0bbef7f60fac58e7facab8e9a`.
The 684 source/SDK/binary hashes are in
`.suitecut/diagnostics/native-credentials-semantic-worker-manifest.json`.
All 46 native CTests, 443 SDK tests, build/import, lint, typecheck and strict
native syntax checks pass. Genuine composed DOM/CSS accessibility semantics
pass 12 live groups, preserving raw Chromium AX separately, actual geometry,
frame epochs and isolated UTF16 units; all capture counters return zero.
Evidence: `.suitecut/diagnostics/native-aria-semantic-integrated.log` and
`scripts/verify-native-aria-semantic.mjs`. Completed unchanged oracle scopes for this binary and the subsequent frame-glob
SDK are listed below; component proofs do not establish broader suite parity.

The frame URL glob SDK rebuild retains native binary `9789b861` and is sealed
in `.suitecut/diagnostics/native-frame-url-glob-manifest.json` (687 hashes).
Its SDK build, lint and type checking pass, with **447 tests in 56 files**.
The unchanged ARIA scope passes **77/79**, with no skips, in
`upstream-native/runs/2026-10-14T19-41-12.041Z-682630`. Its two remaining
failures concern genuine file-input UA metadata and input-value whitespace;
they are retained as historical regressions. Both focused rows pass on 52121865.
Clock mapping and base-URL glob normalization are included in the newer SDK.

Completed unchanged scopes on native `9789b861` with the frame-glob SDK:

| Scope | Actual result | Run under `.suitecut/upstream-native/runs/` |
| --- | --- | --- |
| ARIA | 77 passed, 2 failed / 79 | `2026-10-14T19-41-12.041Z-682630` |
| Six handle files | 33 passed, 1 failed / 34 | `2026-10-14T19-44-31.010Z-688131` |
| WebAuthn plus storage state | 30 passed, 1 failed / 31 (WebAuthn 8/8, storage 22/23) | `2026-10-14T19-46-03.694Z-689945` |
| Request/response | 37 passed, 20 failed / 57 | `2026-10-14T19-47-34.942Z-692199` |
| Dedicated workers | 21 passed, 2 failed, 1 original skip / 24 | `2026-10-14T19-49-42.177Z-696325` |

All input guards remain unchanged; no overlapping scope is aggregated. Genuine
worker script completion and root Blob-worker route fulfillment pass. Original
network response395 now resolves the real frame through its URL glob. The
remaining storage event failure concerns a real browser-generated favicon, not
a utility-page leak; URL-only suppression would lose genuine authored requests.
The DOM handle preview was absent in 9789b861. The genuine DOM.getOuterHTML
metadata path now passes all six unchanged toString cases on 52121865.

Earlier immutable scopes completed independently:

| Scope and exact epoch | Result | Run under `.suitecut/` |
| --- | --- | --- |
| Core assertions/console/artifacts/selectors/uploads, 02026b1e | 595 passed, 1 failed, no skips or flakes / 596 | `upstream-native/runs/2026-10-14T18-09-52.411Z-617226` |
| Complete evaluation file, 0157afb8 | 100 passed / 100, no skips | `upstream-native/runs/2026-10-14T18-59-20.022Z-666761` |
| Six handle files, 0157afb8 | 32 passed, 2 failed / 34 | `upstream-native/runs/2026-10-14T19-00-17.942Z-670153` |
| ARIA snapshots, e2c84fd8 | 73 passed, 6 failed / 79, no skips | `upstream-native/runs/2026-10-14T18-16-20.019Z-630055` |
| URL regression repeats, 02026b1e | 24 passed / 24 | `upstream-native/runs/2026-10-14T18-30-25.767Z-643618` |
| Public storage state, 0157afb8 | 20 passed, 3 failed / 23, no skips | `upstream-native/runs/2026-10-14T18-42-44.472Z-656678` |
| Dedicated-worker file, 0157afb8 | 17 passed, 6 failed, 1 original skip / 24 | `upstream-native/runs/2026-10-14T18-39-54.820Z-652602` |
| Request/response files, e2c84fd8 | 32 passed, 25 failed / 57 | `upstream-native/runs/2026-10-14T18-28-34.913Z-637675` |
| Download cancellation and all downloadsPath cases, 7b7b0567 | 7 passed / 7, including both persistent contexts | `upstream-native/runs/2026-10-14T19-11-24.798Z-676564` |
| WebAuthn and credential storage originals, private 0fb0444b | 10 passed / 10 with genuine localhost RP origin | `upstream-native/runs/2026-10-14T19-06-56.279Z-674301` |
| Independent Puppeteer evaluation, 02026b1e | 36 passed, 16 failed / 52, no skips | `puppeteer-native/runs/2026-10-14T18-09-52.654Z-617229` |
| Independent WPT focus/scroll/input originals, e2c84fd8 | 20 passed, 5 browser-standards failures / 25 | `wpt-native/runs/2026-10-14T18-38-06.827Z-651711` |

Scopes overlap and repeated epochs are not aggregated. The Puppeteer oracle is
unchanged at `d1b9a2162a0113356db5093f055b4a2e1979c6a8`, using generic assertions
and an authored SuiteCut fixture, without a Puppeteer driver. Its actual 100 MiB
transfer passes. WPT uses pinned original documents and generic testharness/
testdriver interfaces with a genuine native handle-click bridge. Its five
remaining failures concern actual CEF/Chromium synchronous focus fixup; no DOM
monkeypatch changes the original standards behavior. WebDriver, Rod/chromedp and
CEF's own parser/string scope is now runnable as recorded below; broader CEF
behavior suites still need independent coverage.

The unchanged pinned CEF parser/string tests pass **40/40** on Linux (31
parser and 9 string tests), with no failures, disabled cases or skipped cases.
The original Windows-only WideToUTF8 case is not compiled on this platform.
Run: `.suitecut/cef-upstream/runs/2026-10-14T20-06-19.069858+00-00-699358`.
`scripts/verify-cef-upstream.py` pins CEF commit 47dbc1d and GoogleTest 1.17.0,
checks exact original source hashes, preserves every assertion and license,
and guards SDK bytes before/after. This direct vendor-library scope verifies
URL, JSON and string behavior; it does not establish browser-action parity.
The first harness run had an incorrect expected inventory of 41; its error
report is preserved, and the Linux inventory was corrected to the original 40.

Independent complete WebDriver-classic files now pass **164/164** across seven
files: title, current URL, click, tag name, selected state, enabled state and CSS.
Run: `.suitecut/webdriver-native/runs/2026-10-14T20-56-20.224Z-728336/summary.json`.
The authored W3C wire adapter uses the native SDK; original Python assertions
remain unchanged. Actual XML/XHTML/SVG and disabled fieldset/legend/optgroup
behavior are included. The original chromedp evaluation file independently
passes **12/12** browser subcases (five Go tests), in
`.suitecut/chromedp-native/runs/2026-10-14T20-36-26.869024+00-00-711075/summary.json`.
The complete selected original Rod concurrent-evaluation test passes **1/1**,
including its actual timer/order/duration assertion, in
`.suitecut/rod-native/runs/2026-10-14T20-38-23.290260+00-00-715228/summary.json`.
These scopes use authored native adapters and generic assertion/test tooling,
without external browser drivers. Unselected APIs are not claimed.

Persistent native profiles pass real restart, native lock and isolation checks:
cookies, exact UTF16 localStorage, IndexedDB and OPFS survive helper exit; a new
page's sessionStorage is empty. Ordinary contexts remain off the record. Existing
profile data survives both startup failure and shutdown. Evidence:
`.suitecut/native-profile/report.json` on 7b7b0567. Genuine touchscreen input,
locator/handle actionability, trial/position behavior, native navigation intent
and canceled held-renderer physical ownership pass five groups on 0157afb8:
`.suitecut/native-touch/report.json`. One actual physical dispatch remains after
logical timeout, the next contact queues, and both retire after genuine ACKs.

The 12-page 1 ms binding deadline now proves logical timeout retains one physical
installation, 22,145 source bytes and 15 pending commands until actual ACK; all
return to zero and all 12 wrappers are absent. Private installer groups were zero
in the sampled interval, so held-group observation is not claimed. Evidence:
`.suitecut/diagnostics/context-binding-epoch-e2c8/binding-native-deadline-report.json`.
All earlier 52 query, 9 world, 11 mixed-world, binding/handle and native-capacity
regressions pass on immutable e2 with scope-specific stderr evidence.

Actual 128 unresolved route callbacks survive page closure; actual global 128
also spans helper restart. Externally settling the original promises and native
finish ACKs releases them. A real in-flight context HTTP transfer survives page1
closure. Real response/body weak collection with a held Request passes:
`.suitecut/native-route-lifetime/report.json` and
`.suitecut/native-network-gc/report.json` on 02026b1e. Current binary body transfer,
context event fanout, SW attribution and worker-script session handoff are
integrated and await live verification in the combined epoch.

Current-code typed-value fuzz passed 1,000,000 deterministic graph, handle-table,
exact UTF16 string/key/Error/RegExp and malformed-input cases; collected heap delta
249,824 bytes. `.suitecut/value-fuzz/report.json` records the exact codec hash and
`.suitecut/value-fuzz/current-code-native-values.js` preserves its bytes. This is
component evidence, not whole-browser leak freedom.

Remaining genuine gaps include preserved-viewport offscreen OOPIF pixels, missing
multipart file bytes in CDP, concurrent cross-page credential synchronization,
context settings, broader routing/worker/runner contracts and native media
orchestration. C++ media helper encoding/lifecycle tests are staged with explicit
`sourceCaptureVerified:false`; actual Source integration is not yet complete.
The exact native Alloy directory delegate patch and build recipe are in
`native/browser/cef-patches/`; modified libcef is unbuilt and unverified. The user
explicitly chose to leave that larger vendor build pending while other features
continue. Historical evidence below retains its original scope.

## Value and handle expansion

The SDK and renderer now share a bounded typed-value format preserving special
numbers, bigint, typed arrays, dates, regular expressions, URLs, errors, undefined
properties, cycles and shared references. Argument functions are rejected, and
property reconstruction avoids prototype pollution. The format has explicit
depth/node limits; the private command and response byte limits still apply.

C++ owns up to 1,024 active/pending handles, releases groups on disposal and
navigation, and drops late results after cancellation without leaving an
unowned remote object. DOM handles expose text/HTML/attribute reads, frame lookup and native click/focus/typing. Transformed and nested iframe input passes; full element actions remain incomplete.

The completed twelve-file run passed 231/300 with 68 failures and one upstream
skip. Evaluation passed 98/100, including the unchanged 100 MB transfer case.
Frame hierarchy passed 13/13 and frame navigation passed 4/4; frame evaluation
passed 11/13. The two remaining frame cases inspect Playwright's private driver
registry and are explicitly unsupported by the authored fixture. The six handle
files passed 32/34; sparse-console and DOM preview formatting remain open.
Older nine-file results remain available under per-run directories.

Handles now have genuine native GUIDs: a source-specific random namespace and
native handle ID. Disposed-handle errors refer to that identity; no stock driver
or stock identity is used. Native response packets allow up to 192 MiB; the writer
also bounds total outstanding reply bytes at 256 MiB. Unsolicited output has a
separate 8 MiB/4,096-packet budget and fails explicitly on overflow. A native
128-entry/4 MiB journal prevents events emitted during command registration from
being lost to an event waiter. Method callbacks are registered before CEF
submission because same-document method results can arrive synchronously.

Same-process and OOPIF frames, target swaps, parent navigation, detach cancellation,
scoped handles and same-origin node adoption pass a live check. Native mouse and
keyboard input passes Unicode, modifier, double/right-click, held-button and
trusted-event checks. A delayed key chord and 1,000 queued cancellations leave
input queues, held keys, modifiers and mouse buttons empty. Isolated DOM worlds
protect action helpers from page-global changes. All 50 active upstream keyboard
cases and all 46 fill cases pass with authored native DOM matchers; 3 keyboard
cases are skipped by unchanged upstream conditions. The current pointer file
passes 79 cases, with 10 failures and one upstream skip. These checks are in `.suitecut/native-frames/report.json` and
`.suitecut/native-input/report.json`.

The earlier value/handle epoch passed 329 unit tests. Value fuzzing passed 1,000,000 typed/circular
and handle-table cases, plus malformed-reference rejection; the tested codec is
preserved as `.suitecut/value-fuzz/million-run-native-values.js`. A subsequent
platform-toJSON guard change passed 100,000 current-code cases. Live checks passed
10,000 errors, 1,000 nested/property handle cycles, 1,024-handle saturation and
recovery, and 1,000 canceled late object completions. Renderer heap delta after GC
was 5,568 bytes; SDK wrapper heap decreased after 100,000 discarded wrappers.
These scoped checks do not establish whole-engine leak freedom.

### Cancellation overload and reply credits

The expanded live test initially terminated the browser on input queue overload.
Reader backpressure then exposed output saturation: a debugger confirmed exit
code 2 from the bounded control queue. Both failures remain in
`.suitecut/diagnostics/control-handles-expanded*.log` and
`.suitecut/diagnostics/native-stress-gdb.txt`.

The reader now waits at 64 queued UI commands, reserves up to 128 reply credits
before dispatch, and returns each credit only after the response is written.
Cancellation's target response uses the original request's credit; its own reply
uses its own credit. The UI thread does not wait for output capacity. The SDK
also bounds queued command bytes at 4 MiB. Shutdown wakes blocked readers.
The unchanged live test now passes, with all native operation/handle/group
registries empty. The historical nine-file rerun completed on this transport fix: 198 passed, 71 failed,
and one upstream skip. Native registry counts are measured before navigation
invalidates the document, so that reset does not hide retained state.

Commands and artifacts:

```sh
node --expose-gc scripts/verify-native-values-fuzz.mjs 100000
node --expose-gc scripts/verify-native-control.mjs 10000
node scripts/verify-native-fuzz.mjs 10000000
node scripts/verify-native-frames.mjs
node scripts/verify-native-input.mjs
node scripts/verify-upstream-native.mjs
```

Results: `.suitecut/value-fuzz/report.json`,
`.suitecut/native-control-verification/report.json`,
`.suitecut/native-fuzz/ten-million-report.json`, and upstream per-run JSON.
The ten-million run covers the earlier framing/registry target; the subsequent
million-case sanitizer run additionally exercises concurrent reply-credit bounds,
cancellation wake-up, and shutdown. It passed with ASan, UBSan and leak detection enabled; both sanitizer reports
remain available.

## Upstream harness boundaries

`scripts/verify-upstream-native.mjs` fetches nine unchanged click, fill, evaluation, and handle
test files at Playwright 1.64.0 commit
`606ecd8b34bac100db32561b6278ffd76e5eb3cd`. It records source/fixture/SDK/executable SHA-256 hashes,
upstream license, fixture adaptations, and per-run JSON results. Each run keeps
its own source manifest and immutable SDK snapshot; executable mutation invalidates
the result. Test-only
dependencies/assets remain under `.suitecut/upstream-native`; the native engine
does not call a Playwright browser driver or use its video encoder.

The runner fixture supplies a SuiteCut page, server assets, platform flags, and
frame-attachment helpers. Stock browser/context fixtures explicitly fail if
requested. Original assertions remain unchanged. Two fill failures are stock
web assertions rejecting `NativeLocator`; they remain failures rather than
receiving a fabricated Playwright identity. Other failures expose unsupported
frames, input options, large payloads, events, and diagnostic wording/preview gaps. This is
a selected-file run, not the entire upstream suite. Independent Puppeteer evaluation coverage now runs as recorded above; WebDriver,
Rod/chromedp, CEF's own suites, and WPT remain unexecuted.

## Blocked and unresolved checks

Full embedder ASan/UBSan builds compile with GCC 14 and Clang 19, but both abort
before a usable browser starts with `munmap_chunk(): invalid pointer`. A minimal
CEF-only initialization control using the same SDK/resources also aborts with
`free(): invalid size`. The allocator/runtime failure is unresolved; do not label
it an application leak report or claim whole-browser sanitizer verification.
CEF-only process execution succeeds, so the initialization boundary needs further
investigation. No sanitizer suppression or sandbox bypass was used to pass it.

The earlier recording failure remains recorded even though an unchanged repeat
passed. Its load sensitivity/cause has not been established. Sustained capture,
audio continuity, A/V synchronization, and long-run drift still need controlled
stress coverage.

The original RTMP run failed its decoded flash/audio-burst assertion. An
independent 1080p60 capture-only control produced 723 frames and 13 flashes in
12 seconds without native writer drops. The publishing encoder now caps H.264
workers at four instead of selecting a thread count from the host CPU count;
this workspace has a four-core cgroup quota. One unchanged RTMP verifier run then
passed with nine matched flashes, 40 ms p95 A/V offset, source replacement and
reconnect; a subsequent unchanged repeat still failed the flash/burst assertion. Its overall delivered rate was about 24.7 FPS despite a median
packet interval near 17 ms, so sustained 1080p60 delivery is not established.
The original failure remains in `.suitecut/native-verification/stream-check.log`;
The passing report is preserved in `.suitecut/diagnostics/stream-bounded-initial-report.json`;
failed-repeat evidence is in `.suitecut/diagnostics/stream-bounded-repeat.log`.
The verifier now removes stale pass reports and writes decoded observations
before its assertion. The publishing issue remains unresolved. A later instrumented run passed with seven
flashes, but delivered only about 24.1 FPS; its observations and result are in
`.suitecut/diagnostics/stream-observed`. Passing and failing repeats both remain
recorded. Sustained 1080p60 delivery and reliability are still open.

A debugger trace now localizes the sanitizer invalid free to bundled Fontconfig
(`IA__FcStrListDone` / `FcEndElement`) during CEF initialization. The trace is
saved at `.suitecut/diagnostics/gdb-asan.txt`. This narrows the investigation but
does not resolve allocator compatibility or verify the whole browser with ASan.

## Reproduction

Build the pinned SDK using the README, then:

```sh
export SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser"
export SUITECUT_NATIVE_HEADLESS=1
pnpm build
pnpm test
ctest --test-dir .suitecut/native-build --output-on-failure
node scripts/verify-native-browser.mjs
node --expose-gc scripts/verify-native-control.mjs 10000
node scripts/verify-native-fuzz.mjs 10000000
node scripts/verify-native-frames.mjs
node scripts/verify-native-input.mjs
node scripts/verify-native-capture-fps.mjs 120
node scripts/verify-native-recording.mjs
node scripts/verify-upstream-native.mjs
```

The upstream command exits nonzero while failures remain. Selecting an upstream
file is supported, for example `node scripts/verify-upstream-native.mjs page-fill.spec.ts`.
The full binary sanitizer experiment uses `-DSUITECUT_SANITIZERS=ON`; its current
startup failure must be resolved before using it for browser fuzzing.

## Platform and release boundaries

macOS ARM64 has historical native integration reports. Their original media and
raw reports are not present in this workspace. Linux has the scoped runtime
evidence above; Windows sandbox and packaging support are unimplemented.

Before a native release, run real CEF checks on each supported platform, repeat
navigation/crash/shutdown cases and measure sustained capture, memory and audio
clock drift. Signed/notarized bundles, browser update maintenance, persistent
profiles, expanded selectors, screenshots, contexts and networking remain open.

The application owns fallback/restart policy. GPU texture sharing and native
hardware-encoder integration are not implemented. Keep all CEF/Chromium and
libyuv notices in binary distributions.

## Reproducible CEF-only sanitizer control

On Linux, build `suitecut-cef-control` with `-DSUITECUT_CEF_CONTROL=ON` in both
normal and instrumented build directories. The control initializes/shuts down
CEF without SuiteCut browser control or media. The current normal build exits 0;
the Clang-instrumented build aborts with `munmap_chunk(): invalid pointer` before
initialization completes. The sandbox remains enabled and no suppressions are used.

```sh
cmake -S native/browser -B .suitecut/native-build -DSUITECUT_CEF_CONTROL=ON
cmake --build .suitecut/native-build --target suitecut-cef-control --parallel 4
cmake -S native/browser -B .suitecut/native-clang-asan -DSUITECUT_CEF_CONTROL=ON
cmake --build .suitecut/native-clang-asan --target suitecut-cef-control --parallel 4
node scripts/verify-cef-initialization.mjs \
  "$PWD/.suitecut/native-build/suitecut-cef-control" \
  "$PWD/.suitecut/native-clang-asan/suitecut-cef-control"
```

The instrumented directory must already be configured with
`-DSUITECUT_SANITIZERS=ON` and the chosen compiler. The verifier returns nonzero
while either control fails and records executable hashes, arguments, sanitizer
settings, exit codes, and native stderr in `.suitecut/cef-initialization/report.json`.

The current framing/registry/credit helper revision also passed 10,000,000 cases
with ASan, UBSan and leak detection enabled. Its source hashes and timestamps are
in `.suitecut/native-fuzz/report.json`; its log is
`.suitecut/diagnostics/control-fuzz-current-ten-million.log`. This does not
instrument frame, input or CEF runtime code. Whole-browser sanitizer initialization
remains blocked by the separately reproduced prebuilt-CEF allocator failure.

### Pointer and viewport verification

The 90-case pointer run is preserved in
`.suitecut/upstream-native/runs/2026-10-14T09-57-02.661Z-316916`.
It verifies fractional frame coordinates, transformed/nested frames, associated
labels, forced/trial actions, animation/detachment behavior, modifier restoration,
tweened mouse movement and abort signals. Remaining failures include popup and
frameset input, missing wait APIs, private upstream test hooks, diagnostics and
a fixture missing its original timer reference. Later changes require fresh
verification; do not infer results from source edits.

Viewport resizing keeps the recording canvas at its launch dimensions. Current
CEF paints fit into that canvas with aspect ratio preserved and black padding.
The browser viewport may have odd dimensions, including 1 by 1; I420 conversion
always receives the fixed even capture dimensions. The live BGRA/I420 verifier
checks viewport layout, native clicks, script/style insertion, portrait padding
and exact capture byte counts: `.suitecut/native-viewport/report.json`.

The 10,000-error and handle-cycle retention reports above predate the frame/input
changes. They are historical scoped evidence and must be rerun for current-code
retention claims. Whole-CEF sanitizer startup remains blocked as documented.

### Historical 6065e76c integrated verification

The latest fully verified baseline is Linux CEF 156 binary
`6065e76c9cad62b9e01a296b0325c4848a457f31a0aa1c2f610d4fa45badf031`:
25 native CTests, 421 SDK unit tests, SDK build/import, typecheck and lint pass.
Built inputs are recorded in
`.suitecut/diagnostics/native-ax-storage-lifecycle-manifest.json`.
The immutable 11-file unchanged upstream run
`2026-10-14T16-17-49.507Z-535679` passes 595/596, with no skips or flaky results:
assertions 481/481, console 20/20, screenshots 67/67, PDF 2/2,
selector registration 6/6, and uploads 19/20. Genuine directory upload on the
primary Alloy offscreen browser remains the single failure in that run.

The unchanged URL-predicate case now passes 24/24 repeats, with no outside
unhandled teardown errors (run `2026-10-14T16-16-31.254Z-531512`). Explicit
teardown observes otherwise ignored navigation promises while awaited callers
retain their actual rejection. A real held HTTP navigation rejects with
Chromium's actual request abort; context closure also completes with 64 pending
navigation waits and empty native registries. History still fails separately on
this baseline; newer history entry/fragment source awaits repeated live proof.

Actual 52-query, nine selector-world and eleven mixed-world checks pass, including
factory closure caching and frame-owned handles. Public storage-state checks pass
cookies, local storage, IndexedDB inline keys and encoded binary values, OPFS
hierarchy/binary restoration, closed-origin capture, service-worker bypass,
associated API equality, actual utility UTF-16 round trips and third-party origin
restoration/capture. Twenty utility cycles leave twelve temporary counters zero.
The unchanged 23-case storage file now passes 21/23 (run
`2026-10-14T16-21-12.822Z-548800`); both remaining cases require genuine WebAuthn
credential support, which is absent. Real native AX capture runs, but standalone
snapshot coverage exposes level formatting, link metadata and other projection
gaps; the broader accessibility family remains partial.

Native contexts use origin-scoped storage by default. The helper disables
Chromium ThirdPartyStoragePartitioning while preserving other caller-supplied
disabled features, so saved state is visible in a third-party iframe of that origin
and its writes remain in the same exported origin state. Cookies retain actual
CEF attributes; sessionStorage is excluded. This is a verified origin-scoped
policy, not partitioned storage-state export. Evidence is in
`.suitecut/diagnostics/context-storage-epoch-6065/utf16-partition-report.json`,
`public-storage/report.json` and `sw-utility-report.json`.

The download API now passes fourteen explicitly scoped real transfer groups and
a genuine opener-owned popup download. Ten stalled-download shutdown repeats
exit cleanly; the former d625 baseline crashed eight of ten times inside Chromium
observer delivery. Deferred terminal settlement fixes that reproduced crash.
Native active/file/disk/reservation/wait/copy counters return to zero. Full
attachment-navigation API verification still fails with ERR_ABORTED, and metadata
boundary handling needs further proof; this family is not fully verified.
Evidence: `.suitecut/diagnostics/download-shutdown-6065.json`,
`download-popup-6065.json` and `.suitecut/native-downloads-transfer-6065/report.json`.

Networking passes twelve core groups, four real fallback groups, seventeen HTTP
groups, and five associated storage groups. This includes reverse original-URL
route plans, cumulative exact binary overrides, finite counts, explicit oversized
fallback rejection with genuine retry, independent CEF cookie storage, empty
payloads, actual Content-Length replacement, raw header variants and disposal.
Thirteen HTTP counters and interception ownership/bytes return to zero. Reports
are `.suitecut/native-network/report.json`, `native-network-fallback/report.json`,
`native-http/report.json` and `native-http-storage/report.json`; immutable inputs
and pinned runtime resources are hash-verified. Broader route retention and binary
body publication changes remain staged.

Screenshot/PDF pipeline proofs from the earlier immutable `ffac` epoch cover
visible child documents and clean restoration. Fullpage capture of an offscreen
OOPIF still misses its pixels with the original CSS viewport preserved. A larger
CSS viewport produces correct pixels but triggers author resize events, so that
probe is diagnostic and is not integrated as a fix. These selected-file checks
and component sanitizer runs do not establish full-suite parity or whole-browser
leak freedom. Newer source changes invalidate claims about the complete current
working tree until a new input manifest and proof are recorded.

### Historical cd771 integrated verification

The Linux CEF 156 binary with SHA-256
`cd77112f1befe7ce2a5057f04fe7c30660fa97ac803d57e3cdb79c5e5c51a64b`
passes 19 native component tests, strict Clang C++20 syntax checking, 402 SDK
unit tests, TypeScript checking and the native module import check. Its built
input hashes are recorded in
`.suitecut/diagnostics/native-deferred-close-network-manifest.json`.

The immutable 11-file upstream run
`2026-10-14T14-38-56.086Z-464621` passes 588/596 unchanged tests with no skips.
Screenshot cases pass 67/67, PDF 2/2, console 20/20, custom-selector registration
6/6, and uploads 19/20. Eight failures remain: three selector parser diagnostics,
three array assertion diagnostics, one URL predicate retry, and primary Alloy
folder upload. These selected tests do not establish full feature parity.

Actual console checks additionally verify original remote argument identity,
claims before journal clearing, a 301-message burst with bounded last-100 history,
console/page-error history across navigation, genuine V8 evaluation source labels,
and zero transient handle/materialization state. Managed dialog and chooser
predicates retain actual event objects and clean up their native registrations.
Reports: `.suitecut/native-console/report.json` and
`.suitecut/native-managed-event-predicates/report.json`.

Network verification passes 12 groups, including inherited OOPIF interception,
exact 1,020,003-byte POST delivery, and a projected oversized 1,100,003-byte
request aborted before reaching the origin. Rejected interception ownership and
pending native commands return to zero; the recorded stderr has no observer
reentrancy diagnostics. See `.suitecut/native-network/report.json`.

Actual forms, chooser, trusted drag/drop, payload/path/Unicode uploads, original
staging deadlines, zero timeout behavior, TimeoutError classification and listener
prepend order pass. The upload/chooser/drag temporary state returns to zero.
See `.suitecut/native-forms/report.json`,
`.suitecut/native-forms/timeouts-prepend-report.json`, and
`.suitecut/native-chooser-drag/report.json`.

Live screenshot checks verify exact contained pixels without viewport resize,
fullpage/mask restoration, five simultaneous reload/capture races, once-only
beforeScreenshot callbacks, and a 400 ms original deadline across 17 navigations.
Native transient capture state returns to zero. Native navigation barriers,
reload and default timeout behavior also pass on this binary.
Reports: `.suitecut/native-artifact-navigation/report.json` and
`.suitecut/native-navigation-barrier/report.json`.

Storage restoration, mixed selector worlds, handle-argument bindings, downloads,
child-document screenshot styling, history navigation and broader families remain
under implementation or verification. Later source edits require a fresh build
and proof. Historical reports below retain their original scope and binary epoch.

### Historical integrated native feature verification

The 2026-10-14 integrated binary
`3f2c164284f1b002ae5c312db941513f201e96a4c20d56aee4f563936c6e70a4`
passes 396 SDK tests across 48 files, 15 native component tests, typecheck and
lint. Exact built inputs are in
`.suitecut/diagnostics/native-next-build-manifest.json`. Subsequent source edits
for selectors, storage and console are not covered by this binary's proofs.

Actual screenshots now verify offscreen DOM pixels through a temporary native
physical-view lease, original viewport/scroll/device-scale restoration, fullpage
masks, PNG/JPEG/WebP/scales/clips, same-process/OOPIF locator geometry, shadow
animations, important caret restoration, native beforeScreenshot callbacks,
6MP physical cancellation and queued recovery. Real PDF text/pagination/A5/
headers/footers/tags/ranges also pass: `.suitecut/native-artifacts/report.json`
and `.suitecut/native-artifact-oopif/report.json`.

Native click navigation barriers wait for actual commit while the server holds
its response; noWaitAfter completes input before release. Real reload performs
a new HTTP request. Default timeouts 1 and 0, actual TimeoutError, and listener
ordering pass: `.suitecut/native-navigation-barrier/report.json`. Original staged
upload deadlines and corrected timeout diagnostics still require the full next
proof; earlier chooser/drag/geometry checks pass on the recorded epochs.

Native HTTP passes 15 actual groups through libcurl and the genuine CEF context
cookie store, including exact GET bodies, repeated Set-Cookie, Latin1 and
constructor/**proto** headers, auth, redirects, uploads, context-close cancellation
and disposal. All 11 HTTP counters return zero:
`.suitecut/native-http/report.json`. Network observation/routing passes 10 groups,
including real inherited timeout and prepend order:
`.suitecut/native-network/report.json`. Oversized interception ownership and
fallback changes require a new binary/live proof.

The earlier `1e711a14...` binary verifies 48 query cases, 16 binding feature/
lifecycle checks, 64 parallel real-handle-return evaluations and 128 physical
host callbacks retained across process restart until actual settlement. Current
`3f2c1642...` host-callback verification separately proves 64 native action pauses
and 64 event predicates retain their process-wide physical callback capacity
through deadline/close/relaunch; canceled logical waits cannot claim user
closures have settled. Native temporary counters return zero:
`.suitecut/native-host-callbacks/report.json`.

The historical `cfa5d9d5...` context proof verifies 104 actual create/close cycles,
CEF cookie/local-storage/permission separation, popups/opener identity, distinct
per-page capture and continued process use after the default page closes. The
buffer-free Linux CefExecuteProcess dispatch omits its own stack protector to
accommodate Chromium's intentional zygote reset; protected initialization/control
code stays separate, sandbox and Chromium canary reset remain enabled. The
rerun has no stack-smashing diagnostics. This is scoped historical context
evidence; current init-script behavior passes but a Chromium observer reentrancy
diagnostic still blocks a clean claim until the bootstrap fix is rebuilt/rerun.

Unchanged upstream runs are independently recorded with immutable binary/SDK/
fixture snapshots. All 25 exposed-function/binding cases and all 13 dialog cases
pass. Latest artifact/upload/PDF group: 83/89 (screenshots 62/67, uploads 19/20,
PDF 2/2). Console baseline is 6/20; console implementation and one fixture fix
are pending verification. Full six-file assertion baseline is 453/481 before
later focused fixes; do not infer a fresh full-suite total. Folder upload remains
blocked on the primary Alloy offscreen browser. Isolated auxiliary Chrome probes
prove actual directory enumeration and video capture, but do not provide a
production runtime or Chrome audio support.

CEF-free/component sanitizer evidence includes ten-million historical framing/
registry/credit cases, one-million URL/event/route helpers, and matched-GCC
artifact controller cycles with physical cancellation/stale callbacks. Reports
record exact source hashes and scope. Whole-CEF ASan/UBSan/LSan startup remains
blocked by the independently reproduced prebuilt CEF allocator failure. No
whole-engine leak-free claim or complete feature-parity claim is made.

### Next publisher diagnostic instrumentation

The historical Source11 and public-wrapper scripts drained native stderr without
an `onDiagnostic` observer. Treat their prior results as behavior evidence, not
empty-stderr proof. The next working source attaches the bounded native stderr
observer and robust shutdown finalizer to every helper launch in those scripts.
The reconnect verifier already observes stderr. New matching-binary actual runs
will establish strict diagnostics separately; independently instrumented quiet
media/capture proofs retain their own original evidence.
