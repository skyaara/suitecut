# Native Page and Frame form selectors

`Page` and `Frame` methods `check`, `uncheck`, `setChecked`, `selectOption` and
`setInputFiles` select the first matching node by default. Their `strict: true`
option requires a single native match; `strict: false` explicitly requests the
default behavior. The SDK validates that option and sends the actual query and
matching policy to C++. It does not resolve, filter, or replace DOM nodes.

Locator methods remain strict. Element handles keep their actual node/frame
identity, including after a replacement appears with the same selector. Frame
methods run within their actual selected frame. An unavailable first match waits
under the original native timeout rather than falling through to a later match.
Existing form actionability, label association, native pointer input, option
selection and upload behavior are preserved.

`strict` is only a Page/Frame selector option. Each method retains its own other
option validation: checkbox actions accept position and trial, while option
selection and file uploads reject unsupported options. This slice adds no new
upload directory, empty-file-clear, or opaque event capability.

Run `scripts/verify-native-page-form-strict.mjs` against a matching native SDK to
check first-match and strict controls, real nonempty file bytes/trusted native
upload events, fixed-handle replacement, frame/label ownership and original
deadlines. The controlled SDK boundary tests do not replace that runtime proof.

The pinned upstream `page-check.spec.ts` (17 cases) and
`page-select-option.spec.ts` (33 cases) passed all 50 unchanged cases against
native binary `97916950c775fee46c3a6f361b6b130bf3ac62395d50b85e3d527bc626351f14`
with this isolated SDK patch. Collection required exactly 50 tests, with no
skips or retries. The native binary, checker, fixtures and sealed inputs stayed
unchanged; the SDK variant changed only the emitted form and Page modules. This
is a private SDK regression result, separate from a merged normal SDK proof.

The same authored seven-group checker first failed against that epoch's original
SDK when `Page.check` treated duplicate inputs as a strict locator. It then
passed all seven groups with the isolated SDK patch, including native first-match
and strict controls. Both runs retained actual shutdown and stderr evidence.

File chooser delivery keeps a failed adoption scoped to its own event when the
native frame registry confirms that child has detached. A waiter for that event
still rejects with the original adoption error; later chooser waits in surviving
frames remain available. Native subscription failures and adoption errors in a
still-live frame remain visible. A user listener exception is surfaced after
listener delivery with its original thrown value, rather than being stored as a
future chooser error. `scripts/verify-native-filechooser-lifetime.mjs` checks the
actual detached-child fixture, a subsequent root chooser with real File bytes
and trusted upload events, and repeated delivery with acknowledged handle cleanup.

The detached-child followed by root-chooser reproduction failed on the original
`97916950` SDK: the root click completed, but the new wait rejected with the old
child's `frame.form: Frame was detached` error. The three-group checker passed
with that same native binary and an isolated SDK changing only the chooser
module. Actual File bytes and trusted upload events passed, all eight selected
ownership counters reached zero, shutdown succeeded and stderr remained empty.
All 1853 sealed inputs, 230 runtime assets and the private SDK stayed unchanged.
The five controlled lifetime tests separately retain the same-event rejection,
subscription failure and original listener exception visibility.

The matching merged SDK/browser proof now passes on sealed native
`ff9863a9` with no SDK, native or checker overlays: the authored chooser lifetime
checker passed all three groups with actual File bytes/trusted native events,
all eight selected ownership fields zero, awaited physical shutdown and empty
native stderr. The unchanged checkbox17/selectOption33 suite separately collected
and passed all 50 cases, with no skips, retries or unexpected results and empty
native stderr. Both runs left all 1951 sealed inputs and 230 runtime assets
unchanged; the original suite also verified the entire sealed SDK before/after.
The earlier isolated-SDK results remain separate historical evidence.


The native oracle fixture maps `playwright.errors.TimeoutError` to the genuine
`TimeoutError` constructor exported by the selected native SDK and restores the
runner's original namespace after the fixture. It does not alter thrown errors,
prototypes, brands or original assertions. This is a declared fixture-surface
mapping, separate from a native implementation change.

With that mapping as the only checker variant, the three unchanged pinned
chooser timeout cases (explicit timeout, inherited default timeout, and explicit
timeout overriding an infinite default) passed on sealed `ff9863a9`. Required
collection was exactly three; no cases were skipped or retried, native stderr
was empty, and all 1951 inputs, 230 assets, the sealed SDK and variant checker
stayed unchanged. The older stock-constructor mismatches remain preserved as
historical evidence. This result does not cover the independent empty-file
reset failure, which remains vendor-pending.
