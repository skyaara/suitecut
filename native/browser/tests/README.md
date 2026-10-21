# Runtime test support

`architecture/` checks explicit source inventories, module dependency boundaries,
controller include ownership and PCH filtering. Its negative controls must fail
when an unlisted source, duplicate owner, missing fixture include or textual
`.cc` include is introduced. CTest labels these checks `native;architecture`.

`support/cef_library.h` loads the pinned CEF library for focused value/protocol
fixtures on macOS. Use `suitecut_link_cef_test` from `cmake/Tests.cmake` for these
fixtures: macOS uses CEF's dynamic wrapper, while Linux links libcef. Loading
CEF values is not evidence of a running browser or real Inspector replies.

The existing focused fixtures remain near their controller headers. They
instantiate the canonical domain `.inc` adapters with controlled receipt and
callback schedules. Preserve their assertions, deadlines and physical ownership
checks when moving code. A small callback that would be stored inline in
`std::function` is useful for checking completion consumption on libc++.

Run bounded component checks and then the real-browser package:

```sh
ctest --test-dir /absolute/engine-build --parallel 1 --output-on-failure -L architecture
python3 tests/cef --engine /absolute/path/to/suitecut-browser --profile full --workers 1
```

The full runner also requires a compiled client integration build and media
tools; see [the execution guide](../../../docs/cef-regressions.md). It verifies
real outcomes, process retirement and pinned source/runtime hashes. No CI job
is configured by this layout.
