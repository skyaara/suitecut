# Contributing to suitecut

suitecut is native code: the CEF engine in `native/browser`, the C++ client SDK
and C ABI in `native/client`, and the helpers they share. Preserve source
ownership, failure propagation and sandboxing when changing these boundaries.

## Setup and checks

See [build requirements](docs/building.md) for the engine, the client SDK, macOS
packaging and the real-browser regression suite. Real browser checks need the
pinned CEF executable described in [native/browser/README.md](native/browser/README.md).

```sh
cmake --preset release -S native/client
cmake --build .suitecut/build/client-release --parallel 2
ctest --test-dir .suitecut/build/client-release --output-on-failure -L unit
python3 -m unittest discover -s tests/cef/unit -p 'test_*.py'
```

Build the engine with `-DSUITECUT_BUILD_BROWSER_CHECKS=ON` and run `ctest` in its
build directory for engine changes. Run the [full CEF suite](docs/cef-regressions.md)
for runtime qualification. Report missing runtime evidence as blocked, not
passing. Keep compiler jobs and browser workers bounded; on a 16 GB Mac use
`--parallel 2` to `4` and one browser worker. CI builds and tests the client
and engine on Linux and macOS for every pull request; see [CI and releases](docs/ci.md).

The website and benchmark harness use Node.js 22+ and pnpm 11 as tools only:

```sh
pnpm install --frozen-lockfile
pnpm lint
pnpm format:check
pnpm site:check
pnpm site:build
```

## Code boundaries

- Keep the client SDK buildable without CEF headers, and the C ABI versioned.
- Preserve bounded capture queues, capture timestamps and observable failures.
- Retain upstream license notices for CEF, Chromium, libyuv and other
  dependencies.
- Do not commit `.suitecut`, browser profiles or generated media.

Treat C ABI, installed header, CLI, MCP tool schema and embed kit layout changes
as public API changes.

## Review

Describe the concrete problem, resulting behavior, validation and remaining
limitations. Preserve unrelated work. Publishing, pushing, deployment and release
require explicit authorization. Contributions retain the repository MIT license.
