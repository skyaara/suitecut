# Native startup cache preparation

`InitStartupSelections` is a data-only component. The renderer does not call it
from `BootstrapDocument`, and the existing synchronous fallback is unchanged.
This unit does not enable opaque-child dispatch, debugger capture, a pre-author
barrier, or factory invocation.

The pure `DecodeInitStartupSnapshot` function validates the complete native
snapshot schema and existing source/count bounds. It returns owned script and
binding data. Script, binding and credential counters are independent body
fields; none is substituted for the authoritative publication version. A script
version must equal its sequential script count. Binding registration IDs are
positive and unique; their publication counter need not equal the active count.
The decoder never evaluates source or mutates an existing accepted snapshot.

The ledger borrows actual `InitSnapshotReceiver::Lease` objects. `Admit` receives
an owner from the trusted native adapter. `Install` checks that exact
context/browser/renderer-epoch/channel-frame identity, then validates and decodes
the actual leased body. An equal publication version requires the identical pin
and body; a newer version also requires a newer transfer. Malformed, conflicting,
capacity-rejected and allocation-failed installations preserve the previous
accepted body and its readers. This is separate from the receiver's wire-level
installation; no existing receiver ACK is strengthened by this component.

`ResetReadiness(owner, generation, credentialVersion)` precedes actual native
credential reset. `AdmitReadiness` consumes a positive native readiness fact for
that generation, expected credential version and exact currently installed pin.
A late old-generation receipt, wrong body pin, different browser/epoch/frame or
different credential version cannot admit a selection. Once credentials have a
nonzero configuration version, `credentialsInstalled:false` alone cannot bypass
readiness, including the credential-clear case. A later source-only publication
requires a new exact-pin readiness admission, but may use the same unchanged
credential generation/version after the adapter verifies its actual readiness.

A native caller supplies `Document{actualFrameID, sequence}` at genuine context
creation. Sequence is monotonic per admitted owner and is not an Inspector
context ID, URL or author counter. `Select` chooses the current immutable body
once. It consumes a valid creation attempt on cache/readiness/capacity/allocation
miss, so a later cache update cannot upgrade that same realm after fallback.
Existing selected documents retain their original body across publication
replacement; credential reset invalidates their selection eligibility.

`Current` checks the ledger's exact admitted owner, selected-document object and
readiness generation. It does not independently prove the native document still
exists. A future renderer adapter must invoke `Release` for the exact actual
context-release identity, and `Retire` at the actual owner lifecycle boundary.
Before dispatch/publication it must additionally verify its original native
operation deadline and complete CEF/Inspector document pins. This synchronous
component creates no new timeout and sends no protocol or IPC command.

The ledger bounds owners and resident decoded snapshots to 64, decoded source
bytes to 64 MiB and selected reader objects to 1024. The underlying receiver
retains its separate 64-mapping/64-MiB physical accounting. Releasing a logical
document removes selection eligibility; external reader objects continue to
hold the original decoded body and actual mapped lease until their real last
reader destruction. Only the receiver produces the resulting physical release
facts. Closing the ledger does not manufacture a wire ACK or assert process
death.

The standalone actual-CEF component has 18 controlled groups, including native
JSON parsing, malformed/equal-conflict rollback, independent counters, identity
rejection, installation ordering, old mapping readers across replacement,
credential readiness reset/races, no late fallback upgrade, bounded readers and
resident snapshots/owners, exact logical versus physical release, and genuine
standard-allocation failure through the production transaction methods. These
are component controls; no startup integration or browser capability is claimed.

## Unused receiver/readiness adapters (Unit B)

`InitStartupReceiver` and `InitStartupReadiness` remain unused by renderer
bootstrap and Source. The current fallback and existing IPC protocol are
unchanged. They add no sends, debugger markers, pre-author capture, native
function invocation or execution permit.

The process boundary is explicit. Distinct renderer/browser peer types can be
created only from actual native browser/main-frame references and the existing
native-admitted context/receiver incarnation. Message fields cannot construct a
peer object. The owner incarnation still must come from the native admission
controller; a URL, author ID or matching packet is not an admission source.

The renderer-only `InitStartupDocument` checks the actual frame/browser, valid
created `CefV8Context`, and `frame->GetV8Context()->IsSame(createdContext)`.
It repeats identity checks before returning a data reader. It is not a fresh
creation/capture ticket. Its future caller must use actual context callbacks
and preserve native sequence/lifetime rules. Browser code does not invoke this
class or access V8.

The renderer adapter accepts an actual CEF shared-memory message only from the
native browser sender/channel. It validates semantic contents before wire
installation and creates an opaque installed receipt only from the receiver's
actual Installed/Duplicate result. Unit A's optional final native guard runs
after decoding and allocation but before replacing the semantic body. If that
guard fails after wire acceptance, the previous semantic body remains and the
new mapping remains physically charged. Logical refusal cannot retire either
mapping. The receipt owns the actual mapped lease; its serialized semantic
message is data, not evidence that any message was sent or delivered.

The browser-only adapter receives that separate typed message from the exact
native renderer/channel. It never accesses a renderer ledger or V8 context,
and it does not compare a browser clock with a renderer clock. It reserves a
bounded ticket with the original browser command ID, generation, deadline,
expected body pin and credential configuration. Actual receipt and final
publication query the browser operation registry and the same monotonic clock.
An already sampled bool or a new timeout cannot replace that query.

Credential reset produces a private native completion lease, tied to its
original operation, exact peer, reset generation and expected credential
version. Only its one actual native callback can establish a positive readiness
fact. Negative, duplicate, canceled, expired or superseded callbacks cannot
upgrade data. A positive fact may survive completion of the credential command,
but reset/peer retirement revokes it. A semantic ticket can publish only when
its exact decoded credential version/installed flag and current native reset
match that fact. Browser eligibility stays browser-local; there is no
UI-to-renderer activation path in this unit.

Browser pending tickets and credential owners are bounded to 64. Forgetting a
logical ticket does not report physical retirement: the renderer's actual mapped
lease and reader accounting are separate. A future integration must preserve
actual install/release receipts and owner death on each side. Actual renderer-incarnation death must retire the native peer even when its
CEF browser object survives. The future browser integration must also validate
the reserved pin against its authoritative accepted publication before any
readiness/activation use; semantic message identity is not that authority.
Correlating a renderer document to an Inspector unique context and proving pre-author message
ordering remain separate unimplemented capabilities.

The adapter host passes 20 controlled groups using real CEF shared
mappings/messages/parser and controlled
native browser/frame/context interfaces. Its guards forbid evaluation, global
access and sending. Those identity fixtures are not real V8/default-context,
credential device or browser integration positives. Controls cover exact peer
and sender rejection, malformed semantic rollback, duplicate wire receipts,
actual old-reader retention, default-world mismatch, document invalidation,
original expiry/cancellation/generation checks, credential-reset races, final
publication expiry, cross-adapter token rejection, pending capacity, and wire
ownership surviving failed semantic publication.
