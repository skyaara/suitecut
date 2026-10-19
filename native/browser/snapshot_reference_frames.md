# Native AI snapshot frame composition

Both structured JSON and YAML AI reference captures use this adapter. Default
(non-AI) snapshots, raw AX and author evaluation keep their existing paths.

For every iframe record emitted by the actual composed-DOM semantic capture,
`SnapshotReferenceFrameIndices` identifies its native retained element index.
`DOM.describeNode` runs on that element's real backend ID in its parent session.
Only Chromium's returned child `frameId` is used; its actual native parent must
match. No URL/name/attribute matching or expected reference spelling is involved.
An attested child may await target/context admission under the original deadline;
after actual attachment was observed, disappearance is an error. Every captured
frame pins its full main/utility IDs, unique identities, loader, session and native
context generation. Every later dispatch, receipt and final publication checks all
pins and the same operation generation/deadline. There is no cross-navigation
retry of a partly captured aggregate.

Frames are captured sequentially using the existing utility semantic/UA control
pipeline. Each genuine retained array is installed through the existing physical
reference sender and remains staging-only. Native JSON strings are composed in
retained utility-world records, bottom-up, by the exact parent iframe reference.
This changes no author DOM. The validation projection is used only to inspect
known metadata; it is never serialized as user data. An exact-token validator
accounts for every retained node before the selected canonical native JSON or YAML formatter runs. YAML is
formatted directly over the merged semantic forest; no YAML parsing, concatenation
or reference alias rewriting is involved.

Limits are 32 frames, 4096 aggregate retained nodes and forest records, 64 levels,
64 KiB actual serialized forest UTF8, and 8192 native reference requests.
JSON output retains its 64 KiB encoded limit. The canonical YAML formatter retains
its existing 64 KiB UTF16-accounted output limit; its JSON string envelope retains
the existing 393218-byte escaped transport bound. Failed formatting or encoding
validation never prepares publication. Existing
resident/install/borrow/release limits and real physical receipts remain in force.
Only one child capture/request chain runs at a time. Temporary selection groups
transfer to the ordinary physical selector owner; persistent captures retain their
independent native install/use/release ownership after logical cancellation.

Publication is a native transaction. The strong operation-owned batch first
holds only validated tickets and the fixed response. `Complete` checks all frame
pins and original expiry before any genuine deferred-publication hold. When that
hold drains it checks again, then calls `CommitBatch` immediately before native
operation `Finish`. A held timeout or child navigation therefore preserves the
previous main-frame publication; no reference is advertised or old publication
evicted merely because formatting completed. Physical old-group release and
borrow receipts may continue after the new successful logical publication.

Latest-success semantics remain per frame. Exactly the successfully captured
frames are replaced atomically. A still-live child omitted because its owner is
now hidden retains its previous independent publication until a successful new
capture in that child or actual document/channel destruction. No empty child
capture or global eviction is synthesized. Opaque native references retain their
real spelling; stock test constants are not aliases.

Repeated successful snapshots preserve a reference for the same genuine backend
node in the exact same document, including reordered semantic output and iframe
owners. Each staging group receives a bounded immutable token vector; publication
and child-frame discovery validate that exact vector rather than interpreting an
old ticket prefix as the new retained array index. Omitted nodes and changed
loader/session/realm identities lose their old references; there is no unbounded
historical identity cache. An atomic commit transfers token lookup to the new
array while outstanding physical reads keep the old array leased. Late release
acknowledgments erase tokens only when that old group still owns their lookup.

Controlled evidence uses the actual coordinator include and actual CEF values:
19 groups cover backend/parent identity, context admission and disappearance,
original expiry, same-numeric context replacement, staging cancellation/late
installer ACK, format failure, and deferred-publication success/navigation/expiry.
The multi-group validator has 18 actual CEF groups; the exact merge literal has
six VM controls including lone units and rollback. Four additional VM groups run
the canonical native YAML formatter directly over composed observations, including
opaque refs, depth, real boxes, isolated units and YAML-only output bounds. Those are component proofs,
not compiled browser API claims. `verify-native-aria-json-frames.mjs` has nine
matching-candidate groups, including real OOPIF backend/frame identity, nested
and aria-owned frames, old-publication preservation and renderer-held physical
cancellation. Its browser execution and full Source compilation are pending the
next integrated candidate. `verify-native-aria-yaml-frames.mjs` adds seven
matching-candidate YAML groups for genuine same-process/OOPIF/nested children,
unchanged default snapshots/DOM, canonical depth/boxes, exact isolated units,
atomic generation replacement, hidden-child policy and YAML-only format failure
preserving old connected references. Those live groups remain unexecuted until
the corresponding implementation is compiled.
