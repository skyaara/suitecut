# Native per-fixture clocks: following contract

This is a historical pre-activation contract and proof record. Runtime integration
is now active; see [the current contract](fixture_timeouts.md). Sections below
labelled unimplemented or pending retain their original stage-specific meaning.

The pinned behavioral reference is the unchanged `timeout.spec.ts` at
`606ecd8b`, stored with its Git-blob verification in
`/tmp/suitecut-fixture-timeout-stage/oracles/manifest.json`. Relevant public
cases are lines 187 (custom setup and teardown budgets), 376 (charged test
result duration), 558 (worker teardown longer than generic force-exit grace),
650 (fixture-local timeout amendment), 611 (custom teardown after afterEach
timeout), and 672 (independent default worker
fixture slots). These are test oracles, not an imported implementation.

## Leaf contract

`FixtureChargedClock` retains its original wall origin and accumulated charged
intervals. Native output-directory preparation and ordinary fixture setup
remain charged. Native test hooks/body share their original charged budget;
afterEach/ordinary test fixture teardown share the existing separate cleanup
budget. BeforeAll/afterAll retain their own native clock instances. Fixture
slots never suspend the suite clock or native stop/physical retirement clocks.
No JS clock, Promise race, or scheduling decision is introduced. `NextDeadline`
intersects a fixture slot only with the actual native suite/stop cleanup cap,
never with the charged test deadline. A zero fixture slot still observes that
original cap; cancellation retains its physical ownership.

An explicit fixture timeout, including zero, selects an independent setup slot
and excludes that slot's genuinely active interval from associated charged test
time. Every worker fixture also receives an independent slot, using the
inherited timeout when no custom value exists. A default test fixture borrows
the associated charged clock. Setup starts only with native real fixture
admission and excludes dependency setup before that fixture's admission.

The exact native token/scope/generation/declaration/worker/PID identify every
slot. Genuine READY closes setup timing and resumes charged test time. It does
not settle the author callback: the physical fixture lease remains present.
Native teardown admission starts a fresh slot with its own original start and
configured timeout. An actual setup-local timeout amendment persists as the effective timeout for
the fresh teardown slot. This is verified by the authored genuine stock1.64
probe, not inferred from implementation source: declaration timeout1,000 was
amended to100 in setup; teardown was actually admitted then timed out at100.
The active amendment retains its original slot origin. Ordinary test timeout changes continue to use the original charged test origin.
The first bounded leaf uses the existing native mutable-timeout bound of
120,000 ms; zero means no local deadline, not unlimited suite lifetime.

Timeout observation or cancellation never removes a fixture record or closes
its excluded interval. Only real raw fixture settlement or exact actual worker
waitpid retirement may do that. This deliberately retains physical ownership
through ignored cancellation and active file/append/Source fences. A ready
worker callback remains a physical lease while it may be eligible for reuse.
The timing controller does not infer a callback exit or manufacture a process
receipt. It is a sidecar to existing FixtureOwners/ScopedFixtures, not a new
scheduler or subreaper. History is bounded to 8,192 phase records and 64 physical
fixture leases per worker controller. Admission reserves records before any
charged-time mutation, and overflow rejects before starting author work.

## Required central integration (not implemented yet)

The HookWorker ingress ordering is significant: current FixtureReady and
FixtureSettled call HookPhases::Tick *before* consulting the fixture manager.
The clock sidecar must observe the genuine fixture transition and synchronize
the exact native charged deadline before any wall-deadline Tick, without
accepting an expired local fixture receipt as READY. Group clock storage must
outlive all active excluded intervals. READY drops the borrowed clock pointer,
so a retained worker callback cannot refer to a completed test's clock.

A default fixture's timeout follows its associated charged budget, so an active
body timeout amendment must update the same clock rather than an independent
absolute copy. A custom fixture timeout amendment uses a separate exact fixture
owner packet and original slot; it must not go through the existing body token
validation and accidentally amend a suspended test clock. An acknowledged
shortening may already be expired at ACK time and must trigger the real cancel
path, never resurrect the interval. The existing raw-error/finally and
file/append/Source retirement fences remain before timer teardown transitions.

Native declaration metadata must pin absent versus explicit-zero timeout, plus
actual resolved declaration identity and inherited settings across layers and
retries. SDK validation and callback metadata must relay native selected values;
literals keep real leases without receiving an authored Source setup bridge.
Native fixture-local timeout amendments need an authentic setup/teardown owner
and an ACK included in the actual callback/use fence even when unawaited.

