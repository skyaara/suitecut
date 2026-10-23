# Native pointer input

The browser binary resolves pointer targets, checks actionability, maps frame coordinates, and dispatches genuine Chromium input. `NativePage`, `NativeLocator`, and `NativeElementHandle` expose hover. Page hover selects the first match by default; locator hover is strict. An element handle keeps its actual node and frame identity and rejects after that node detaches, even when a replacement matches its old selector.

Hover requires a visible, stable element that receives pointer input. Disabled controls and descendants of `aria-disabled` containers can be hovered. Hover uses the selected child's own geometry. Options include an element-relative `position`, native movement `steps`, modifier keys, `force`, `trial`, an original `timeout`, and caller cancellation. `force` skips the target's hit check; the real overlay still receives the physical pointer when it covers that point. `trial` checks readiness without moving the pointer. `noWaitAfter` has no navigation-wait effect for hover.

Run the authored state and ownership verifier with the matching built binary:

```sh
SUITECUT_NATIVE_EXECUTABLE=/absolute/path/to/suitecut-browser pnpm test:native:hover
```

It verifies actual CSS hover state and trusted pointer events, selected child offsets, disabled/ARIA states, obstruction and force, trial, pinned detached/replacement identity, frame routing, modifiers, and native ownership cleanup. Its main and child fixtures explicitly use standards mode; bare `:hover` has different matching rules in quirks documents. Native diagnostic callback text is retained without filtering and must be empty for a clean run.

`NativeLocator.scrollIntoViewIfNeeded` and `NativeElementHandle.scrollIntoViewIfNeeded` use the browser binary's original deadline, native frame/node identity, layout stability observation, and Chromium's `DOM.scrollIntoViewIfNeeded` command. They accept `timeout` and `signal`. Scrolling permits disabled controls, `visibility:hidden`, zero-size boxes, and `display:contents`; an element without layout waits until it obtains layout. Locators retain strict selection, while handles reject detachment. Already exposed elements keep their scroll position.

Run `SUITECUT_NATIVE_EXECUTABLE=/absolute/path/to/suitecut-browser pnpm test:native:scroll` for actual native scrolling, stability, cancellation, pinned-node/frame identity, and bounded ownership checks. The verifier retains actual native stderr and checks that handles, command evaluation groups, and input ownership return to zero before closing the helper.

Intercepted HTML drag-and-drop retains Chromium's actual drag data. Native
`Input.dispatchDragEvent(drop)` performs its own drag-over negotiation; the
controller does not insert another drag-over before it. Escape uses native
`dragCancel` to end the intercepted source drag, then preserves the real key and
subsequent mouse-release input. It does not construct DOM drag or mouse events.
Actual failed protocol replies reject the operation. Continuations check the
original owner, generation, work record and deadline before issuing more input
or reporting success. Native diagnostics expose the retained drag-operation
count and whether actual intercepted data is still held.

`scripts/verify-native-drag-sequence.mjs` checks trusted manual, helper and child
drag events, Escape, genuine text payloads, target drop-effect decisions and
settled native ownership. The controlled C++ sender test exercises failed and
late replies; it does not initialize CEF or stand in for that browser proof.

On sealed native `fb2068e8f41b5467d74aed200431951e5ceb75462fed110b58c5a013005c6134`,
the authored five-group browser verifier passed with trusted actual events,
all ten selected ownership fields zero/false, empty stderr and successful
physical shutdown. The unchanged pinned `page-drag.spec.ts` collected all 21
cases and reported 19 passed, two existing upstream FIXME skips and no failures
or retries. All three earlier duplicate-dragover/Escape failures passed. That
original-suite run emitted the preserved 66-byte Fontconfig configuration
warning, so it is a behavior pass without a clean-stderr claim. Both proofs used
the exact sealed SDK/native with no overlays and left all 1903 inputs and 230
runtime assets unchanged.

A separate controlled sender audit exposed a physical-lifetime gap: public
cancellation can discard an accepted Drop callback and send source-end cleanup
before its reply, or a successful Drop reply arriving after the original
deadline can trigger another source-end. Those baseline failures remain
preserved independently of the retained-receipt repair below.

