# Native attempt error attribution — focused verification requirements

This following verification unit targets the native fixture attribution repair;
it does not change retention policy, production fixture ownership or the original
public retention checker. No new runtime or compilation was used to author it.

## Preserved causal evidence

The first merged retention negative is the unchanged native report:
`/tmp/suitecut-native-attachment-ctest-post-0d116d39-r1/native-runner-output-retention-public-data/cases-nz9jnd8s/failures-only-native-final-attempt-facts/actual.json`
(SHA256 `028f1c7243b9180b90f45c99c6565911d62cc43536796a83379e0c39b757990b`).
Its five per-attempt retention decisions and actual paths match the reference.
The final test's original fixture Error already fails that attempt, while its
native owner-bound diagnostic wrapper is additionally promoted to globalErrors.
The original failure remains evidence and is not a passing public scope.

The granted stock public1.64/Node24.19 no-browser probe explicitly captures
onError, onTestEnd, onEnd and builtin JSON report output:
`/tmp/suitecut-preserve-output-stock-teardown-attribution-v1/actual/index.json`
(SHA256 `59c4981e3b53cde1a7db457d11488e1ee76a9a2f5f187a4ca6e2e7844092646c`).
One genuine after-use fixture Error appears on the failed attempt; actual
onError and builtin JSON global-error arrays are empty. The supervised process
actually exits1, with empty stderr, unchanged inputs and all recorded PIDs absent.
The older preservation reporter did not implement onError, so its absence of a
global-error field is not used as evidence of global-error behavior.

## Required native behavior

The original primary `error` must retain its exact Error/stack. Additional native
phase diagnostics must retain actual test/generation/worker/PID plus genuine
scope/generation and source. They must not become run-global merely because a
primary Error is already present. No string comparison or text deduplication
establishes whether two receipts are the same event.

Verification owns the production repair and proposed optional attempt field:
`secondaryErrors:[{testId,generation,workerIndex,pid,scopeId,scopeGeneration,origin:'phaseResult'|'fixtureRaw',error,originatingStepId?}]`.
The raw fixture secondary must remain an exact Error scalar with fixtureRaw
origin; native explanatory wrappers remain phaseResult diagnostics. An absent
empty field remains compatible. Any change to this reviewed schema
must update these following assertions explicitly, before a runtime run.

The focused cases are:

1. Passing body, one genuine test-fixture teardown Error: the raw Error remains
   primary, any native explanatory diagnostic belongs to that exact attempt,
   and globalErrors stays empty, matching the stock attribution observation.
2. Body Error then a distinct test-fixture teardown Error: the earlier body Error
   remains primary. The teardown Error/context remains separately owner-bound,
   with no run-global promotion. This specifically catches eager SETTLED writing
   a later fixture Error before the earlier body Result is remembered.
3. Primary body step Error plus distinct afterEach step Error: both errors and
   actual originating-step identities survive. A secondary points to the real
   step with the same native attempt/worker owner, never a fabricated or foreign
   step. Exact source/stack must not be replaced by a generated diagnostic.
4. Two actual phase callbacks throw the SAME original Error object with identical
   message AND stack bytes: primary plus a genuine secondary occurrence both
   remain. A text-based duplicate filter would incorrectly lose the latter.
5. A successful test with a genuine worker-fixture teardown failure: worker error
   remains global, and is not attached to the last test merely because that test
   is still available in native state. Its primary passed result stays unchanged.
6. Failed first retry with body/fixture errors, then a passing retry: earlier
   secondaries remain under the old generation/worker/PID; the new attempt has no
   stale error rows. Physical file/callback/fixture cleanup still precedes retry.
7. A body and a worker-fixture teardown throw the SAME Error object/bytes: one
   owned primary and the genuinely unassociated worker/global failure both
   survive. Matching error strings never authorize moving or dropping a global.

The stock probe establishes case1 only. Cases2–7 are native ownership and error
preservation requirements, not additional claimed stock observations.

## Native policy/controller negatives

Production controls owned by Verification should reject mismatched or stale
scope/generation, foreign test/worker/PID and invalid originating-step ownership.
A rejected association must retain the real failure and prior accepted report
state. A real unassociated result still follows global reporting. Bounded record
or byte failure must not replace a hard failure with skip/success or silently
truncate an error. Publication may not bypass actual callback, teardown, file,
Source or descendant retirement to obtain a convenient result.

These native controls must be labeled controlled receipts. They are not browser,
fixture process or physical callback proofs unless actual native execution is
separately recorded.

## Matching process/report proof

The authored checker `scripts/check-native-runner-attempt-error-attribution.py`
uses real defineNativeSuite callbacks, actual original Error stacks recorded by
authored fixtures and the real native proof supervisor. Its seven process cases
are pending a separately granted matching Runner/SDK build. It must preserve
first failures, actual exit1 on hard errors, stderr, original report paths and
actual supervisor/worker/helper process closure. It does not modify thrown
errors, constructors, native input packets, error brands or assertions to green.

Final report readers must retain optional secondary diagnostics in JSON, JUnit
and HTML, with exact owner/source information and lossless scalar handling. The
original unchanged retention public checker must pass again after the product
repair; this following diagnostic checker is not a substitute for it.