Existing HookPhases/FixtureOwners/ScopedFixtures currently compare absolute
wall deadlines. They must consume charged clock deadlines at *all* admission,
Tick, amendment, result duration and cleanup boundaries. Changing only the
fixture timer would still allow those managers to time out a legitimately
excluded setup. Original attempt wall duration remains separately retained;
the reported TestResult duration must exclude custom fixture slots for the
path explicitly asserted by pinned case 376 (1,200 <= duration < 2,200 ms,
versus at least 3,200 ms wall time including beforeAll and the custom fixture).
No claim is made yet that every NativeTestInfo.duration snapshot has identical
semantics; native actual wall observation remains separately retained.

Per-fixture expiry must preserve setup failure versus teardown infrastructure
classification and raw callback error precedence. Custom worker teardown may
outlive generic idle force-exit grace, while original suite cancellation and
bounded physical drain retain precedence. A custom setup that never reaches
READY stays charged to its own slot until real settlement/worker retirement.
Custom teardown may exclude its active interval from an already-expired
afterEach charged clock without clearing that clock's expired latch; its own
separate fixture slot remains original and bounded. This does not revive any
author hook or body. Global stop must still admit already-supported independent cleanup exactly as
in the existing native cleanup-clock repair; no global clock is restarted.

Every coordinator/reuse/Source shared-domain readiness path must include the
real timing/fixture retirement state without adding another waitpid consumer.
In particular an active timer expiration cannot free a project/group/resource
lock, retire output/file/append work, or publish a successful report. The
actual process controls and unchanged public oracle adaptations must execute
before any integrated parity claim.

## Current proof status

The corrected leaf's 27 controlled cases and timeout catalog's 10 provenance
controls passed actual `-O2 -g0` execution and
`-O1 -g0` ASan/UBSan execution with leak detection enabled. Both strict compiles
used C++20, `-Wall -Wextra -Werror -UNDEBUG`; compiler and runtime stderr were
empty. Evidence is `/tmp/suitecut-fixture-timeout-stage/leaf-v3`.
Six exact C++ source inputs remained unchanged. The maximum cumulative child
RSS observed by Python resource accounting was 319,124 KiB.
No host/helper, SDK emit, actual runner, or unchanged public oracle was run for
this new leaf. Central adapter work is still pending. Two CMake policy targets are authored
in `fixture_timeouts.cmake`; their fresh configure/CTest execution is pending. Existing deadline feature proofs and earlier fixture
feature proofs do not count as this feature's integrated proof.


Two authored no-browser stock probes executed once under pinned genuine
Playwright1.64.0/Node24.19.0 and the isolated native proof supervisor. They
established setup-to-teardown timeout amendment persistence (actual teardown
timeout100 after setup amendment100) and omitted override timeout inheritance
(300 ms overridden setup passed with inherited1,000 despite default test100).
192 package/CLI/probe inputs remained unchanged; supervisor recorded actual
child exit1, no timeout, cleanupFailed=false and descendantsRetired=true.
Evidence: `/tmp/suitecut-fixture-timeout-stage/reference-probes/stock-v1/index.json`.
These are authored reference probes, not unchanged pinned original test runs.
No external implementation was inspected.

The earlier leaf27 optimized/SAN proof predates this amendment persistence
correction. That proof remains preserved as evidence of the provisional policy;
the corrected clock27 and native timeout catalog10 controls now pass both
optimized and executed ASan/UBSan/LSan in leaf-v3. The first v2 measurement
driver failed before compiler spawn because /usr/bin/time was absent; that
plumbing failure is preserved separately. No central product adapter has been
integrated or run yet.

## Optional native phase composition

`FixtureAttemptClocks` holds the exact associated scope/generation/worker/PID,
one original main clock and a separately created cleanup clock. Output
preparation can be carried in as native already-charged time; its original wall
origin remains retained. A logical Freeze stops charging but never clears an
excluded fixture lease. Retirement readiness requires all those genuine
excluded intervals to close. Thirteen controlled cases cover this owner and
clock contract.

HookPhases has an optional native clock binding; existing callers have not been
switched. With that binding it derives deadline/charged observations from the
same native clock, creates cleanup once, retains original timeout amendments,
and charges any fallback physical drain independently of an excluded custom
fixture interval. Three controlled composition groups exercise custom setup
longer than the test budget, timeout followed by real cleanup phases/long
custom teardown, foreign owner rejection and original expired-beforeAll drain.
The existing unmodified HookPhases nine groups also pass. All three component
binaries passed optimized and actual ASan/UBSan/LSan execution under
`/tmp/suitecut-fixture-timeout-stage/composition-v1` with exact seven input hashes
unchanged and maximum cumulative child RSS323,516 KiB. These are controlled
native receipts, not actual author/OS callbacks or integrated runner proofs.

The declaration transport and public timeout fields remain separate uncommitted
WIP until real FixtureOwners/ScopedFixtures clock scheduling, mutation ACKs,
raw/file/append/Source fences, result wall/charged reporting and process proofs
are complete. No runtime feature is enabled by this optional phase binding.
