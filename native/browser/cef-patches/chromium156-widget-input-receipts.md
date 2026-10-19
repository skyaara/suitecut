# Pinned native widget input receipt candidate

Status: **isolated source patch; not compiled, runtime verified, activated or
connected to the production controller/SDK**. The custom CEF build remains
user-deferred. The source applicability check passes against the exact pinned
Chromium files. The pinned PDL parser and browser generator also produce the
new backend declarations. These checks do not establish C++ compatibility or
actual renderer delivery.

Target: Chromium `refs/tags/156.0.8078.17`, used by CEF
`47dbc1df023e99f006fb9e850a8d83cc19b4fdd9`. The JSON beside this document pins
all six original/patched files and the patch's SHA-256.

## Underlying gap

The original `content/browser/devtools/protocol/input_handler.cc` has SHA-256
`5008fb8d8eb67bb584c99e304eb8f2456c1d8c07b6afeed8214e215df77671f7`.
Its `InputInjector::Cleanup()` sends success for pending mouse callbacks;
`InjectMouseEvent()` also sends success when no input event was queued. Its
`OnInputEventAck()` matches a legacy FIFO using `kFromDebugger`, without returning
the actual widget or ACK source/state. None of those empty protocol results
proves mouse-up delivery to the original widget.

The actual lower-level `InputRouter::SendMouseEvent` already carries a distinct
native `MouseEventCallback` for each event. This candidate preserves that exact
callback rather than attempting to strengthen the legacy observer FIFO.

## Added candidate API

- `Input.acquireMouseWidget(frameId, loaderId)` leases the attached session's
  actual frame widget. Both IDs must match its current native document; an
  empty loader ID matches only an initial document with no navigation token.
  It returns an unguessable lease and the actual immutable document token.
- `Input.dispatchMouseEventToWidget(lease, sequence, type, x, y, button, ...)`
  supports raw mouse press, release and movement. Coordinates are relative to
  that leased widget's viewport. Sequence starts at one and advances only for
  admitted native dispatch. There is one pending event per lease. The native
  button state must match press/release admission.
- `Input.releaseMouseWidget(lease)` refuses while native work is pending, a
  button remains held on the live original widget, or callback retirement left
  delivery uncertain. Actual widget destruction permits retirement once its
  pending callback has completed or been destroyed.

The lease retains the widget's genuine weak pointer and a `WeakDocumentPtr`,
which invalidates even when a same-process navigation reuses the frame object.
The session's native frame, active document and widget are checked before
submission and again in `RenderWidgetHostImpl` immediately before the real
input-router call, after browser filtering. There is no coordinate hit-test,
current-widget fallback, or JavaScript event construction.

OOPIFs require their own actual attached target session. This candidate does
not choose another frame automatically. It also does not add receipt variants
for wheel, keyboard, touch, or the legacy synthetic drag controller. A genuine
widget receipt alone does not prove a particular DOM listener or drag/drop
sequence ran.

## Receipt meaning and native ownership

Every reply contains the exact lease, sequence and immutable document token,
an `ackObserved` flag, and the actual native ACK source/state enum values.
Outcomes are explicit:

| Outcome | Meaning |
| --- | --- |
| `rendererAck` | The exact event callback ran with `kMainThread` or `kCompositorThread`, and the original document/widget remains current. |
| `browserAck` | The exact callback ran with `kBrowser`; this does not prove a renderer acknowledgment. Nonblocking movement can produce this result. |
| `widgetDestroyed` | The original native widget weak pointer is invalid. |
| `documentChanged` | The original document/session frame is no longer current or active. Any observed native ACK fields remain available. |
| `dispatchRefused` | Native admission/filtering refused dispatch without a renderer ACK. |
| `dispatchRetired` | The callback was physically destroyed without an ACK, or its source was unknown. Live delivery remains uncertain. |

The candidate forwards through the existing native browser policy, touch
emulator and input-router pipeline. Its opt-in ACK callback preserves latency
and delegate handling while deliberately bypassing the legacy `InputInjector`
observer FIFO, so an independently owned event cannot consume an older ordinary
CDP callback. Existing ordinary input methods are unchanged.

The native router callback owns the pending lease and its protocol callback.
A default invocation on actual callback destruction reports retirement, never
renderer success. No timeout, posted UI task, logical cancellation, injector
cleanup or `kFromDebugger` match can produce `rendererAck`. Session destruction
drops its protocol delivery but the distinct native closure retains physical
ownership until its actual callback completes or is destroyed. If the detached
session held buttons or delivery was uncertain, the process registry retains a
strong orphan lease even after the final native callback disappears. A new
session cannot acquire that still-live original widget and forget its held
native input state.

A UI-thread process registry permits at most 64 widget leases (including
orphans) and 64 pending receipts, with one lease per original widget across
sessions. Admission prunes an orphan only after the actual original weak widget
is dead and its physical callback is no longer pending. It removes the registry
entry before releasing the orphan owner. Every destructor and pending-state
update checks the immutable lease token, so a late old owner cannot erase or
modify a newer entry if native memory addresses are reused. Weak widget/frame
references do not keep a browser or document alive. No polling timer or logical
timeout releases an orphan.

## Deferred integration and acceptance

Build the custom library against the pinned revision, then connect the native
controller to this exact API with actual target/session/document identity.
Keep stock `originalWidgetInputReceipts` false until the rebuilt backend and
controller are verified together. This source patch alone does not enable it.

Required runtime checks include real trusted press/release, browser-filtered
and nonblocking outcomes, renderer crashes, same-process/cross-process document
replacement before dispatch and ACK, session detachment, callback destruction,
interleaved ordinary/leased senders, replayed sequences, bounds and actual
browser closure. Existing authored drag behavior needs separate verification.
E2E and fuzz execution remain deferred by the user.
