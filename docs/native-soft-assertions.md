# Native soft assertion bridge

`expectNative.soft(locator)` and `expectNative.soft(page)` expose the existing
asynchronous native matcher methods, including `.not`. Await each assertion:

```ts
await expectNative.soft(page.locator('#status')).toHaveText('Ready')
await expectNative.soft(page).not.toHaveTitle('Loading')
await expectNative.soft(page.locator('#status'), 'Status should be ready').toHaveText('Ready')
await expectNative.soft(page, { message: 'Page title should be ready' }).toHaveTitle('Ready')
const response = await page.request.get('https://example.test/status')
await expectNative.soft(response, 'Service should be healthy').toBeOK()
```

The real native runner must admit the assertion in an active test body,
`beforeEach`, or `afterEach` associated with an actual attempt. Calling a soft
matcher outside that scope fails before invoking the matcher. The bridge does
not infer soft mode from an external runner's matcher context.

Native admission supplies the original remaining budget and abort signal. The
bridge forwards them to the existing native observation, ARIA, URL predicate,
and prepared screenshot pipelines; it adds no host retry loop, deadline clock,
or outcome counter. A native settlement ACK is required before the assertion
Promise resolves. The runner owns final test/step classification and phase
ordering: a soft mismatch in `beforeEach` must allow the body to run, and a soft
step can report failure while returning its callback's real value. Hard failures
and timeouts retain their original priority. An accepted cleanup soft failure
cannot be hidden by expected-failure classification.

Only an Error issued by a genuine mismatch producer in this exact invocation
can be submitted as soft. The original object, name, message, and stack remain
unchanged. Wrong-invocation, forged, or already-consumed receipts are rejected;
late detached async work cannot create another receipt after settlement.
Argument validation, raw transport/capture failures, arbitrary author errors,
and native admission/settlement rejection remain hard. The native journal owns
capacity rejection and retains protocol/cleanup failures rather than silently
dropping an assertion. `info.errors` currently exposes the acknowledged soft
Error snapshot for this attempt, preserving its history across per-test hooks.
It does not claim a complete raw-hard error journal; hard errors remain in their
actual thrown callback and native report. Soft entries appear only after the
actual native settlement ACK.

Authored URL predicates run outside the enclosing matcher receipt and soft
lease contexts, including their async continuations. A hard assertion inside a
predicate keeps its own ordinary budget and Error; it cannot become the outer
soft mismatch. Explicit nested soft assertions require a separate admission.
An actual false predicate result resumes the outer matcher's original context.

The optional second string or `{ message: string }` object is also accepted by
hard `expectNative`. Its string is captured when the assertion is created and prefixes
only a genuine mismatch diagnostic, preserving the actual expected/received
data and the original Error object supplied to native settlement. `.not` keeps
the same authored context. Argument validation, raw transport/capture errors,
and cancellation retain their existing diagnostics without that prefix.

API response soft assertions accept only instances issued by a genuine native
HTTP dispatch result. A direct `new NativeAPIResponse(...)` with caller metadata
cannot create that private receipt. The matcher uses the original accepted
status/headers rather than replaceable public getters. A textual mismatch reads
its diagnostic through a fresh native body command under the original soft
signal and budget; it does not borrow the public response's cached body Promise.
Response/context disposal retires the receipt, and raw body/transport/schema or
cancellation errors remain hard. Controlled tests cover these seams; actual
native HTTP/runner pairing remains pending matching runtime proof.

Pure-value matchers, synchronous unawaited soft assertions,
`beforeAll`/`afterAll`, fixture callbacks, and third-party `expect.soft` adapters
are also outside this slice. Existing hard assertions and
`createNativeMatchers()` keep their current behavior.

Controlled SDK tests establish author-bridge identity, transport forwarding and
ACK ordering only. Real callback admission, journal/report outcomes, native
physical retirement and browser matching require the combined runner candidate;
these are not established by a mocked ACK.
