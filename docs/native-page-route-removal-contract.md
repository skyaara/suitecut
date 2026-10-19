# Page route callback removal

Page callbacks have an actual native callback scope and token. The lease book's
`CapturePage(scope)` selects only callbacks admitted by that exact scope whose
native registration has no shared Context policy. It returns their original
immutable receipt objects and their actual uint64 tokens. It does not compare
Source/broker publication sequences or infer identity from a URL, Page ID or
worker target. Queued revocation and real Finish can settle those receipts;
closing a running scope cannot. A later invocation never enters the snapshot.

The selection primitive itself changes no routing behavior. HAR replay
performs its native work without issuing a language callback lease. Its existing
policy removal and native body ownership remain independent of a user callback
wait. Native callback disposal reports unavailable, not completed.

The authored public Page adapter adds wait and ignoreErrors while preserving
default removal behavior. Its transaction reserves the snapshot before deactivating actual Page policies,
then publishes distinct native admission and all-owner control-ACK receipts in
the Page's own bounded channel. Both receipts carry the same actual selected
registration IDs, callback scope and tokens, plus one opaque SDK mutation ID.
A missing/error/late ACK cannot be replaced by command success or a timer.
Unselected Context callbacks and future Page invocations remain outside it.

The native Source must supply its original operation deadline, operation
generation and actual context internally; these must not be read from caller
parameters or reconstructed from another clock. Cancel removes only observer
delivery. The dispatched control callback remains owned by the existing native
sender until genuine ACK or physical channel closure. Late ACK checks the exact
original operation generation before affecting a replacement.

The host adapter uses exact scope/token tuples and the existing one-observer
per-invocation receipt book. Admission removes only selected handler IDs; a
future registration is preserved. Wait links are removable and bounded. Ignored
user exceptions require actual selected callback identity plus a successful
matching removal; genuine native command errors keep their original provenance.
Context/Page close cancels observer delivery without pretending the author
closure ended. Controlled host tests are separate from future real browser proof.

The component/SDK checks cover bounded exact snapshots, cancellation, real
control-error precedence, held resource-publication isolation and removable
wait edges. Page host quota is reserved before native command admission.
The transaction retains at most 64 Apply callbacks per Page and 128 Tasks /
4 MiB actual retained Task payload globally; callback receipts and native
per-command physical ownership retain their existing independent budgets.

Required matching live scopes: actual held callback through finish ACK; Page
wait excludes a simultaneously running Context callback; native control failure
and original deadline precedence; selected ignoreErrors with genuine request
abort; actual Page close with a still-live independent observer. Default
unrouteAll behavior, callback identity and current native HAR cleanup must be
regressed independently. These live scopes remain pending the matching combined native/SDK build.
No browser support claim follows from controlled components or the SDK checks.

Existing shared error recovery remains unchanged: if the wrapper's actual abort
fails, the unhandled invocation catch records that error and attempts a recovery
abort on the same pause. The controlled test records distinct primary/recovery
failures and the same pause ID; it does not describe them as independent callback
owners or claim that this duplicate recovery is required by the public contract.
