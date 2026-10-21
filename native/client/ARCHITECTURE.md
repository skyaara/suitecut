# Native SDK architecture

The SDK and the CEF runtime are separate products. Consumers build or install
`native/client` without CEF, Node, or a browser download. Runtime bundles add the
CEF engine and media tools separately; SDK installation does not launch a browser.

## Package layout

| Path                     | Owns                                                    | Private dependencies                   |
| ------------------------ | ------------------------------------------------------- | -------------------------------------- |
| `include/suitecut/`      | Installed C ABI and C++20 API                           | Standard library, other public headers |
| `src/core/`              | JSON values, credential storage, image decoding         | OpenSSL Crypto, Zlib, OS               |
| `src/wire/`              | Bounded pipe/WebSocket transport, TLS, process lifetime | Core, OpenSSL SSL, OS                  |
| `src/backends/chromium/` | CDP browser state and commands                          | Wire, shared DOM helpers               |
| `src/backends/firefox/`  | BiDi browser state and commands                         | Wire, shared DOM helpers               |
| `src/transport/`         | CEF binary decoder, backend selection, request lifetime | Both backends                          |
| `src/browser/`           | Browser facade, observations, visual interactions       | Core, public transport API             |
| `src/agent/`             | Agent policy, tools, observation/action workflows       | Browser and core public APIs           |
| `src/c_api/`             | ABI-v1 handles, error and ownership translation         | Public transport API                   |
| `cmake/`                 | Build policy, install and export metadata               | CMake                                  |
| `examples/`              | Standalone consumers and CLI implementations            | Public SDK                             |
| `tests/`                 | Unit tests, integrations, downstream package consumers  | Explicit test fixtures                 |
| `../shared/`             | Authored protocol-independent DOM/ARIA scripts          | Standard C++, no CEF headers           |

Each implementation directory has an explicit CMake OBJECT target and source
inventory. A module can include another module's private headers only through
an explicit dependency; there is no global `src/` include path. Firefox shares
the wire connection with Chromium without depending on CDP browser state.
`connection.hpp` retains the internal `CdpConnection` name for this shared
transport; it owns both CDP pipes and generic JSON-RPC WebSockets.

The installed target remains `SuiteCut::Client`, backed by one
`libsuitecut_client` archive or shared library. Object modules are implementation
boundaries, not separately supported binary APIs. This avoids archive ordering,
duplicate symbols, and a family of independently versioned backend ABIs.
No private header or build-only target is exported. C++ API compatibility
requires a compatible compiler/standard library; the versioned C ABI is the
foreign-language boundary. Windows runtime support is not qualified here.

## Ownership and bounded work

Transport owns process/connection teardown, pending requests, deadlines and event
budgets. Backends own protocol state. Browser and agent layers use those owners;
they must not introduce a thread per command, replay mutations on timeout, or
report logical close as completed physical teardown. Keep worker and queue
bounds explicit. Test actual state, media and process retirement through
`tests/cef`, including failures and cancellation.

## Making changes

Add implementation files to their module's CMake inventory. Keep public headers
self-contained and free of backend/CEF includes. Test fixtures stay under
`tests/support`; they are never part of an installed SDK. Build with one or two
compiler jobs on memory-constrained hosts. The release checks are local commands;
this reorganization does not enable CI.
Python is optional for ordinary SDK builds. With Python installed, CTest's
`architecture` label checks include boundaries and negative controls; configure
it alongside the `unit` label for future CI qualification. CMake always rejects
missing, duplicated or unlisted implementation source files.

Run unit tests and installed-package verification before browser qualification:

```sh
cmake -S native/client -B build/client -DBUILD_TESTING=ON
cmake --build build/client --parallel 2
ctest --test-dir build/client -L unit --output-on-failure --parallel 1
python3 native/client/tools/verify-package.py --output /absolute/new/output
```

The package verifier tests static and shared builds, relocation, C and C++
consumers, every public header in isolation, and embedding with `add_subdirectory`.
The source packager includes the SDK, shared helpers, license and notices only:

```sh
python3 native/client/tools/package-source.py --output /absolute/new/sdk.tar.gz
```

The browser embedder remains a separate CEF application. Its large translation
unit and controller headers are not claimed to be independently packaged C++
libraries by this SDK split. Runtime signing and platform/media qualification
remain separate release gates.
