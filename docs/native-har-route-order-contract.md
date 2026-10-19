# Native HAR route registration and archive ownership

The native UI policy actor mints one opaque, strictly increasing registration
order. No SDK numeric value, target install time, Inspector event cursor or
file completion time supplies this order. Reserve it after command validation
and before a genuine asynchronous HAR read. Its immutable native policy keeps
that order across Source and context-worker installations. Registry binding
generations remain separate and reject old snapshots after removal/ID reuse.

A request captures one native plan from its actual original URL. Page policy
precedes Context policy, each in reverse registration order. HAR miss/fallback
advances this same plan. Author overrides update the outgoing request used for
archive lookup; they never rematch new registrations or a new URL policy set.
The existing native URL matcher bridge remains authoritative for real author
RegExp/predicate results. This slice adds no SDK route selection or archive
reading/filtering.

An admitted HAR lookup pins its exact immutable archive policy, independently
of the registration map. Removing/reusing its registration cannot retarget a
pending real request-body read. Queued installers and actual Fetch ACKs retain
the policy until their real receipt. Body preparation pins it through the
independent physical body-read receipt, including after logical metadata
invalidation/Clear; data delivery alone does not retire this physical owner.
Each actual read retains at most 16 distinct immutable policies. A failed
admission remains a hard bounded resource failure.

Global archive reservation counts actual NativeHARReplay::bytes payload for
up to 64 MiB of distinct actual archive objects, and 4096 live policy objects.
Sharing the same immutable object charges its payload once; there is no hash,
URL or content-based inferred sharing. This is a payload ledger, not a claim
that STL/CEF allocation overhead equals payload bytes. Existing per-context
and per-target active route count/payload limits remain in force. Physical
pins can outlive those logical active-policy counters.

Fallback passes its original min(operation,pause) deadline and exact native
operation generation through later native HAR selection and actual Fetch ACK.
Logical cancellation aborts a still-held native pause without completing a
physical body read. A late body outcome after pause removal cannot fulfill a
replacement. A genuine late Fetch ACK can retire physical ownership but cannot
publish logical success after the original budget. No duration reset, fake
ACK or removal-time physical retirement is introduced.

The HAR context manager also fails an actual detached target's saved readiness
observer once, retains physical installers until ACK, and pins each batch's
native generation so an old canceled operation cannot complete a reused ID.

Scope boundaries: context-wide HAR registration/removal is still a separate
manager from ordinary context route mutations. Context unrouteAll(wait/
ignoreErrors) does not gain a new HAR physical-completion guarantee here.
HAR URL RegExp/predicate API, archive updating/recording/metadata, popup vendor
first-wire gaps and favicon public request-ID behavior are unchanged. Browser
proof is pending a matching fully compiled Source/native/SDK epoch. The native chronology7, manager/deadline/detach/ID-reuse/capacity, independent
body receipt and existing fallback/matcher/POST/replay controls pass. The
preserved old future-target order and admitted-archive replacement failures
remain negative baselines. All are components, not live browser proof.
