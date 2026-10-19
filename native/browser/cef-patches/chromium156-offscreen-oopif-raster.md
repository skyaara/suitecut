# Preserved-viewport offscreen OOPIF raster candidate

Status: **pinned vendor source candidate, uncompiled and unverified**. The stock
CEF binary still has the preserved-viewport offscreen OOPIF screenshot limitation.
This patch does not change SuiteCut's production capture hooks or advertise a
new capability. Build other native controls independently of this vendor work.

The target is Chromium `156.0.8078.17`, used by CEF
`47dbc1df023e99f006fb9e850a8d83cc19b4fdd9`. Apply
`chromium156-offscreen-oopif-raster.patch` at Chromium's `src` root. Its digest,
exact two-file preimages/output hashes, upstream URLs and inspected dependencies
are in `chromium156-offscreen-oopif-raster.json`.

## Native policy being repaired

The pinned `PageHandler::CaptureScreenshot` already acquires a real WebContents
capturer and, for `captureBeyondViewport`, temporarily sets
`WebPreferences.record_whole_document`. `WebContentsImpl::SetWebPreferences`
sends that preference to every non-BFCache RenderViewHost in the primary tree.
Blink maps it to `mainFrameClipsContent=false`. `ScreenshotCaptured` restores the
original web preferences and device emulation after the actual snapshot.
SuiteCut already uses that genuine command and retains its original native
request/capture/physical-view ownership. No additional SDK or CEF API is needed
for this proposal.

The current renderer still computes an OOPIF's compositor raster rectangle from
the normal author viewport or remote viewport intersection. An entirely offscreen
child can therefore provide an empty/old surface even while the root records its
whole document. Hidden cross-origin render throttling can also discard the
child's paint. Enlarging the author's CSS viewport changes resize events and
layout; compositing separately captured child images would fail to reproduce
native clipping, transforms, occlusion and stacking.

The candidate changes two existing renderer decisions only while the scoped
whole-document preference is active:

- `RemoteFrameView::ComputeCompositingRect` uses the actual outermost document
  rectangle for root capture. A nested OOPIF uses its unchanged local iframe
  viewport instead of the ordinary remote on-screen intersection. Existing
  inverse coordinate mapping, compositor scale handling, overdraw and child-size
  clamps remain in force. It does not unconditionally raster an arbitrarily
  large transformed child viewport.
- `LocalFrameView::CanThrottleRendering` suppresses ordinary offscreen/subtree
  render throttling in that scope. Explicit lifecycle suppression, display locks
  and view-transition suppression retain precedence. Outside the scope, the
  existing throttling path is preserved.

The patch does not modify author CSS viewport sizes, scroll positions, frame
sizes, DOM, intersection observations, media-visibility state, ancestor clip or
transform properties. Native Viz remains responsible for composing actual child
surfaces. No pixel is reconstructed from DOM or substituted from another browser.
The existing preference restoration switches both policies back. No new public
waiter, background queue, timer or independent screenshot operation is introduced.

## Build and acceptance boundary

`git diff --check`, reverse applicability, and fresh forward `git apply --check`
passed against the exact downloaded preimages. This is source applicability
checking; Chromium generated headers and a renderer compile are unavailable in
this small workspace. No compiler/runtime success or pixel repair is claimed.

Use the pinned large-host checkout and CEF build recipe in `README.md`, applying
this Chromium-root patch before `cef_create_projects.sh`. Keep the candidate
CEF distribution and SuiteCut build separate from the stock installation. Record
actual source revisions, patch digest, GN arguments, full build logs and final
library/binary hashes. Existing SuiteCut full-page capture automatically requests
the policy; do not enable a speculative extension command or change runtime style.

A rebuilt candidate still needs real raster acceptance before this limitation
can be marked closed. In particular, verify first-capture child-before-root
surface readiness, nested/offscreen/cross-site frames, transformed/clipped and
large child surfaces, captures with an originally scrolled root, concurrent
navigation/detach, and exact viewport/scroll/resize-event restoration. A root
snapshot must not succeed with an older or empty child surface simply because
its own capture callback completed. If existing Viz surface synchronization is
insufficient, an actual retained child-commit fence is still required in the
vendor backend; this candidate does not invent one or treat a root callback as
proof of child completion. E2E and fuzz testing remain deferred as requested.

No stock-browser feature is blocked on applying this patch. The pending step is
the genuine Chromium/CEF build and its backend acceptance, rather than another
SuiteCut source compile or a host-side screenshot approximation.
