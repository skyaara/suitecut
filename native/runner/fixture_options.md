# Native fixture literal and option policy

Suite fixtures accept callback declarations or explicit `{ value, option?,
scope?, automatic? }` data declarations. `fixtureOptions` configures known
option fixtures at suite scope. The native catalog owns final selection and
dependencies. The host executes a native-selected constant through a genuine
SETUP/use/READY/TEARDOWN fixture lease; the original authored callback is not
invoked. No fixture readiness or process-retirement receipt is synthesized.

Each raw declaration can have a value sidecar containing an optional `option`
flag and an optional literal. Absence of a literal means the declared authored
callback remains selected; an explicit undefined literal is distinct from
absence, JSON null, an empty string, and zero. `option` is inherited through
override layers. A previously registered `option:true` cannot be changed to
false. Scope and previous-self/final-sibling binding remain owned by the native
fixture catalog.

Suite configuration chooses constants only for known effective option names.
Unknown names, repeated overrides, non-option targets and malformed values
reject before any setup callback. This bounded native configuration is applied
to the final effective declaration; broader project/test.use configuration
precedence is outside this slice. A selected configuration constant omits the
original callback and its dependency admission, while preserving the raw
declaration and original dependencies for retry metadata/provenance. Other
dependencies still resolve to the final effective option version.

Native sources have distinct `Callback`, `DeclaredLiteral`, and
`ConfiguredLiteral` origins. The last two are values, not fabricated calls to
the original authored callback. The host runs their genuine SETUP/use/READY/TEARDOWN lifecycle with the exact
native fixture token, scope and generation. A retained-Source startup bridge
must be restricted to a selected authored callback, not a native-selected
constant. `FCONST` pins the exact selected declaration, value wire and origin
before that version is admitted; callback dependency aliases can be omitted
only after this native selection. Literal and configured values are bounded
serialized data, without authored object identity.

JSON values are bounded to 8192 serialized bytes, 2048 value nodes and depth
16; numbers must parse as finite doubles. Strings retain checked escaped UTF16
units, including isolated surrogates and NUL. Raw JSON is retained exactly;
undefined uses a separate typed marker. All literal/config metadata together
is limited to 2 MiB, with the existing 256-declaration/override bounds. Complex
author object identity, non-JSON values, per-project/test.use option selection
and explicit per-fixture timeout clocks are separate capabilities.

References are unchanged public tests at revision
`606ecd8b34bac100db32561b6278ffd76e5eb3cd`: worker option literals in
`fixtures.spec.ts:576`, and option-only config/change-of-option diagnostics in
`fixture-errors.spec.ts:793` and `:820`. Controlled policy tests are not claimed
as unchanged public runner executions.

The SDK raw encoder snapshots supported data, rejects accessors/holes/hidden
properties and preserves explicit undefined separately from JSON null and
negative zero. Unsupported nested undefined and non-JSON objects reject before
metadata emission. The configured value does not require invoking an option
callback or acquiring its now-unused callback dependencies. Metadata rows pin
both the original raw declaration/dependencies and configured bytes across
retry loads; native selection is not performed by the JavaScript layer.
