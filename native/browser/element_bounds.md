# Native element bounds

Locator and retained ElementHandle bounds enter the same `element_geometry.inc`
reader. A locator resolves one genuine utility-world node through the existing
query planner, including its original lazy frame path/custom-engine realm and
strict diagnostics. A retained handle adopts its original backend node into the
actual utility context; it never resolves a replacement by selector.

The reader tests actual connectedness/layout in the utility realm, then reads the
real Inspector `DOM.getBoxModel` border quad. Same-process child boxes already
use their renderer root's viewport coordinates. Only out-of-process renderer
roots are mapped through real parent frame-owner content quads. All four corners
share each captured projective transform; viewport coordinates are not clamped.
No author DOMRect or synthetic node/owner metadata supplies the returned box.

Every selected and ancestor document is pinned by actual frame/session/loader,
numeric and unique main/utility context identities, and native context epoch.
The original operation generation/deadline is checked before dispatch, after
receipts and before publication. Bounded logical work stays strongly owned until
final Complete, including any native causal network-publication barrier; the
successful Complete boundary rechecks its original deadline and all documents. Native ordinary admission bounds logical jobs
at64; the existing SelectorWait controller retains actual calls and temporary
groups through logical cancellation until real ACK or actual channel death.
The duplicate generic evaluation tracker transfers into that selector owner
before each actual submission, including the first selection; logical cancel
cannot fire-and-forget release a group while its selection call remains held.
Its bounded physical watchdog uses the existing posted fatal teardown. A missing
locator can retry only under its original deadline and after old attempt groups
retire. A fixed node cannot rematch. There is at most64 ancestral document pins.

Actual disconnected/hidden/zero-layout nodes return null. Only Chromium's exact
`-32000 Could not compute box model.` response permits a second actual layout
check for a same-document layout race; a still-laid-out node or any unrelated
protocol failure remains an error. No catch-all error-to-null conversion exists.

The unchanged d9b5 baseline exposed child-local locator coordinates and author
prototype dependence in both prior APIs. Controlled quad10 and the actual Source
include16 tests pass; SDK transport4 tests cover genuine frame/handle identities,
finite output, original timeout and cancellation. These are component proofs.
`scripts/verify-native-bounding-box.mjs` has12 real browser groups; the exact
pinned public bounding-box9 oracle is cataloged separately. Both require a new
compiled immutable runtime before any claim of public native parity.
