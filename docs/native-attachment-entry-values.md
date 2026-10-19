# Mutable attachment author values and immutable native snapshots

The following SDK slice preserves the original object inserted into the author
attachment array, its original Buffer, and later normal property writes/deletions.
It does not rewrite native declarations, committed artifact bytes/inodes, original
errors, writer/callback receipts or the accepted array journal. Native C++ still
owns declaration admission, bounded byte snapshots, publication and final report
projection. There is no latest-value report extension or field-write journal.

## Pinned public behavior

Public Playwright 1.64.0 types (`playwright/types/test.d.ts:2538`, SHA256
`7e2cbb3666ddd7b5c45240f28deb693e134eacb9ddffe037bd0af64beafc843b`)
expose mutable `name`, `contentType`, optional `path` and `body: Buffer`.
Only public types and test oracles were read; no external implementation was
inspected or copied. The preserved original attachment tests exercise pushes,
not subsequent field edits.

Two separately authored no-browser public API runs, using one worker, retries0,
Node24.19.0 and the existing native proof supervisor, establish these behaviors:

| Author action | Author array | Stock reporter history |
| --- | --- | --- |
| Push the same object twice | Both slots are that exact object | Two insertion snapshots |
| Edit its fields/Buffer, then push again | All aliases see current edits | A third snapshot with values at that push |
| Edit fields/Buffer without another push | Original objects and Buffers remain mutable | Earlier values remain unchanged |
| Unshift/splice/index an existing reference | Normal original-object aliases | No new insertion snapshot |
| Reverse/remove/delete existing slots | Actual author order/holes change | Insertion history stays unchanged |
| Edit an ordinary `attach(body)` entry | Same mutable author entry | Original attachment snapshot remains |

The first run has4 passing authored cases; the second has3. Both actual child
processes exited0, stderr was empty, all pinned input hashes were unchanged and
the supervisor recorded descendant retirement without cleanup failure. Proofs:
`/tmp/suitecut-attachment-entry-contract-v1/actual-stock-v1` and
`/tmp/suitecut-attachment-entry-contract-v2/actual-stock-v1`.
The corresponding `oracle/*.author.json` and `oracle/reporter.json` retain the
actual author values and stock observations. The ordinary body attach exposed
body only, so the conditional original-file inode assertion was not exercised;
it is not claimed as a physical-file proof.

## Native admission and reflection

Every explicit push occurrence submits its own bounded immutable declaration
through the existing native ingress, even when two occurrences share the same
actual author object. Private copied metadata/bytes remain isolated from author
edits, so later publication validation never reads mutable author state.
Native IDs/admission replies remain authoritative. Original author objects are
reflected separately; they never carry fabricated publication or file receipts.

A parallel slot-reference vector retains distinct earlier native insertion IDs
when several author slots alias one object. Projection-only operations reuse the
already admitted reference occurrences in their prior order; additional aliases
reuse the existing reference. Explicit push appends the new occurrence IDs.
Native C++ validates the complete proposed sparse view as before. Removal,
replacement or reordering cannot cancel a file job or erase immutable history.
The accepted `attachmentViews` remains a distinct native projection extension;
it is not represented as stock reporter insertion history.

Ordinary completed attachments expose a mutable author entry. Typed-array body
input retains its exact original identity; string input exposes a distinct Buffer.
The original native request/publication metadata and captured upload bytes stay
private and independent of subsequent writes through either author alias.
No setter interception or boundary-time getter execution is introduced. Changes
made after the original author scope cannot retroactively change accepted native
history; a new push still requires an actually open original callback budget.
A path edit is an author reference only; a body edit changes author bytes only.
Neither copies or overwrites an already committed physical file.

## Bounds and proof limits

Existing declaration, slot, native metadata and actual retained-body quotas still
apply to every new push admission. No new native counter, version journal or
physical retirement path is added. The following direct-push D admission carries body plus optional authored path
metadata. Body bytes alone enter the existing writer; the path is neither read,
resolved, copied nor used as the writer source. A distinct wire discriminant
preserves present-empty path versus absence and the existing UTF16 metadata
codec preserves isolated units. Ordinary `attach(options)` keeps its existing
mutual-exclusion rule; this change applies only to direct array declarations. New unadmitted
objects inserted through non-push methods retain the existing native declaration
policy; the stock comparison above covers already admitted references only.

The previous latest-entry-value C++ journal draft was uncompiled and is preserved
privately as abandoned design; it is not registered, committed or activated.
SDK controls exercise reflection and packets, not actual native writers. Matching
end-to-end NodeHost/nativeRunner insertion/file proof remains required before a
combined runtime claim. Earlier array5 process proofs retain their exact frozen-
entry scope and are not relabeled as this following behavior.


## Direct-push body plus path following unit

One additional stock-public no-browser case passed under the same pinned versions
and actual supervisor. Its original object carried Buffer bytes `0080ff` and an
actual temporary path containing distinct bytes `414243`. Both original fields
reached the stock reporter after author edits, with original metadata and body
bytes; the actual input file retained its device/inode/bytes. This does not claim
that stock never read the input file. Evidence is preserved under
`/tmp/suitecut-attachment-entry-both-v1/actual-stock-v1` and `oracle`.

`AttachmentAppendSource` validates B/D/R/N wire shapes. D creates an ordinary
native Body declaration with a bounded optional author-path JSON field. Existing
original deadline, metadata/body quotas, writer admission, publication validation
and real callback/file retirement remain authoritative. JSON and JUnit already
preserve this declared path; validation now accepts it on Body, and HTML labels
it as metadata distinct from an actual committed body file. Controlled policy and
report tests are not actual file receipts. The authored matching process checker
has7 scopes for the Body+path unit, including actual body bytes versus the independent source-path
file; that following end-to-end proof remains pending.


Following focused validation: new native parser/declaration/report10 and existing
(adapted obsolete mutually-exclusive-shape assertion) declaration15 controls pass
optimized and ASan/UBSan/LSan, with unchanged private header inputs and empty
runtime stderr. Scoped SDK41 tests, strict type checking and lint pass. No actual runner was launched for this following unit.


## Ordinary existing-Buffer identity following unit

A final separately authored one-case stock public oracle used an actual existing
Buffer input, not the earlier string-body ordinary probe. After awaited attach,
its exposed `entry.body` was exactly the input Buffer: changing either alias
changed the other. The reporter still retained initial bytes `0080ff` after both
aliases reached `1117ff`. This one supervised no-browser child exited0 with no
stderr, unchanged pinned inputs and genuine physical closure; artifacts are under
`/tmp/suitecut-ordinary-attachment-buffer-v1`.

The SDK therefore retains the actual original typed-array body as an author
reference separately from copied native upload/publication metadata. Publication
reflects that reference only after the existing genuine native receipt; it cannot
rewrite the accepted bytes, writer result, inode or report history. Controlled
wire tests assert unchanged upload chunks while author aliases mutate. Matching
NodeHost/nativeRunner proof for this following identity refinement remains pending.


The separate eighth authored process scenario asserts exact original Buffer alias
identity in Node, real immutable native output bytes and metadata, real file inode
validation and the genuine publication-view ACK. It is added to the existing
checker rather than changing the earlier ordinary string-body scenario. SDK42
focused tests, strict type checking and lint pass; the eighth actual process case
remains unrun pending the matching composed Runner/SDK grant.
