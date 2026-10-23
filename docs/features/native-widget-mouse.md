# Original-widget mouse control

The native SDK can consume SuiteCut's isolated Chromium mouse-lease extension.
Stock CEF does not expose that extension. The engine probes its exact read-only
protocol revision before publishing `originalWidgetInputReceipts`; source patch
files or a CEF version string never enable this flag.

```ts
if (browser.capabilities()?.originalWidgetInputReceipts) {
  await page.mouse.click(40, 60, { backend: 'widget' })
  await page.mouse.down({ backend: 'widget' })
  await page.mouse.move(200, 120, { backend: 'widget', steps: 8 })
  await page.mouse.up({ backend: 'widget' })
}
```

The same option is available on `dblclick`. A held gesture must keep the same
backend. Ordinary mouse methods retain their existing acknowledged native path
when the option is absent. An explicit widget request against stock CEF rejects
as unsupported; it does not silently fall back to weaker receipts.

Widget press/release require an exact renderer acknowledgment for the original
native lease, sequence and immutable document token. Nonblocking mouse movement
may return a browser acknowledgment; that outcome does not prove a renderer ACK
or a particular DOM listener. Native completion waits for required physical
release and lease-retirement callbacks. Cancellation waits for an already
submitted command's actual reply, then releases held buttons through that same
original lease. Document loss or uncertain delivery closes the actual page
instead of sending an old release into a replacement document.

`page.mouse` addresses its actual root widget. It does not hit-test a point into
an OOPIF or claim delivery to a child document. `frame.mouse` can address an
actual separately attached OOPIF widget, with coordinates local to that frame's
viewport. A same-process child sharing its ancestor's widget cannot be leased as
an independent document by this backend protocol and is rejected. Cross-widget
synthetic drag routing, wheel, keyboard and touch receipt extensions remain
backend work. The existing native controls for those operations remain their
own paths.

The exact native controller keeps leases across successful `down`/`up` SDK
calls, forbids mixed pointer backends while a widget holds buttons, checks
original operation deadlines before publishing completion, and preserves
physical callbacks across logical cancellation. Its process-wide vendor budget
includes detached held/uncertain orphan leases until actual widget destruction.

The custom library needs the ordered `chromium156-widget-input-receipts.patch`
and `chromium156-widget-input-receipt-probe.patch` under
`native/browser/cef-patches`. The probe supplement is read-only and pins the
receipt patch's repaired postimages. Vendor compilation and runtime acceptance
remain deferred. Focused controller checks cover capability rejection, canceled
acquisition/press ordering, exact original-lease release, held state across SDK
calls, browser-versus-renderer ACKs and document-loss closure. They do not replace
custom-library, E2E or fuzz acceptance.
