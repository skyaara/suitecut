# Targeted native keyboard ownership

Locator and retained-element `press`, `type`, `pressSequentially` and `sendKeys` carry their original target query/frame, strictness, diagnostic expression, timeout, delay and optional abort signal in one native intent. Retained-element `press` uses its actual native handle rather than a separate focus command followed by global keyboard input.

The existing native keyboard queue is acquired before target focus/selection. Preparation, delayed key dispatch and cleanup share the original operation generation and absolute deadline; queued preparation cannot steal the current keyboard intent’s focus. Ordinary `press`/`type` key interpretation remains unchanged.

Logical cancellation stops author progress but retains actual preparation/key command ownership. The queue advances after real replies and, if needed, acknowledged key releases. A failed release retains ownership until actual browser closure. The queue serializes keyboard intents; independent mouse or application focus changes remain possible.

The focused native component compiles the production queue/admission/retirement and retained-command includes with actual CEF values and controlled DevTools replies. It does not prove renderer focus/event delivery. Thin SDK controls verify command identity and option forwarding without claiming native scheduling.
