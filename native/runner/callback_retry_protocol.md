# Native callback retry protocol v1

Status: agreed implementation contract; public activation awaits the joint
native scheduler, actual steps, attachment disposition and Source fence proofs.
Base: `64cc681`. This protocol uses the existing owned host callback channel.
There is no new process, socket, SDK retry loop or SDK scheduling timer.

## Public surface

`expectNative(callback).toPass({timeout?, intervals?})` invokes a no-argument
authored callback and returns `Promise<void>`. It is admitted only in genuine
body, beforeEach and afterEach callbacks, including actual author steps.
Existing native afterEach cleanup grants remain authoritative after global stop;
this does not create a new cleanup budget. Fixtures, beforeAll, afterAll,
callback custom messages, `.not`, `.soft` and nested toPass are unsupported in
this first slice. Existing other assertion overloads remain unchanged.

Native default timeout is zero: no independent assertion cap, still intersected
with the genuine callback, parent step and original suite/cleanup cap. An
explicit positive timeout is fixed at original assertion admission. Timeout and
interval values are nonnegative safe integers; an explicit interval list has
1–64 entries. Native defaults are 100/250/500/1000 milliseconds, repeating the
last interval. Native compares intervals against remaining time before adding.

An assertion's own timeout is an ordinary catchable assertion failure. Native
owner/global cancellation, protocol/identity corruption, transport death and
true producer/rollback failure independently veto a later raw success. Valid
framed command negatives, DOM races and ordinary callback throws are retryable
without matching error text or public JavaScript classes. Default final
exhaustion preserves the exact last authored thrown value where one exists.

## Identity and actual steps

`owner3` below is `callbackToken scopeId scopeGeneration`; C++ derives and retains
the complete actual StepOwner (testId, attemptGeneration, workerIndex and PID).
The first genuinely native-admitted iteration step ID is the assertion ID.
Each actual invocation has a monotonically increasing native invocation nonce
and a different genuine globally allocated step ID. No HookPhases token or
synthetic outer author callback receipt is invented.

Each iteration StepRecord carries
`callbackAssertion: {id: string, invocation: string}`. Existing native step
started/deadline/raw callback receipt/physical receipt fields remain the actual
invocation history; titles and numeric ID ranges are not authority. The compact
top-level `callbackAssertions` is an array of these exact summary fields:

```typescript
{
  id: string; requestId: string;
  testId: string; generation: string; callbackToken: string;
  scopeId: string; scopeGeneration: string;
  workerIndex: number; pid: number; callbackKind: 'body'|'beforeEach'|'afterEach';
  parentStepId: string; // '0' when no actual outer author step
  startedMonotonicMs: number;
  ownDeadlineMonotonicMs?: number; // omitted for timeout omitted/zero
  deadlineMonotonicMs: number; // final native effective original cap
  attempts: number; lastInvocation?: string; // omitted only if never invoked
  status: 'passed'|'timedOut'|'canceled'|'infrastructure';
  error: string;
  receiptMonotonicMs: number; receipt: 'settled'|'workerExited';
}
```

`generation` is the genuine global attempt generation, matching StepRecord.
`attempts` counts actual native RUN admissions, not report test retries. All
timestamps are native real monotonic milliseconds, never authored Date values.
`error` is a checked native error scalar; JavaScript keeps original thrown-value
identity separately for public rejection. When the feature is unused,
`callbackAssertions` is omitted to preserve baseline report bytes.
Status is `passed|timedOut|canceled|infrastructure`; receipt is
`settled|workerExited`. No report attempts are manufactured.

## Host packets

All integers are canonical unsigned decimal; `-` marks omitted option data.
Error and location fields use the existing checked UTF-16 hex convention.
Native validates vector/count/frame/error bounds before author admission.

Host to native:

```
TOPASS_BEGIN request owner3 parentStep timeoutOrDash intervalsCSVOrDash fileUTF16 line column origin
TOPASS_RAW request owner3 id invocationNonce iterationStep P|E|I errorUTF16 sourceStep sourceTicket operationsPending
TOPASS_OPERATIONS_RETIRED request owner3 id invocationNonce iterationStep sourceTicket
TOPASS_PRODUCER_FAILURE request owner3 id invocationNonce iterationStep sourceTicket errorUTF16
TOPASS_CANCEL request owner3 id
```

`P/E/I` means actual raw return, ordinary authored throw, or an independently
latched genuine producer failure. `TOPASS_RAW` is sent only after the real
function settles. `TOPASS_PRODUCER_FAILURE` can precede or follow that receipt
while the exact invocation is still physically held; it never settles the raw
function. Only the private actual producer observer emits it, never error-text
classification. SourceTicket is from the real native registered Source binding;
zero denotes no Source use, never a substitute for unregistered Source work.
Native sourceStep is checked against genuine same-owner failed-step ancestry.
`operationsPending` is the real captured high-level Source operation count,
bounded by the existing 128 admission limit. Raw delivery is never delayed to
hide it. Zero permits the native physical fence to begin; a positive count
requires exactly one OPERATIONS_RETIRED after the real captured operations
settle. That packet is only front-end continuation drainage, never native
physical proof: the genuine Engine cutoff/resume is still required. A zero
SourceTicket may become the actual registered ticket on OPERATIONS_RETIRED if
an already-admitted multi-stage operation had not sent its first real command
at raw return. Native validates the same exact invocation and real receiver.