The following Drop receipt controller retains each registered Drop/cleanup
callback independently of the canceled logical operation. Its Source adapter
also checks native pins immediately before an observer-deferred submission.
Canceling a pending Drop does not send another source-end. A late successful
Drop cannot publish public success after the original deadline. Normal helper
success retains actual final mouse-release/interception-disable replies; a
canceled helper or observed document/drag-incarnation change requests posted
owning-browser closure instead of sending a new mouse-up through a current
widget lookup. A fixed 30-second physical cleanup grace requests closure without
fabricating an ACK or freeing accepted callbacks. Actual replies or actual
Inspector/browser closure settle the physical work.

The controller permits one unresolved completion per Source, with a shared
64-record/16 MiB serialized-data-and-document-metadata budget and a 1 MiB
per-record limit. Existing CEF/config envelopes and native allocator overhead
are separate bounded resources. New pointer operations and raw Input commands
conflict while that receipt owns the pointer. Controlled core23 and actual
Source-include33 cases pass optimized and sanitizer builds, including late ACKs,
queued refusal/cancellation, old/new incarnations, real-close boundaries and
allocation failure after cleanup responsibility transfer.

The additional authored `scripts/verify-native-drag-lifetime.mjs` requires an
actual trusted dragover's loopback wire beacon and a real native pending receipt
before aborting the public operation. It checks original error identity while
native cancellation leaves the callback/payload retained, then actual late
completion, one source-end, cleared old manual data and a subsequent trusted
click. Its 1200 ms authored handler is bounded real renderer work, not a timer
used to manufacture a native receipt. It runs as a separately declared checker;
ordinary CDP ACKs still do not prove widget delivery.

On sealed native `b074f3e2de5b57a152761c02b8d27d4280ded6adc7100718702981d75a39e9ac`,
the exact official drag sequence passed all five groups with its ten selected
ownership fields zero/false, empty native stderr and successful shutdown. The
unchanged original drag file again collected 21 cases: 19 passed and two
existing FIXME cases skipped, without retries or unexpected failures. Its same
66-byte Fontconfig warning remains a strict-stderr negative.

The separate held-Drop checker passed all three groups on that same sealed
SDK/native. After the genuine trusted handler's wire beacon, native diagnostics
showed one completion record, one callback and 390 retained bytes. Cancellation
returned the caller's original Error while logical input work reached zero and
those physical entries remained retained. Actual late replies then settled all
sixteen required ownership fields to zero/false. The document observed exactly
one trusted drop and dragend, followed by a successful trusted click. Native
stderr was empty and actual process/stdio closure reported exit zero with no
signal. All three runs preserved all 2002 source/build inputs and 230 runtime
assets, without SDK/native overlays. The held checker is additional authored
evidence, not an unchanged upstream test or a vendor widget-delivery receipt.

An ordinary CDP mouse-event success is not a stronger original-widget delivery
receipt: Chromium can acknowledge injector cleanup and events that were never
queued. The [deferred native widget receipt proposal](../../native/browser/cef-patches/chromium156-widget-input-receipts.md)
describes that separate vendor capability; it is not implemented or enabled.

Text-node handles use actual DOM Range geometry for pointer actions. The binary scrolls an offscreen selected Text node with Chromium's native DOM command, then maps a visible positive-area text rectangle to genuine pointer input. Style/disabled checks and hit ancestry use the owning element; the pointer does not use the center of a larger parent. A detached selected Text node rejects. For forced pointer actions, a node with real layout rectangles but no positive-area viewport point reports an outside-viewport error; missing layout and CSS-hidden elements retain visibility errors. Run `pnpm test:native:pointer-nodes` with the matching native executable to verify these cases and actual trusted input.

`NativeLocator.selectText` and `NativeElementHandle.selectText` select actual browser text. Native C++ owns target resolution, composed visibility checks, original deadlines, cancellation and acknowledged retry-group cleanup. Inputs and textareas use the browser's selection API; ordinary elements use real DOM Ranges. Associated label controls are resolved through native DOM association, and fixed handles keep their node/frame identity. Options are `timeout`, `force` (bypass visibility checks) and `signal`. No input/change events are constructed. Run `SUITECUT_NATIVE_EXECUTABLE=/absolute/path/to/suitecut-browser pnpm test:native:select-text` to verify actual document selections, native selection events, visibility waits, caller cancellation and ownership cleanup.
