# Native locator suggestions

`locator.suggest({ timeout?, signal? })` and
`elementHandle.suggestLocator({ timeout?, signal? })` return up to five native
recipes, readable code, diagnostics and creation provenance. Each suggestion
also exposes an ordinary `locator`. Future use resolves that query normally;
creation provenance is not a permanent uniqueness or DOM-stability guarantee.

The native C++ plan ranks observed test-ID attributes, labels, roles with exact
names, placeholder/alt/title, exact text, IDs, conservative alphabetic classes,
and an ancestry path. No SDK counter, selector ranking or retry chooses a
recipe. Utility-world code reads actual DOM facts using the existing semantic
query helper. C++ orders finite candidates, then one synchronous native capture
checks every candidate for exactly one result and identity with the selected
Element. Its actual backend node must equal the original observation's backend.
The last publication boundary rechecks native owner/generation/deadline,
frame/session/loader, numeric and unique main/utility contexts, and any retained
handle's exact original object/group/context/renderer ownership.

A retained handle is adopted through real DOM.describeNode/DOM.resolveNode into
its captured utility context; no selector reacquires it. An adopted node in a
different document is rejected. Missing unique identity, navigation, detach,
disposal and expired original deadlines fail rather than borrowing a new realm.
Actual Inspector callbacks and native operation groups remain held after
logical cancellation until their real ACK/group release or actual channel close.

Omitted candidates are diagnostic facts: ambiguous queries, a unique query for a
different node, unsupported/oversized text, or bounded observation exhaustion.
Text observation uses a finite light-DOM window; the real built-in selector is
still the verification authority. Accessible role/name facts use existing DOM
selector semantics, not a claim of full external AX parity. Authored isolated
UTF16/NUL test-ID values are omitted where a readable equivalent CSS recipe
cannot faithfully represent them. Candidate and result capacities are 24 and 5;
query and aggregate native DOM work are bounded under the original operation
budget.

`page.pickLocator({ x, y }, options)` and the inherited frame method use native
`DOM.getNodeForLocation` plus `DOM.resolveNode` in the captured session/utility
realm. Points are bounded integer viewport CSS pixels. Same-process child-frame
coordinates use actual native viewport dimensions, frame-owner content quads
and the existing projective mapping helper; native rounded Inspector pixels are
reported separately. Every ancestor document and native parent/session mapping
is pinned. An OOPIF target uses its own actual Inspector-root coordinates.

The backend must return a real positive backendNodeId and exactly the requested
frameId. Selecting a descendant/different frame, missing metadata or an
unavailable backend method fails explicitly; callers can request the actual
frame. There is no JavaScript elementFromPoint or root-page fallback. The first
observation and final verification must serialize the exact picked backend node.
`pointPicking` means that this result used the native point pipeline, with its
actual coordinates in `provenance.point`; ordinary locator/handle suggestions
have false. These are controlled implementation contracts; backend support in a
live pinned CEF browser is still unproved until matching integration runs.

Focused controls exercise actual C++ policy/controller/physical-ledger code with
controlled Inspector replies and CEF value graphs. SDK transport tests do not
claim real DOM/Inspector/browser behavior. Matching live CEF validation remains
separate and unrun.