Native to host:

```
TOPASS_CONTEXT owner3 testId attemptGeneration workerIndex pid callbackKind
TOPASS_ADMITTED request owner3 id ok errorUTF16
TOPASS_RUN request owner3 id invocationNonce iterationStep parentStep remainingMs
TOPASS_RESULT request owner3 id passed|timedOut|canceled|infrastructure errorUTF16 lastInvocation
TOPASS_ITERATION_RETIRED request owner3 id invocationNonce iterationStep
TOPASS_RETIRED request owner3 id
TOPASS_CONTEXT_RETIRED owner3
```

Only native sends RUN. Each real BEGIN ingress can atomically admit its first
invocation; scheduled retries dispatch at most once per actual pump turn with
round-robin fairness. `NextWake` joins the existing native poll deadline. The SDK registers the closure and pending lease
synchronously before returning its Promise. It runs the actual callback within
the native-issued managed step ALS and retains the entry/producer observers
until ITERATION_RETIRED. Raw-finally closes new iteration admission; stale ALS
must reject rather than fall back to an outer callback. Native logical timeout
may send RESULT early, but RETIRED requires real physical completion or exact
host waitpid. The fixed existing cleanup grace only retires admitted work.

For an assertion that actually uses Source (including a synchronously captured
high-level Source operation waiting for readiness), RESULT still closes native
logical admission at the original cap, while the public Promise settles only
after actual RETIRED. This prevents work started by a timeout catch from delaying
its own prior whole-Engine fence. CEF-free callbacks retain early logical
rejection. Pre-existing unrelated long native Source operations remain outside
this first slice: whole-Engine readiness is conservative, rather than a
per-command physical cut. Resident JavaScript timers alone are not native work.
The enclosing info.signal represents its real owning callback; a private
iteration signal cancels admitted native operations at the assertion's own cap.

## Attachment boundary

Public ownership is immutable: TestInfo.attach remains test-level; a captured
stepInfo.attach remains attached to that original explicit step. Private
iteration provenance `(assertion, invocation, managedStep)` is added separately
to real request/job/declaration/mutation state and checked by the native retry
controller. It fences actual writers, publication ACKs and append declarations.

An ordinary error can be handled only for that exact physically settled
iteration. Original failed rows remain unchanged; prior parent failures,
accepted soft errors, native mutation errors, raw writer death, decoder/identity
failures and rollback failures are never cleared. An unawaited ordinary append
failure cannot become a passing iteration merely because its raw function
returned. Successful artifact history and file/output retirement remain real.

## Source boundary

Clock owns Runner scheduler/steps/glue; Verification owns native Source callback
fence/receiver adapter; Queries owns SDK source-work/protocol/producer bridge.
The existing authenticated original-stdin BEGIN/CUT/OBSERVED/ACK and actual
RESUME/RESUMED protocol is reused. fd4 frame shapes/caps remain unchanged.

Native binds one exact CallbackRetryOwner to an actual registered Source ticket
and host incarnation. It issues an opaque monotonically increasing Source
channel epoch distinct from WorkerReuse borrow or per-assertion nonce. One
challenge is live per Source. SDK adapter purpose `CallbackIteration` permits
healthy unrelated commands under their unchanged original budgets; only the
closed exact iteration rejects new author admission, across ordinary and
queued control paths. Already admitted operations and cleanup continuations
retain their real owners and drain before the cut. Whole Engine physical checks
may delay the fence conservatively; no renderer/JavaScript quiescence is imposed.

SDK sends `SOURCE_WORK_READY ticket nonce` only after original stdout barrier
raw return, actual native ACK and inbox settlement. Native then requests Source
RESUME; after genuine Source RESUMED it sends existing host SOURCE_WORK_RESUME.
SDK actually clears that exact pending challenge and sends
`SOURCE_WORK_HOST_RESUMED ticket nonce generation` on the host channel. Native
checks both authenticated Source RESUMED and this real SDK receipt before
adapter reset and iteration retirement. It never fabricates rawReturned from a
datagram. The adapter retains exact identity across either channel ordering.

The existing 64 completed-watermark replay-history bound remains. Expiry,
refusal, unknown producers, unregistered used Sources or stale identities cannot
be converted into successful physical evidence. Source process lifetime stays
charged through actual host/domain/EOF/pidfd retirement; no browser reuse is
enabled. Native continues checking original assertion/owner/global deadlines
and fixed cleanup grace while all these receipts are pending.

Physical cleanup-end is fixed at the earliest real raw receipt or logical
abort plus the existing 1000 milliseconds, intersected with any stricter actual
physical retirement/cleanup grant. The origin is set once and never renewed by
late receipts, ACKs or amendments. This cleanup-only authority can retire work
after an ordinary caught assertion timeout; it cannot reopen author admission,
admit another invocation, renew an expired assertion or override owner/global
cancellation. A caught own timeout permits outer success only after genuine
physical retirement and in the absence of independent native fatal state.
