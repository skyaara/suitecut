# Native fixture timeout clocks

The actual native fixture lease admits a setup clock. An explicit fixture timeout
(including zero) owns independent setup and teardown slots. Ordinary worker slots
use the native resolved root/project default. Ordinary test fixture slots charge
the current associated test/cleanup clock. Independent slots suspend charging of
the associated test clock; they retain their original wall origins, raw callback
ownership, and the original suite cap.

`NativeSuiteDefinition.timeout` and `NativeProjectDefinition.timeout` are raw
optional declarations. Native C++ resolves omitted test limits using actual
project membership; explicit test values override project/root values. An omitted
root defaults to30000ms. The worker fixture default never comes from a selected
or dynamically amended test. Explicit zero disables that local clock only.

The host invokes real setup/use/teardown callbacks and marshals exact native
amendment ACKs. `NativeFixtureInfo.setTimeout` is awaited before observing the
native change; an independent setup amendment persists into teardown, as observed
through the pinned public stock runner. It never resets an original origin.
READY and raw settlement wait outstanding amendment ACKs, including ignored
requests. Old asynchronous scopes cannot reopen fixture admission after READY or
raw retirement. A native timeout signals the actual fixture callback, retains its
physical ownership, and bounds noncooperative callback retirement before actual
worker waitpid. READY worker leases remain live during reuse; they are eligible
only under the native ready predicate, never treated as physically exited.

Attempt `durationMs` records charged time; `wallDurationMs` preserves actual
native elapsed wall time. A custom fixture's timeout message uses its actual
resolved title/declaration. Optional source excerpts come from genuine captured
extend/define callsites, native declaration IDs, and bounded native reads of real
pinned files; unavailable, modified or replaced source is omitted. No inferred
line positions or source excerpts are synthesized.

The authored ten-scope public checker exercises real callbacks and real worker
processes. Its results remain separate from the stage-specific original fixture
oracles187/376/558/650 and their preserved negatives. Full fixture-suite parity,
the harness's global fixture `test.setTimeout` bridge and exact upstream reporter
formatting are not established by these scoped results.
