# Native callback assertions

`expectNative(callback).toPass({ timeout?, intervals? })` registers a no-argument
callback with the actual native runner. Native code selects each invocation,
interval, original deadline and final result. There is no SDK retry timer or
retry loop. An omitted timeout means native default zero. Explicit timeout and
interval values are nonnegative safe integers; interval arrays have 1–64 entries.

The first slice supports actual body, beforeEach and afterEach callbacks,
including real author steps. Construction only captures the function; invocation
uses the current actual callback. Old asynchronous callback contexts cannot regain
admission. Fixtures, beforeAll, afterAll, `.not`, `.soft`, custom callback messages
and nested toPass are unsupported. Existing assertion overloads remain unchanged.

Author throws and validly framed native command/matcher negatives are ordinary
retry outcomes. Final exhaustion preserves the original last thrown value chosen
by native code. Native cancellation, protocol/schema/transport death and private
physical-producer failures veto later author success. Error text and public Error
classes never classify these failures. Generic negative command replies do not
attest infrastructure provenance.

Every invocation uses a genuine native-managed step. Logical timeout closes new
author admission and aborts only its private operation signal; `info.signal` stays
with the outer callback. Actual callback return and completion of preadmitted SDK
pipelines are separate receipts. Their registries and Source failure observers
remain through native iteration retirement. SDK continuation drainage does not
prove physical zero: native Source reader/writer/producer ACK and actual resume
receipts remain required. For a synchronously preadmitted Source operation,
the public assertion Promise settles only after actual native assertion
retirement. The logical result still closes/aborts the iteration at its original
cap. Pure callback/timer work can reject logically before its raw Promise returns.
This adds no timer, deadline reset or protocol roundtrip.

Cancellation also gates native hook cleanup and fixture release on the managed
invocation and its real parent callback retiring. Waiting consumes the original
cleanup budget. Native fixture and hook owners share that already-fixed cleanup
clock even when no afterEach hook exists. Actual settlement resumes the existing
pump without an added timer or roundtrip. Accepted native soft assertions inside
the callback retain their own failed-step/test ledger; toPass does not erase them.

Explicit operation boundaries cover browser transport ready/credit queues,
prepared screenshot/configuration/capture/comparison pipelines, request-context
creation/fetch/command and response body, file input staging, and context
creation/storage/script/binding setup. Built-in cleanup retains its admitted
provenance. Author callbacks, validation getters, request serialization and custom
stream callbacks do not inherit an internal continuation token. These wrappers do
not authorize Source producers or worker reuse; existing native readiness policy
still applies. Browser work requires one genuinely registered Source per invocation.

Public attachment ownership stays separate from retry fencing. TestInfo files
remain test-owned; captured explicit-step files keep their original step. Private
assertion/invocation/iteration provenance identifies actual append, writer and
publication continuations. Native-classified settled ordinary input failures can
participate in retry without erasing their original records.

The current Source fence checks the whole Engine. Pre-existing unrelated long
Source operations or overlapping Source assertions are unsupported; this first
slice does not claim cut-selective producer isolation or healthy concurrent Source
parity. Native admission intent is only conservative host continuation bookkeeping,
never a physical retirement receipt.
