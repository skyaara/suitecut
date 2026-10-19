# Native fixture declaration layers

This component preserves raw declaration layers and resolves them in C++.
`extendNativeFixtures(base, overrides)` appends immutable declaration layers;
the result can be used as a native suite's `fixtures` declaration. The original
flat record remains supported through its existing wire protocol.

Each actual setup callback has one contiguous declaration ID. A native version
has that ID, its original author name, layer number, previous declaration ID,
and an internal graph key. A self-named dependency binds to the previous
declaration; every other dependency binds to the final effective declaration.
An override inherits unspecified scope and automatic status. An explicit scope
change is invalid. Only final versions and previous versions genuinely reached
through dependencies enter the execution graph. A previous automatic version
does not become a second automatic root. Independent effective automatic
fixtures retain their first author-name insertion order when overridden.

The final graph is validated before any callback can be admitted: unknown
dependencies, cycles, worker-to-test dependencies, duplicate names within a
layer, malformed IDs, and capacity overflow reject the catalog. Limits are 32
layers, 256 raw declarations, 4096 raw dependency edges, and 2 MiB of charged
metadata. Shadowed declarations count toward raw limits even when not executed.
The unchanged `FixtureGraph` and `FixtureOwners` own planning, actual tokens,
callback readiness, and teardown; this catalog supplies their native keys.

The SDK helper snapshots and freezes raw declarations/dependency arrays while
retaining actual callback identity. It appends layers without resolving names,
filling inherited options, planning execution, or invoking setup callbacks.
Fixture names `layers` and `kind` remain ordinary names; only actually issued
layer objects are accepted as catalogs.

The adapter pins all raw `FLAYER`/`FDECL` rows into the existing retry metadata
signature. Native C++ supplies final author-name-to-key roots plus per-version
dependency aliases and effective scope to the host. Setup admission uses the
actual version key to select its original declared callback; the host executes
it and exposes aliases from those native mappings. Mapping validation rejects
foreign identities, altered original dependency names, missing targets and
post-admission remapping. The host does not choose target versions. Every
worker/test/hook lease
continues to use existing exact token/test/generation and original clock and
retirement rules. Reuse compatibility includes the catalog signature. No
per-fixture timeout, literal/option fixtures, JavaScript graph scheduling, or
physical process proof is claimed by the standalone component controls. The
separate authored public-process checker exercises actual native
setup/use/teardown, hooks, retries, native pre-admission rejection, and unchanged
flat fixtures.

Behavioral references are unchanged pinned public tests at revision
`606ecd8b34bac100db32561b6278ffd76e5eb3cd`: `fixtures.spec.ts` lines 548 and
603 cover previous-version/self and final-sibling binding; the override scope
errors in `fixture-errors.spec.ts` cover both scope-change directions. They are
references for the controlled catalog tests, not claimed unchanged runner runs.
