# Native page idle override

`NativePage.emulateIdleState({ isUserActive, isScreenUnlocked })` sends real
`Emulation.setIdleOverride` commands. Both properties must be booleans. Calling
`emulateIdleState()` or passing `undefined` sends `Emulation.clearIdleOverride`;
public `null` is rejected. No IdleDetector, clock or DOM value is replaced by JS.
The page method rejects on a NativeFrame. There is no context-wide idle API.

Source applies the override to the real root renderer and its actual attached
frame sessions. Same-process frames use the root renderer override. Future
OOPIF children are configured before their existing native bootstrap readiness
ACK, using state acknowledged by Chromium when that child reaches the native
queue. Reset clears current renderer overrides and future inheritance. A root
ACK may have applied even if a later child fails or the caller is canceled; the
controller retains this actual root state rather than claiming rollback.

One CDP command remains physically outstanding at a time. The existing native
64-operation/queue bound and native original operation deadline apply. Timeout
zero remains unbounded locally; existing caller/global cancellation authority
still applies. Pending queued work checks the original cap before dispatch.
Cancel removes logical publication and undispatched descendants, retains any
actual in-flight callback, and serializes the next override/clear after that
callback's real ACK or actual target retirement. Deferred CEF observer submissions
recheck cancellation/deadline before any real send. Source's accepted-work gate
joins the retained idle work; it does not infer physical completion from cancel.

A child that actually detaches during propagation no longer needs a renderer
override; a failure on a still-live target remains an error. Unknown/unsupported
CDP errors remain errors. The default unused path has no controller, queue, timer
or CDP traffic. The additional bootstrap callback is synchronous and empty.

Controlled component checks exercise retained native callback ordering, native
state snapshots, cancellation, real-cap comparisons and receipt disposal. They
do not demonstrate actual CEF idle-state behavior, frame inheritance or the
public browser API; a matching production Source/browser proof is separate.
