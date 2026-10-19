# Fixture timeout runtime integration design (historical)

This document records the design before runtime activation. The native clocks,
host ACK ledger and actual callback/file/worker integration are now active;
see [the current contract](fixture_timeouts.md), `fixture_timeout_binding.inc`
and `src/native-runner-fixture-timeouts.ts`. Future-tense requirements and leaf
proof limits below describe that earlier stage, not missing current features.

* Raw `FTIMEOUT declarationId timeout` rows pin absent versus explicit zero in
  the native metadata signature. Flat IDs are real native metadata row positions;
  layered IDs are actual FDECL IDs. Native FixtureTimeoutCatalog resolves
  inherited values and retains the actual timeout-source declaration ID.
  Default worker slots inherit the original selected project/configuration timeout,
  not the associated test's possibly amended timeout. The genuine stock probe
  uses project1,000 ms, selected test100 ms, worker setup300 ms and passes with
  the body still observing timeout100. Worker setup is excluded from the result
  duration; a default test fixture continues to share the associated test clock.
* A native fixture-slot sidecar precedes actual SETUP or TEARDOWN. It carries
  the exact real token/scope/generation, setup/teardown phase, effective native
  timeout and original slot timing. It must not create a callback/value/Source
  bridge. Constants still use their actual existing fixture leases and use/READY
  transition; no authored callback or Source setup is invented.
* A new fixture-local amendment request has its own bounded request namespace,
  exact actual lease identity and native ACK. Public fixture info reads native
  ACK state. Native authentic active setup/teardown changes retain that slot's
  original start; the accepted effective timeout persists into a fresh teardown
  origin, as the stock behavioral probe establishes. Ready/settled/foreign
  requests reject rather than changing a body or unrelated fixture clock.
* Host amendment admission is synchronous before its first await. The promise
  stays in the actual fixture callback ledger even when ignored. Before READY,
  and again before actual raw SETTLED, the host waits its genuine mutation ACKs.
  Native cancellation may arrive before an immediate-expiry amendment ACK;
  that ordering is valid. The original authored error remains primary and any
  amendment failure remains separately retained, never inferred from error text.
* The fixture callback ownership context spans its real setup/use/teardown
  callback. It is separate from Clock's startup-only Source setup context,
  which closes before first use. Body/step/hook callbacks retain their existing
  exact owners and no JavaScript work selection or outcome counter is added.
* A fixture signal aborts only from the actual native fixture/retirement cancel
  path. Body timeout does not masquerade as a fixture-local deadline: real
  custom fixture teardown may run under its new independent slot. A suite stop
  still applies the genuinely established native cleanup/physical cap.

Before READY, native code must validate its actual slot deadline/cancel state,
then stop setup charging and update both phase and scope deadline consumers
before any ordinary wall-deadline Tick. READY keeps the actual callback lease.
Before raw SETTLED, all current raw/file/append/Source fences must retire; only
then does the slot close its excluded interval. Expiry/cancel keeps both the
interval and callback ownership until actual settlement or actual host waitpid.

HookPhases and both flat/scoped fixture owners consume the same charged clock.
The original attempt begins before native output-directory preparation, which
is charged exactly once. Main and cleanup clocks have separate original
origins. Independent fixture intervals suspend their associated clock's charge,
not suite time. Physical drain after an expired ordinary scope is charged
separately so a legitimate custom fixture slot cannot hit the old generic
1,000 ms limit. An independent worker teardown similarly uses its actual slot,
not an unrelated last-test or generic idle clock. Once a true local deadline
expires, cancellation and physical drain have immutable bounded deadlines.

The test's charged clock can be created after beforeAll with already-charged
output preparation, but its separate wall observation must preserve the actual
original attempt start. `FixtureAttemptClocks` therefore accepts prior charged
and prior wall observations separately; excluded beforeAll time is never added
to charged duration merely to retain the real wall origin. Both observations
come from native admission, not an author or host timer.

Real shared-subreaper/domain, raw/steps/soft/file/append/output/Source and actual
worker-exit fences stay conjunctive. No clock event frees a project/group,
resource lock or PID. Ready worker callbacks can remain retained with no active
slot; their prior test's charged-clock pointer is dropped. Timeout history is
not reuse eligibility: actual outcome/controller readiness still decides that.

Native TestResult duration must match the pinned376 charged-duration assertion,
while actual wall observation remains separately retained. Snapshot
NativeTestInfo.duration semantics must be verified rather than assumed equal.
A worker fixture teardown error remains a genuine global infrastructure error
when no test is active. Cleanup failures remain infrastructure and cannot be
excused by expectedFailure; raw author errors and suite interruption retain
current priority.

Original187 additionally needs declared custom titles and a genuine declaration
source excerpt. Capture real extend/define callsites using the existing native
callsite primitive before author execution, bind the location to the native
actual declaration ID, and have native code read/verify the actual source file (pinning its identity/bytes before callbacks and
revalidating before publication/retry) before producing a bounded excerpt. Missing/unavailable files yield no invented
excerpt. Unchanged187 cannot be claimed merely from matching error wording.

`FixtureTimeoutRuntime` retains the shared clock while setup/teardown can still
settle. Native local expiry starts one physical grace at the original deadline;
late polling and repeated cancellation cannot extend it. Its exact-owner
`SlotDeadline` returns no fresh Source setup admission for Ready/canceled slots.
Logical timeout, native READY and timer-side settlement never retire the real
fixture/step/file/output/Source/domain owners. Actual worker exit is an external
authentic waitpid receipt supplied by the existing sole process controller.
